# Firmware espac-jeedom (ESP32 / ESPHome)

Passerelle entre un climatiseur Airton (UART 9600 8N1 sur le port wifi de l'unité intérieure)
et Jeedom (MQTT). Aucune régulation sur l'ESP : il reflète l'état réel de la clim et exécute
les commandes. Topics et valeurs : [../docs/CONTRAT-MQTT.md](../docs/CONTRAT-MQTT.md).

| Fichier | Rôle |
|---|---|
| `espac.yaml` | Configuration ESPHome (wifi, MQTT, UART, LED, colle avec le C++) |
| `espac_airton.h` | Protocole Airton (encodage/décodage, CRC), découpage des trames, logique passerelle |
| `secrets.example.yaml` | Modèle des secrets → copier en `secrets.yaml` (jamais commité) |
| `tests/` | Tests hôte du protocole (`./tests/run_tests.sh`, clang++) |

## Matériel

- ESP32-WROOM-32D ou 32U, DevKit 30 broches, USB-C (`board: esp32dev`).
- **Variante 32U : l'antenne externe (IPEX/U.FL) est obligatoire.** Sans antenne, le wifi est
  quasi inexistant et l'étage RF peut souffrir.
- Connecteur JST 4 broches, pas de 2,54 mm (côté clim).

## Câblage

Couleurs côté clim d'après le README du projet ESPAC original.

| Broche ESP32 (DevKit 30 broches) | Signal | Fil côté clim |
|---|---|---|
| `VIN` (5V) | alimentation 5 V fournie par la clim | **rouge** |
| `GND` | masse | **violet** |
| `GPIO16` (`D16`, sérigraphié parfois `RX2`) | TX de l'ESP → clim | **bleu** |
| `GPIO17` (`D17`, sérigraphié parfois `TX2`) | RX de l'ESP ← clim | **vert** |

> Attention : sur beaucoup de DevKit, GPIO16/GPIO17 sont sérigraphiés `RX2`/`TX2` (UART2 matériel
> par défaut). On ne tient **pas** compte de cette sérigraphie : le firmware (comme l'original) utilise
> GPIO16 en **émission** et GPIO17 en **réception**. Les broches se changent dans les `substitutions`
> (`uart_tx_pin`, `uart_rx_pin`) si besoin.

### Niveau logique 5 V sur GPIO17

L'ESP32 fonctionne en **3,3 V et ses entrées ne tolèrent pas le 5 V**. Avant de brancher le fil vert
sur GPIO17 :

1. Clim sous tension, ESP **non** branché, mesurer au multimètre la tension entre le fil **vert** et
   le fil **violet** (GND) au repos (ligne série au repos = niveau haut).
2. Si la mesure est ≈ 3,3 V : branchement direct possible.
3. Si la mesure est ≈ 5 V : intercaler un **pont diviseur** :

```
fil vert (TX clim, 5 V) ──[ 1 kΩ ]──┬── GPIO17
                                     │
                                  [ 2 kΩ ]
                                     │
fil violet (GND) ────────────────────┴── GND ESP
```

(5 V × 2k / (1k + 2k) ≈ 3,3 V.) Dans le sens ESP → clim (GPIO16, 3,3 V), la plupart des
entrées 5 V reconnaissent 3,3 V comme niveau haut ; c'est ce que fait l'original.

> Danger secteur : l'unité intérieure est alimentée en 230 V. Couper le disjoncteur avant d'ouvrir
> le capot.

## Configuration

Dans `espac.yaml`, section `substitutions` :

- `device_name` : nom du module **et** des topics (`espac/<device_name>/...`), `[a-z0-9-]`, un par clim
  (ex. `espac-salon`, `espac-chambre`).
- `friendly_name`, `mqtt_broker` (192.168.0.125 par défaut).
- `uart_tx_pin`, `uart_rx_pin`, `led_pin`.
- `rx_gap_ms` (30) : silence inter-octets qui termine une trame RX.
- `rx_check_crc` (`"false"`) : à passer à `"true"` seulement quand le CRC RX aura été confirmé
  (voir les logs `CRC hypothèse OK/KO`).
- `cmd_ignore_ms` (3000) : fenêtre d'ignorance des trames contradictoires après une commande.

Secrets :

```sh
cd firmware
cp secrets.example.yaml secrets.yaml
openssl rand -base64 32      # → api_key
# puis éditer secrets.yaml
```

## Installation d'ESPHome (venv local)

```sh
cd /Users/smug/Documents/05-Dev/espac-jeedom
python3 -m venv .venv
.venv/bin/pip install esphome
```

## Premier flash (USB)

1. Brancher l'ESP32 en USB-C **sans** le relier à la clim.
2. Repérer le port série : `ls /dev/cu.*` (souvent `/dev/cu.usbserial-XXXX` ou
   `/dev/cu.wchusbserial-XXXX` ; pilote CH340/CP210x requis selon la carte).
3. Compiler et flasher :

```sh
cd /Users/smug/Documents/05-Dev/espac-jeedom/firmware
../.venv/bin/esphome run espac.yaml --device /dev/cu.usbserial-XXXX
```

Si le flash ne démarre pas, maintenir le bouton `BOOT` au début de l'écriture.

## Mises à jour suivantes (OTA, wifi)

```sh
cd /Users/smug/Documents/05-Dev/espac-jeedom/firmware
../.venv/bin/esphome run espac.yaml --device espac-salon.local    # ou l'adresse IP
```

Logs à distance : `../.venv/bin/esphome logs espac.yaml --device <ip>`, ou l'interface web
`http://<ip>/` (utilisateur `admin`, mot de passe `web_password`).

Si le wifi est injoignable, l'ESP ouvre le point d'accès `ESPAC <device_name>` (mot de passe
`ap_password`) avec un portail captif pour saisir un autre réseau.

## Test MQTT

```sh
# Tout observer :
mosquitto_sub -h 192.168.0.125 -t 'espac/#' -v

# Commandes :
mosquitto_pub -h 192.168.0.125 -t espac/espac-salon/set/power -m ON
mosquitto_pub -h 192.168.0.125 -t espac/espac-salon/set/mode -m heat
mosquitto_pub -h 192.168.0.125 -t espac/espac-salon/set/target_temperature -m 21
mosquitto_pub -h 192.168.0.125 -t espac/espac-salon/set/fan -m auto
mosquitto_pub -h 192.168.0.125 -t espac/espac-salon/set/resync -m 1
```

Au démarrage on doit voir `status online`, `info {...}`, `state/unit_link OFF` puis, dès la
première trame de la clim, `state/last_frame` et tous les états. **Aucune commande n'est envoyée
à la clim avant cette première trame** (les `set` reçus avant sont ignorés).

## Fonctionnement

- Lecture continue de l'UART (toutes les 10 ms), découpage des trames par silence inter-octets
  et par l'en-tête `7A 7A` s'il est présent ; trames < 34 octets rejetées.
- Chaque trame reçue est publiée en hexadécimal sur `state/last_frame` (non retenu) pour la
  rétro-ingénierie ; le log indique aussi si le CRC hypothétique correspond.
- États publiés en retained, uniquement sur changement ; republication complète à chaque
  (re)connexion MQTT et sur `set/resync`.
- `power=OFF` envoie le code « Off » (0x05) en conservant le dernier mode, restauré à `power=ON`.
- Plusieurs `set` rapprochés (< 150 ms) sont regroupés en une seule trame.
- LED GPIO2 : bref flash à chaque trame émise ou reçue.

## Tests hôte

```sh
./tests/run_tests.sh
```

Vérifie le CRC (algorithme identique à `esphome::crc16`), des trames de référence identiques à
celles de l'original, le turbo (code 13), l'arrêt (0x05), l'aller-retour encode/décode sur toutes
les combinaisons, le découpage des trames et la logique passerelle.
