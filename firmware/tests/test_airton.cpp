// Tests hôte (macOS/Linux) de espac_airton.h — sans ESPHome.
// Compilation : voir run_tests.sh
#define ESPAC_HOST_TEST 1

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// --- Bouchons ESPHome -------------------------------------------------------
#define ESP_LOGD(tag, ...) ((void) 0)
#define ESP_LOGI(tag, ...) ((void) 0)
#define ESP_LOGW(tag, ...) (printf("    [WARN] "), printf(__VA_ARGS__), printf("\n"))

namespace esphome {
// Même algorithme que esphome/core/helpers.cpp avec les paramètres par défaut :
// CRC-16 polynôme réfléchi 0xA001, init 0xFFFF, refin=false, refout=false (pas de xorout).
// (= CRC-16/MODBUS)
uint16_t crc16(const uint8_t *data, uint16_t len, uint16_t crc = 0xffff, uint16_t reverse_poly = 0xa001) {
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++)
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ reverse_poly) : static_cast<uint16_t>(crc >> 1);
  }
  return crc;
}
}  // namespace esphome

#include "../espac_airton.h"

using namespace espac;

static int g_fail = 0, g_ok = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (cond) {                                                         \
      g_ok++;                                                           \
    } else {                                                            \
      g_fail++;                                                         \
      printf("  ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                   \
  } while (0)
#define CHECK_EQ_STR(a, b)                                                                   \
  do {                                                                                       \
    std::string _a = (a), _b = (b);                                                          \
    if (_a == _b) {                                                                          \
      g_ok++;                                                                                \
    } else {                                                                                 \
      g_fail++;                                                                              \
      printf("  ÉCHEC %s:%d\n    obtenu  : %s\n    attendu : %s\n", __FILE__, __LINE__, _a.c_str(), \
             _b.c_str());                                                                    \
    }                                                                                        \
  } while (0)

static std::string hex(const std::vector<uint8_t> &v) { return to_hex(v.data(), v.size()); }

// Fabrique une trame RX de 34 octets à partir d'une trame TX (mêmes champs décalés de +1).
static std::vector<uint8_t> make_rx(uint8_t room, uint8_t b12, uint8_t b13, uint8_t b14, uint8_t b15) {
  std::vector<uint8_t> f(34, 0);
  f[0] = 0x7A;
  f[1] = 0x7A;
  f[10] = room;
  f[13] = b12;
  f[14] = b13;
  f[15] = b14;
  f[16] = b15;
  return f;
}

static AcState base_state() {
  AcState s;
  s.power = true;
  s.mode = 1;     // cool
  s.fan = 3;      // medium
  s.target = 22;
  s.swing_v = 4;  // medium
  s.swing_h = false;
  s.display = true;
  s.clean = false;
  s.mute = true;
  return s;
}

static void test_crc() {
  printf("- CRC16 (vecteur standard MODBUS)\n");
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(esphome::crc16(v, 9) == 0x4B37);
}

static void test_encode_reference() {
  printf("- Encodage : trame identique à l'original\n");
  // Original pour cool / medium / 22 °C / volet medium / écran ON / muet :
  // b12 = (1|8)|(3*16)=0x39, b13 = 6, b14 = 0|4, b15 = 0x8C filtré en 0x88, b16 = 1.
  // Trame et CRC calculés indépendamment (Python).
  CHECK_EQ_STR(hex(encode(base_state())), "7A 7A 21 D5 18 00 00 A1 00 00 00 00 39 06 04 88 01 00 00 00 00 00 E7 4D");
  CHECK(encode(base_state()).size() == TX_FRAME_LEN);

  AcState s;
  s.power = true;
  s.mode = 4;  // heat
  s.fan = 0;   // auto
  s.target = 24;
  s.swing_v = 1;
  s.swing_h = true;
  s.display = false;
  s.clean = true;
  s.mute = false;
  CHECK_EQ_STR(hex(encode(s)), "7A 7A 21 D5 18 00 00 A1 00 00 00 00 0C 08 11 44 00 00 00 00 00 00 86 DD");
}

static void test_flags_table() {
  printf("- Drapeaux b15 (valeurs de l'original 4/68/136/200)\n");
  AcState s = base_state();
  s.display = false; s.clean = false; CHECK(encode_b15(s) == 4);
  s.display = false; s.clean = true;  CHECK(encode_b15(s) == 68);
  s.display = true;  s.clean = false; CHECK(encode_b15(s) == 136);
  s.display = true;  s.clean = true;  CHECK(encode_b15(s) == 200);
  // Décodage, y compris les variantes que l'original normalisait (0, 128, 140, 204).
  struct { uint8_t raw; bool disp, clean; } cases[] = {
      {4, false, false}, {68, false, true}, {136, true, false}, {200, true, true},
      {0, false, false}, {128, false, false}, {140, true, false}, {204, true, true}};
  for (auto &c : cases) {
    AcState out;
    decode(make_rx(21, 0x39, 6, 4, c.raw).data(), 34, AcState(), out);
    CHECK(out.display == c.disp);
    CHECK(out.clean == c.clean);
  }
}

static void test_turbo() {
  printf("- Turbo = 13 (bug de l'original corrigé)\n");
  AcState s = base_state();
  uint8_t code;
  CHECK(name_to_code(FANS, "turbo", code) && code == 13);
  s.fan = 13;
  CHECK(encode_b12(s) == 0xD9);
  CHECK_EQ_STR(hex(encode(s)), "7A 7A 21 D5 18 00 00 A1 00 00 00 00 D9 06 04 88 01 00 00 00 00 00 B1 49");
  AcState out;
  decode(make_rx(21, 0xD9, 6, 4, 0x88).data(), 34, AcState(), out);
  CHECK(out.fan == 13);
  CHECK_EQ_STR(code_to_name(FANS, out.fan), "turbo");
}

static void test_power_off() {
  printf("- Power OFF : b12 bas = 0x05, mode mémorisé\n");
  AcState s = base_state();
  s.power = false;
  CHECK(encode_b12(s) == 0x35);
  CHECK_EQ_STR(hex(encode(s)), "7A 7A 21 D5 18 00 00 A1 00 00 00 00 35 06 04 88 01 00 00 00 00 00 D8 8D");
  // RX à l'arrêt : le mode précédent (heat) est conservé.
  AcState prev = base_state();
  prev.mode = 4;
  AcState out;
  decode(make_rx(19, 0x35, 6, 4, 0x88).data(), 34, prev, out);
  CHECK(out.power == false);
  CHECK(out.mode == 4);
  // Remise en marche : le dernier mode revient.
  out.power = true;
  CHECK((encode_b12(out) & 0x0F) == (0x08 | 4));
}

static void test_roundtrip() {
  printf("- Aller-retour encode -> decode sur toutes les combinaisons\n");
  int n = 0;
  for (int power = 0; power < 2; power++)
    for (auto &m : MODES)
      for (auto &f : FANS)
        for (int t = 16; t <= 31; t++)
          for (auto &sv : SWINGS_V)
            for (int sh = 0; sh < 2; sh++)
              for (int d = 0; d < 2; d++)
                for (int c = 0; c < 2; c++) {
                  AcState s;
                  s.power = power;
                  s.mode = m.code;
                  s.fan = f.code;
                  s.target = static_cast<uint8_t>(t);
                  s.swing_v = sv.code;
                  s.swing_h = sh;
                  s.display = d;
                  s.clean = c;
                  s.mute = true;
                  auto tx = encode(s);
                  // La clim renvoie b12..b15 aux offsets 13..16.
                  auto rx = make_rx(20, tx[12], tx[13], tx[14], tx[15]);
                  AcState prev;
                  prev.mode = m.code;  // à l'arrêt le mode vient de l'état précédent
                  AcState out;
                  bool ok = decode(rx.data(), rx.size(), prev, out);
                  if (!ok || diff_fields(s, out) != 0) {
                    g_fail++;
                    printf("  ÉCHEC aller-retour : %s\n", hex(tx).c_str());
                    return;
                  }
                  n++;
                }
  g_ok++;
  printf("    %d combinaisons OK\n", n);
}

static void test_decode_guards() {
  printf("- Décodage : longueur minimale, valeurs hors table\n");
  AcState out;
  auto rx = make_rx(21, 0x39, 6, 4, 0x88);
  CHECK(!decode(rx.data(), 33, AcState(), out));
  AcState prev = base_state();
  // fan = 9 (inconnu), consigne 40 (hors plage), volet 9 (inconnu) : valeurs précédentes conservées.
  decode(make_rx(21, 0x99, 24, 0x09, 0x88).data(), 34, prev, out);
  CHECK(out.fan == prev.fan);
  CHECK(out.target == prev.target);
  CHECK(out.swing_v == prev.swing_v);
  CHECK(out.mode == 1);
  CHECK(out.room_temp == 21);
}

static void test_splitter() {
  printf("- Découpage des trames (timeout + en-tête)\n");
  FrameSplitter sp(30);
  auto a = make_rx(21, 0x39, 6, 4, 0x88);
  auto b = make_rx(22, 0x3C, 8, 4, 0x88);
  uint32_t t = 1000;
  sp.push(0x00, t);  // octet parasite avant l'en-tête
  for (auto x : a) sp.push(x, ++t);
  for (auto x : b) sp.push(x, ++t);  // deux trames collées (boucle ralentie)
  CHECK(sp.poll(t + 10).empty());    // pas encore de silence
  auto frames = sp.poll(t + 31);
  CHECK(frames.size() == 2);
  if (frames.size() == 2) {
    CHECK(frames[0] == a);
    CHECK(frames[1] == b);
  }
  // Sans en-tête : bloc conservé tel quel.
  for (int i = 0; i < 40; i++) sp.push(static_cast<uint8_t>(i), ++t);
  frames = sp.poll(t + 50);
  CHECK(frames.size() == 1 && frames[0].size() == 40);
}

struct Pub {
  std::string key, value;
  bool retain;
};

static void test_gateway() {
  printf("- Passerelle : sync, set, fenêtre d'ignorance, publication sur changement\n");
  Gateway gw;
  std::vector<Pub> pubs;
  std::vector<std::vector<uint8_t>> sent;
  gw.set_publisher([&](const std::string &k, const std::string &v, bool r) { pubs.push_back({k, v, r}); });
  gw.set_sender([&](const std::vector<uint8_t> &f) { sent.push_back(f); });
  auto last = [&](const std::string &k) -> std::string {
    for (auto it = pubs.rbegin(); it != pubs.rend(); ++it)
      if (it->key == k) return it->value;
    return "<absent>";
  };
  auto count = [&](const std::string &k) {
    int n = 0;
    for (auto &p : pubs) n += p.key == k;
    return n;
  };

  // Avant toute RX : aucun TX, aucun état publié.
  CHECK(!gw.on_set("power", "ON", 0));
  gw.loop(1000);
  CHECK(sent.empty());
  CHECK(count("power") == 0);

  // Première trame : cool / medium / 22 / écran ON, éteinte.
  auto rx_off = make_rx(21, 0x35, 6, 4, 0x88);
  CHECK(gw.on_rx_frame(rx_off, 1000));
  CHECK(gw.synced());
  CHECK_EQ_STR(last("power"), "OFF");
  CHECK_EQ_STR(last("fan"), "medium");
  CHECK_EQ_STR(last("target_temperature"), "22");
  CHECK_EQ_STR(last("room_temperature"), "21");
  CHECK(count("last_frame") == 1);
  for (auto &p : pubs) {
    if (p.key == "last_frame") CHECK(!p.retain);
    else CHECK(p.retain);
  }

  // Trame identique : rien n'est republié (last_frame compris).
  size_t before = pubs.size();
  gw.on_rx_frame(rx_off, 2000);
  CHECK(pubs.size() == before);

  // Valeurs invalides ignorées.
  CHECK(!gw.on_set("mode", "turbo", 2100));
  CHECK(!gw.on_set("target_temperature", "35", 2100));
  CHECK(!gw.on_set("target_temperature", "abc", 2100));
  CHECK(!gw.on_set("inconnu", "1", 2100));

  // set power=ON + mode=heat : publication optimiste immédiate, une seule trame après anti-rebond.
  CHECK(gw.on_set("power", "ON", 3000));
  CHECK(gw.on_set("mode", "heat", 3050));
  CHECK_EQ_STR(last("power"), "ON");
  CHECK_EQ_STR(last("mode"), "heat");
  gw.loop(3100);
  CHECK(sent.empty());  // anti-rebond encore actif
  gw.loop(3250);
  CHECK(sent.size() == 1);
  if (!sent.empty()) CHECK(sent[0][12] == 0x3C);  // medium | marche | heat

  // RX contradictoire (encore éteinte) pendant la fenêtre : ignorée, sauf temp. pièce.
  auto rx_off2 = make_rx(23, 0x35, 6, 4, 0x88);
  gw.on_rx_frame(rx_off2, 4000);
  CHECK_EQ_STR(last("power"), "ON");
  CHECK_EQ_STR(last("room_temperature"), "23");

  // RX confirmant la commande : acceptée.
  gw.on_rx_frame(make_rx(23, 0x3C, 6, 4, 0x88), 4500);
  CHECK_EQ_STR(last("power"), "ON");
  CHECK_EQ_STR(last("mode"), "heat");

  // Changement venu de la télécommande (hors fenêtre) : pris en compte (lecture continue).
  gw.on_rx_frame(make_rx(23, 0xDC, 7, 4, 0x88), 20000);
  CHECK_EQ_STR(last("fan"), "turbo");
  CHECK_EQ_STR(last("target_temperature"), "23");

  // Commande non confirmée : après la fenêtre, l'état de la clim l'emporte.
  CHECK(gw.on_set("display", "OFF", 21000));
  gw.loop(21200);
  CHECK_EQ_STR(last("display"), "OFF");
  gw.on_rx_frame(make_rx(23, 0xDC, 7, 4, 0x88), 25000);
  CHECK_EQ_STR(last("display"), "ON");

  // mute : non renvoyé par la clim, conservé.
  CHECK(gw.on_set("mute", "OFF", 26000));
  gw.loop(26200);
  CHECK(sent.back()[16] == 0);
  gw.on_rx_frame(make_rx(23, 0xDC, 7, 4, 0x88), 26300);
  CHECK_EQ_STR(last("mute"), "OFF");

  // power OFF depuis heat puis ON : le mode heat revient.
  gw.on_set("power", "off", 30000);
  gw.loop(30200);
  CHECK((sent.back()[12] & 0x0F) == 0x05);
  gw.on_set("power", "ON", 31000);
  gw.loop(31200);
  CHECK((sent.back()[12] & 0x0F) == 0x0C);

  // resync : republie tout, même sans changement.
  int p = count("power");
  CHECK(gw.on_set("resync", "", 32000));
  CHECK(count("power") == p + 1);

  // unit_link
  CHECK(gw.unit_link(26300 + 59000));
  CHECK(!gw.unit_link(26300 + 60001));
}

int main() {
  printf("Tests espac_airton.h\n");
  test_crc();
  test_encode_reference();
  test_flags_table();
  test_turbo();
  test_power_off();
  test_roundtrip();
  test_decode_guards();
  test_splitter();
  test_gateway();
  printf("\n%d vérifications OK, %d échec(s)\n", g_ok, g_fail);
  return g_fail == 0 ? 0 : 1;
}
