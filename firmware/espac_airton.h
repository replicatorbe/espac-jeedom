// espac_airton.h — protocole série des climatiseurs Airton + passerelle MQTT.
//
// Code original du projet espac-jeedom. Seuls les *faits* du protocole (offsets,
// bits, CRC) proviennent de l'observation documentée par le projet ESPAC de
// dOHMotik (https://github.com/git-greatguy/espac). Voir docs/CONTRAT-MQTT.md.
//
// Ce fichier ne dépend que de la STL et de esphome::crc16 (+ macros ESP_LOGx),
// ce qui permet de le compiler sur l'hôte pour les tests (firmware/tests/).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifndef ESPAC_HOST_TEST
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#endif

namespace espac {

static const char *const TAG = "espac";

// ---------------------------------------------------------------------------
// Constantes du protocole
// ---------------------------------------------------------------------------
static const size_t TX_FRAME_LEN = 24;
static const size_t RX_MIN_LEN = 34;
static const uint8_t HDR0 = 0x7A;
static const uint8_t HDR1 = 0x7A;

// En-tête fixe des trames ESP -> clim (12 octets).
static const uint8_t TX_HEADER[12] = {0x7A, 0x7A, 0x21, 0xD5, 0x18, 0x00,
                                      0x00, 0xA1, 0x00, 0x00, 0x00, 0x00};

// Structure commune observée sur la vraie clim (RX) :
//   [0..1] 7A 7A, [2] émetteur (D5 état / D1 battement), [3] destinataire (21),
//   [4] longueur totale de la trame, [7] type, ..., [n-2..n-1] CRC16 (même algo que TX).
static const size_t RX_LEN_BYTE = 4;
static const size_t RX_TYPE_BYTE = 7;
static const uint8_t RX_TYPE_STATE = 0xA3;      // état complet (34 o), envoyé à chaque changement
static const uint8_t RX_TYPE_HEARTBEAT = 0xA4;  // battement de cœur (13 o), toutes les 10 s
// 0xA6 (28 o) : trame complémentaire suivant chaque état, contenu nul observé.

// Offsets dans la trame RX (clim -> ESP).
static const size_t RX_ROOM_TEMP = 10;
static const size_t RX_MODE_FAN = 13;  // même format que b12 TX
static const size_t RX_SETPOINT = 14;  // même format que b13 TX
static const size_t RX_SWING = 15;     // même format que b14 TX
static const size_t RX_FLAGS = 16;     // même format que b15 TX

// b12 : bits 0-2 = mode, bit 3 = marche, quartet haut = ventilation.
static const uint8_t POWER_BIT = 0x08;
static const uint8_t MODE_OFF_CODE = 0x05;  // « mode » envoyé quand power=OFF

// b15 : drapeaux. Valeurs effectivement émises par l'original :
//   aucun = 0x04, nettoyage = 0x44, écran = 0x88, écran+nettoyage = 0xC8.
static const uint8_t FLAG_BASE = 0x04;
static const uint8_t FLAG_DISPLAY = 0x88;  // bits 3 + 7
static const uint8_t FLAG_CLEAN = 0x40;    // bit 6 (+ bit 2 = base quand écran éteint)
static const uint8_t FLAG_DISPLAY_RX = 0x08;  // bit lu pour l'écran en RX
static const uint8_t FLAG_CLEAN_RX = 0x40;    // bit lu pour le nettoyage en RX
static const uint8_t FLAG_UNKNOWN_MASK = 0x33;  // bits 0,1,4,5 : inconnus, recopiés tels quels

// ---------------------------------------------------------------------------
// Tables valeurs <-> chaînes du contrat MQTT
// ---------------------------------------------------------------------------
struct Code {
  uint8_t code;
  const char *name;
};

static const Code MODES[] = {{0, "auto"}, {1, "cool"}, {2, "dry"}, {3, "fan_only"}, {4, "heat"}};

// Ventilation : codes du quartet haut de b12. Turbo = 13 (l'original envoyait 7 : bug).
// Cycle de la télécommande : 5 → 13 → 6 → 0 → silence → 1 → 2 → 3 → 4 → 5.
// « quiet » (silence) n'est pas un code du quartet : c'est ventilation 0 + bit 0x40 de l'octet consigne.
static const uint8_t FAN_QUIET = 0x0F;          // code interne (jamais émis tel quel)
static const uint8_t SETPOINT_QUIET_BIT = 0x40; // bit « silence » de l'octet consigne
static const Code FANS[] = {{0, "auto"},      {1, "very_low"}, {2, "low"},   {3, "medium"},     {4, "high"},
                            {5, "very_high"}, {6, "humidity"}, {13, "turbo"}, {FAN_QUIET, "quiet"}};

static const Code SWINGS_V[] = {{0, "off"},    {1, "auto"}, {2, "very_high"}, {3, "high"},
                                {4, "medium"}, {5, "low"},  {6, "very_low"}};

template<size_t N> inline const char *code_to_name(const Code (&table)[N], uint8_t code) {
  for (size_t i = 0; i < N; i++)
    if (table[i].code == code)
      return table[i].name;
  return nullptr;
}

template<size_t N> inline bool name_to_code(const Code (&table)[N], const std::string &name, uint8_t &out) {
  for (size_t i = 0; i < N; i++) {
    if (name == table[i].name) {
      out = table[i].code;
      return true;
    }
  }
  return false;
}

inline std::string to_lower(std::string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return s;
}

inline std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n\"");
  if (a == std::string::npos)
    return "";
  size_t b = s.find_last_not_of(" \t\r\n\"");
  return s.substr(a, b - a + 1);
}

// Accepte ON/OFF (insensible à la casse) ainsi que 1/0, true/false.
inline bool parse_bool(const std::string &raw, bool &out) {
  std::string v = to_lower(trim(raw));
  if (v == "on" || v == "1" || v == "true") {
    out = true;
    return true;
  }
  if (v == "off" || v == "0" || v == "false") {
    out = false;
    return true;
  }
  return false;
}

inline const char *bool_str(bool b) { return b ? "ON" : "OFF"; }

inline std::string to_hex(const uint8_t *data, size_t len) {
  static const char *digits = "0123456789ABCDEF";
  std::string s;
  s.reserve(len * 3);
  for (size_t i = 0; i < len; i++) {
    if (i)
      s.push_back(' ');
    s.push_back(digits[data[i] >> 4]);
    s.push_back(digits[data[i] & 0x0F]);
  }
  return s;
}

// ---------------------------------------------------------------------------
// État de la clim
// ---------------------------------------------------------------------------
struct AcState {
  bool power = false;
  uint8_t mode = 3;         // dernier mode actif (conservé quand power=OFF)
  uint8_t fan = 0;          // code du quartet haut (0..6, 13)
  uint8_t target = 24;      // consigne °C (16..31)
  uint8_t target_extra = 0; // bits 5 et 7 de l'octet consigne (inconnus) : recopiés tels quels
  uint8_t swing_v = 0;      // 0..6
  bool swing_h = false;
  bool display = true;
  bool mute = true;         // non lu en RX : piloté uniquement par l'ESP
  bool clean = false;
  uint8_t flags_extra = 0;  // bits inconnus de b15, recopiés depuis la clim
  int room_temp = 0;        // °C (RX uniquement)
};

// Masques de champs (utilisés pour la fenêtre d'ignorance après commande).
enum Field : uint16_t {
  F_POWER = 1 << 0,
  F_MODE = 1 << 1,
  F_FAN = 1 << 2,
  F_TARGET = 1 << 3,
  F_SWING_V = 1 << 4,
  F_SWING_H = 1 << 5,
  F_DISPLAY = 1 << 6,
  F_MUTE = 1 << 7,
  F_CLEAN = 1 << 8,
};

// ---------------------------------------------------------------------------
// Encodage ESP -> clim (24 octets, CRC16 ESPHome, MSB d'abord)
// ---------------------------------------------------------------------------
inline uint8_t encode_b12(const AcState &s) {
  uint8_t low = s.power ? static_cast<uint8_t>(POWER_BIT | (s.mode & 0x07)) : MODE_OFF_CODE;
  uint8_t fan = s.fan == FAN_QUIET ? 0 : s.fan;
  return static_cast<uint8_t>(((fan & 0x0F) << 4) | low);
}

inline uint8_t encode_b15(const AcState &s) {
  uint8_t b = s.display ? FLAG_DISPLAY : FLAG_BASE;
  if (s.clean)
    b |= FLAG_CLEAN;
  return static_cast<uint8_t>(b | (s.flags_extra & FLAG_UNKNOWN_MASK));
}

inline std::vector<uint8_t> encode(const AcState &s) {
  std::vector<uint8_t> f(TX_HEADER, TX_HEADER + sizeof(TX_HEADER));
  f.push_back(encode_b12(s));
  uint8_t t = s.target < 16 ? 16 : (s.target > 31 ? 31 : s.target);
  uint8_t quiet = s.fan == FAN_QUIET ? SETPOINT_QUIET_BIT : 0;
  f.push_back(static_cast<uint8_t>((t - 16) | (s.target_extra & 0xA0) | quiet));
  f.push_back(static_cast<uint8_t>(((s.swing_h ? 1 : 0) << 4) | (s.swing_v & 0x0F)));
  f.push_back(encode_b15(s));
  f.push_back(s.mute ? 1 : 0);
  for (int i = 0; i < 5; i++)
    f.push_back(0x00);
  uint16_t crc = esphome::crc16(f.data(), static_cast<uint16_t>(f.size()));
  f.push_back(static_cast<uint8_t>(crc >> 8));
  f.push_back(static_cast<uint8_t>(crc & 0xFF));
  return f;
}

// ---------------------------------------------------------------------------
// Décodage clim -> ESP
// ---------------------------------------------------------------------------
// CRC RX : hypothèse = même CRC que TX sur tout sauf les 2 derniers octets.
// Non confirmé : n'est appliqué que si rx_check_crc vaut true.
inline bool rx_crc_ok(const uint8_t *buf, size_t len) {
  if (len < 3)
    return false;
  uint16_t crc = esphome::crc16(buf, static_cast<uint16_t>(len - 2));
  return buf[len - 2] == (crc >> 8) && buf[len - 1] == (crc & 0xFF);
}

// Décode une trame RX dans `out` (initialisé à partir de `prev`). Les champs dont la
// valeur est hors table conservent leur valeur précédente. Retourne false si trop courte.
inline bool decode(const uint8_t *buf, size_t len, const AcState &prev, AcState &out) {
  if (len < RX_MIN_LEN)
    return false;
  out = prev;
  out.room_temp = static_cast<int8_t>(buf[RX_ROOM_TEMP]);

  uint8_t mf = buf[RX_MODE_FAN];
  out.power = (mf & POWER_BIT) != 0;
  uint8_t m = mf & 0x07;
  if (code_to_name(MODES, m) != nullptr)
    out.mode = m;  // m == 5 (« Off ») : on garde le dernier mode
  uint8_t fan = mf >> 4;
  if (fan == 0 && (buf[RX_SETPOINT] & SETPOINT_QUIET_BIT))
    out.fan = FAN_QUIET;
  else if (fan != FAN_QUIET && code_to_name(FANS, fan) != nullptr)
    out.fan = fan;

  // Bits 0-4 = consigne − 16 ; bits 5-7 = drapeaux (0x40 vu en mode froid), conservés.
  int t = (buf[RX_SETPOINT] & 0x1F) + 16;
  if (t >= 16 && t <= 31) {
    out.target = static_cast<uint8_t>(t);
    out.target_extra = buf[RX_SETPOINT] & 0xA0;
  }

  uint8_t sw = buf[RX_SWING];
  uint8_t sh = sw >> 4;
  if (sh <= 1)
    out.swing_h = sh == 1;
  uint8_t sv = sw & 0x0F;
  if (sv <= 6)
    out.swing_v = sv;

  uint8_t fl = buf[RX_FLAGS];
  out.display = (fl & FLAG_DISPLAY_RX) != 0;
  out.clean = (fl & FLAG_CLEAN_RX) != 0;
  out.flags_extra = fl & FLAG_UNKNOWN_MASK;
  return true;
}

// Champs pilotables qui diffèrent entre a et b.
inline uint16_t diff_fields(const AcState &a, const AcState &b) {
  uint16_t d = 0;
  if (a.power != b.power)
    d |= F_POWER;
  if (a.mode != b.mode)
    d |= F_MODE;
  if (a.fan != b.fan)
    d |= F_FAN;
  if (a.target != b.target)
    d |= F_TARGET;
  if (a.swing_v != b.swing_v)
    d |= F_SWING_V;
  if (a.swing_h != b.swing_h)
    d |= F_SWING_H;
  if (a.display != b.display)
    d |= F_DISPLAY;
  if (a.mute != b.mute)
    d |= F_MUTE;
  if (a.clean != b.clean)
    d |= F_CLEAN;
  return d;
}

// ---------------------------------------------------------------------------
// Sauvegarde de l'état en flash (64 bits) : permet de commander la clim dès le
// redémarrage de l'ESP, sans attendre que la clim envoie une trame d'état.
// ---------------------------------------------------------------------------
static const uint8_t PACK_MAGIC = 0xE5;  // à changer si le format évolue

inline uint64_t pack(const AcState &s) {
  uint64_t v = 0;
  v |= (uint64_t) PACK_MAGIC;
  v |= (uint64_t) (s.mode & 0x07) << 8;
  v |= (uint64_t) (s.power ? 1 : 0) << 11;
  v |= (uint64_t) (s.swing_h ? 1 : 0) << 12;
  v |= (uint64_t) (s.display ? 1 : 0) << 13;
  v |= (uint64_t) (s.mute ? 1 : 0) << 14;
  v |= (uint64_t) (s.clean ? 1 : 0) << 15;
  v |= (uint64_t) (s.fan & 0x0F) << 16;
  v |= (uint64_t) (s.swing_v & 0x0F) << 20;
  v |= (uint64_t) s.target << 24;
  v |= (uint64_t) s.target_extra << 32;
  v |= (uint64_t) s.flags_extra << 40;
  return v;
}

// Retourne false si la valeur n'est pas une sauvegarde valide (flash vierge, ancien format).
inline bool unpack(uint64_t v, AcState &s) {
  if ((v & 0xFF) != PACK_MAGIC)
    return false;
  AcState o;
  o.mode = (v >> 8) & 0x07;
  o.power = (v >> 11) & 1;
  o.swing_h = (v >> 12) & 1;
  o.display = (v >> 13) & 1;
  o.mute = (v >> 14) & 1;
  o.clean = (v >> 15) & 1;
  o.fan = (v >> 16) & 0x0F;
  o.swing_v = (v >> 20) & 0x0F;
  o.target = (v >> 24) & 0xFF;
  o.target_extra = (v >> 32) & 0xFF;
  o.flags_extra = (v >> 40) & 0xFF;
  if (code_to_name(MODES, o.mode) == nullptr || code_to_name(FANS, o.fan) == nullptr ||
      code_to_name(SWINGS_V, o.swing_v) == nullptr || o.target < 16 || o.target > 31)
    return false;
  s = o;
  return true;
}

// ---------------------------------------------------------------------------
// Découpage du flux UART en trames (timeout inter-octets + en-tête 7A 7A)
// ---------------------------------------------------------------------------
class FrameSplitter {
 public:
  explicit FrameSplitter(uint32_t gap_ms = 30, size_t max_len = 256) : gap_ms_(gap_ms), max_len_(max_len) {}

  void set_gap(uint32_t gap_ms) { gap_ms_ = gap_ms; }

  void push(uint8_t b, uint32_t now) {
    if (buf_.size() >= max_len_) {
      ESP_LOGW(TAG, "Tampon RX plein (%u o), vidé", (unsigned) buf_.size());
      buf_.clear();
    }
    buf_.push_back(b);
    last_ms_ = now;
  }

  // Appelé quand aucun octet n'est disponible : renvoie les trames terminées.
  std::vector<std::vector<uint8_t>> poll(uint32_t now) {
    std::vector<std::vector<uint8_t>> frames;
    if (buf_.empty() || (now - last_ms_) < gap_ms_)
      return frames;
    split_on_header_(buf_, frames);
    buf_.clear();
    return frames;
  }

  // Découpe un bloc : si l'en-tête 7A 7A est présent, les trames commencent à chaque
  // occurrence (les octets parasites avant le premier en-tête sont écartés, mais
  // conservés en tant que trame s'il n'y a aucun en-tête du tout).
  static void split_on_header_(const std::vector<uint8_t> &in, std::vector<std::vector<uint8_t>> &out) {
    std::vector<size_t> starts;
    for (size_t i = 0; i + 1 < in.size(); i++) {
      if (in[i] == HDR0 && in[i + 1] == HDR1) {
        starts.push_back(i);
        i++;  // 7A 7A 7A ne compte qu'une fois
      }
    }
    if (starts.empty()) {
      out.push_back(in);
      return;
    }
    for (size_t k = 0; k < starts.size(); k++) {
      size_t a = starts[k];
      size_t b = (k + 1 < starts.size()) ? starts[k + 1] : in.size();
      // Une trame trop courte suivie d'un « en-tête » est probablement un faux en-tête
      // dans les données : on la fusionne avec la suivante.
      while (k + 1 < starts.size() && (b - a) < RX_MIN_LEN) {
        k++;
        b = (k + 1 < starts.size()) ? starts[k + 1] : in.size();
      }
      out.emplace_back(in.begin() + a, in.begin() + b);
    }
  }

 private:
  std::vector<uint8_t> buf_;
  uint32_t last_ms_ = 0;
  uint32_t gap_ms_;
  size_t max_len_;
};

// ---------------------------------------------------------------------------
// Passerelle : état, commandes MQTT, publication
// ---------------------------------------------------------------------------
class Gateway {
 public:
  // publish(clé, valeur, retain) : la colle YAML construit le topic espac/<dev>/state/<clé>.
  // Retourne false si la publication a échoué (MQTT déconnecté) : la valeur sera retentée.
  using PublishFn = std::function<bool(const std::string &, const std::string &, bool)>;
  using SendFn = std::function<void(const std::vector<uint8_t> &)>;

  uint32_t ignore_window_ms = 3000;  // fenêtre d'ignorance des RX contradictoires
  uint32_t tx_debounce_ms = 150;     // regroupe plusieurs `set` rapprochés en une trame
  uint32_t link_timeout_ms = 60000;  // unit_link
  bool check_rx_crc = true;

  void set_publisher(PublishFn fn) { publish_ = std::move(fn); }
  void set_sender(SendFn fn) { send_ = std::move(fn); }

  bool synced() const { return synced_; }
  bool restored() const { return restored_; }

  // Reprend le dernier état sauvegardé (au démarrage). Les commandes sont alors
  // acceptées immédiatement ; la clim renvoie son état réel dès la première commande
  // (ou au prochain changement), ce qui corrige un éventuel écart.
  bool restore(uint64_t saved) {
    AcState s;
    if (synced_ || !unpack(saved, s))
      return false;
    state_ = s;
    synced_ = true;
    restored_ = true;
    ESP_LOGI(TAG, "État restauré depuis la flash (en attente de confirmation par la clim)");
    publish_state_(false);
    return true;
  }
  const AcState &state() const { return state_; }

  // --- Réception ---------------------------------------------------------
  // Traite une trame brute. Retourne true si elle a été jugée valide.
  bool on_rx_frame(const std::vector<uint8_t> &frame, uint32_t now) {
    std::string hex = to_hex(frame.data(), frame.size());
    size_t n = frame.size();
    bool header_ok = n > RX_TYPE_BYTE && frame[0] == HDR0 && frame[1] == HDR1 && frame[RX_LEN_BYTE] == n;
    bool crc_ok = rx_crc_ok(frame.data(), n);
    if (!header_ok || (check_rx_crc && !crc_ok)) {
      // Fréquent si la liaison est bruitée (soudure, fils) : ignoré silencieusement.
      ESP_LOGD(TAG, "RX invalide ignorée (%s) : %s", header_ok ? "CRC" : "en-tête/longueur", hex.c_str());
      return false;
    }
    // Toute trame valide prouve que le lien série fonctionne.
    last_rx_ms_ = now;
    have_rx_ = true;

    uint8_t type = frame[RX_TYPE_BYTE];
    if (type == RX_TYPE_HEARTBEAT) {
      ESP_LOGV(TAG, "RX battement : %s", hex.c_str());
      return false;
    }
    if (type != RX_TYPE_STATE || n < RX_MIN_LEN) {
      ESP_LOGD(TAG, "RX type 0x%02X (%u o) non décodé : %s", type, (unsigned) n, hex.c_str());
      return false;
    }
    ESP_LOGI(TAG, "RX état : %s", hex.c_str());
    publish_value_("last_frame", hex, true, false);  // dernière trame d'état, retenue (diagnostic)

    AcState rx;
    decode(frame.data(), n, state_, rx);

    // Fenêtre post-commande : on ignore les trames qui contredisent la commande.
    if (pending_fields_ != 0) {
      bool in_window = (now - last_cmd_ms_) < ignore_window_ms;
      uint16_t contra = diff_fields(rx, state_) & pending_fields_;
      if (in_window && contra != 0) {
        ESP_LOGD(TAG, "RX contradictoire ignorée (champs 0x%03X, commande il y a %u ms)", contra,
                 (unsigned) (now - last_cmd_ms_));
        state_.room_temp = rx.room_temp;
        publish_state_(false);
        return true;
      }
      if (contra != 0)
        ESP_LOGW(TAG, "La clim n'a pas confirmé la commande (champs 0x%03X) : état de la clim retenu", contra);
      pending_fields_ = 0;
    }

    rx.mute = state_.mute;  // le volume n'est pas renvoyé par la clim
    if (!synced_ || restored_) {
      ESP_LOGI(TAG, "Trame d'état reçue : synchronisé avec la clim");
      synced_ = true;
      restored_ = false;
    }
    state_ = rx;
    publish_state_(false);
    return true;
  }

  // --- Commandes ---------------------------------------------------------
  // `key` = suffixe après set/. Retourne true si la commande est acceptée.
  bool on_set(const std::string &key, const std::string &raw, uint32_t now) {
    if (key == "resync") {
      ESP_LOGI(TAG, "Resync demandé : republication complète");
      publish_all();
      return true;
    }
    AcState s = state_;
    std::string v = trim(raw);
    std::string lv = to_lower(v);
    bool ok = false;
    uint16_t field = 0;
    bool b;
    uint8_t code;

    if (key == "power") {
      ok = parse_bool(v, b);
      if (ok)
        s.power = b;
      field = F_POWER;
    } else if (key == "mode") {
      ok = name_to_code(MODES, lv, code);
      if (ok)
        s.mode = code;
      field = F_MODE;
    } else if (key == "target_temperature") {
      char *end = nullptr;
      float f = strtof(v.c_str(), &end);
      ok = !v.empty() && end != nullptr && *end == '\0' && f >= 15.5f && f < 31.5f;
      if (ok)
        s.target = static_cast<uint8_t>(f + 0.5f);
      field = F_TARGET;
    } else if (key == "fan") {
      ok = name_to_code(FANS, lv, code);
      if (ok)
        s.fan = code;
      field = F_FAN;
    } else if (key == "swing_v") {
      ok = name_to_code(SWINGS_V, lv, code);
      if (ok)
        s.swing_v = code;
      field = F_SWING_V;
    } else if (key == "swing_h") {
      ok = parse_bool(v, b);
      if (ok)
        s.swing_h = b;
      field = F_SWING_H;
    } else if (key == "display") {
      ok = parse_bool(v, b);
      if (ok)
        s.display = b;
      field = F_DISPLAY;
    } else if (key == "mute") {
      ok = parse_bool(v, b);
      if (ok)
        s.mute = b;
      field = F_MUTE;
    } else if (key == "clean") {
      ok = parse_bool(v, b);
      if (ok)
        s.clean = b;
      field = F_CLEAN;
    } else {
      ESP_LOGW(TAG, "set/%s : clé inconnue (ignorée)", key.c_str());
      return false;
    }

    if (!ok) {
      ESP_LOGW(TAG, "set/%s : valeur invalide '%s' (ignorée)", key.c_str(), v.c_str());
      return false;
    }
    if (!synced_) {
      ESP_LOGW(TAG, "set/%s ignoré : aucune trame reçue de la clim (état inconnu)", key.c_str());
      return false;
    }

    ESP_LOGI(TAG, "set/%s = %s", key.c_str(), v.c_str());
    state_ = s;
    // Le volume n'est pas renvoyé par la clim : inutile de bloquer les RX pour lui.
    if (field != F_MUTE)
      pending_fields_ |= field;
    last_cmd_ms_ = now;
    tx_due_ = true;
    tx_due_ms_ = now + tx_debounce_ms;
    publish_state_(false);  // optimiste
    return true;
  }

  // À appeler régulièrement : envoie la trame en attente (après anti-rebond).
  void loop(uint32_t now) {
    if (tx_due_ && static_cast<int32_t>(now - tx_due_ms_) >= 0) {
      tx_due_ = false;
      last_cmd_ms_ = now;  // la fenêtre démarre à l'émission réelle
      std::vector<uint8_t> f = encode(state_);
      ESP_LOGI(TAG, "TX : %s", to_hex(f.data(), f.size()).c_str());
      if (send_)
        send_(f);
    }
  }

  // --- Diagnostic --------------------------------------------------------
  bool unit_link(uint32_t now) const { return have_rx_ && (now - last_rx_ms_) < link_timeout_ms; }

  void publish_link(uint32_t now) { publish_value_("unit_link", bool_str(unit_link(now)), true, false); }

  void publish_diag(int rssi, uint32_t uptime_s, uint32_t now) {
    publish_value_("rssi", std::to_string(rssi), true, false);
    publish_value_("uptime", std::to_string(uptime_s), true, true);
    publish_link(now);
  }

  // Republie tout (connexion MQTT, set/resync).
  void publish_all() {
    cache_.clear();
    publish_state_(true);
    for (auto &kv : extra_)
      publish_value_(kv.first, kv.second, true, true);
  }

 private:
  void publish_state_(bool force) {
    if (!synced_)
      return;  // jamais d'état fictif publié en retained
    const AcState &s = state_;
    publish_value_("power", bool_str(s.power), true, force);
    publish_value_("mode", code_to_name(MODES, s.mode), true, force);
    publish_value_("target_temperature", std::to_string(s.target), true, force);
    publish_value_("fan", code_to_name(FANS, s.fan), true, force);
    publish_value_("swing_v", code_to_name(SWINGS_V, s.swing_v), true, force);
    publish_value_("swing_h", bool_str(s.swing_h), true, force);
    publish_value_("display", bool_str(s.display), true, force);
    publish_value_("mute", bool_str(s.mute), true, force);
    publish_value_("clean", bool_str(s.clean), true, force);
    publish_value_("room_temperature", std::to_string(s.room_temp), true, force);
  }

  void publish_value_(const std::string &key, const std::string &value, bool retain, bool force) {
    if (retain && key != "power" && key != "mode" && key != "target_temperature" && key != "fan" &&
        key != "swing_v" && key != "swing_h" && key != "display" && key != "mute" && key != "clean" &&
        key != "room_temperature")
      extra_[key] = value;  // diag (rssi, uptime, unit_link) : rejoué par publish_all
    auto it = cache_.find(key);
    if (!force && it != cache_.end() && it->second == value)
      return;
    if (publish_ && publish_(key, value, retain))
      cache_[key] = value;  // mis en cache seulement si réellement publié
  }

  AcState state_;
  bool synced_ = false;
  bool restored_ = false;
  bool have_rx_ = false;
  uint32_t last_rx_ms_ = 0;
  uint32_t last_cmd_ms_ = 0;
  uint16_t pending_fields_ = 0;
  bool tx_due_ = false;
  uint32_t tx_due_ms_ = 0;
  std::map<std::string, std::string> cache_;
  std::map<std::string, std::string> extra_;
  PublishFn publish_;
  SendFn send_;
};

// Instances uniques utilisées par la colle YAML.
inline Gateway &gateway() {
  static Gateway g;
  return g;
}
inline FrameSplitter &splitter() {
  static FrameSplitter s;
  return s;
}

}  // namespace espac
