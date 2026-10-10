# Carnet des installations

Notes de terrain pour chaque module ESPAC posé.

## Câblage de référence (ESP32 DevKit 30 broches, USB-C)

Couleurs **de notre câble** (elles diffèrent de celles du projet d'origine) :

| Fil (notre câble) | Broche ESP32 | Rôle |
|---|---|---|
| rouge | **VIN** | 5V fourni par la clim |
| brun | **GND** | masse |
| bleu | **D16** (GPIO16, parfois marquée RX2) | TX de l'ESP → clim |
| blanc | **D17** (GPIO17, parfois marquée TX2) | RX de l'ESP ← clim |

⚠️ Ne pas utiliser RX0/TX0 (port USB de la carte). Ne jamais brancher l'USB et la clim en même temps.

## Modules

### espac-salon — clim du haut (salon)
- Posé le 10/10/2026. Carte ESP32-WROOM-32D, MAC `84:1F:E8:17:3C:C0`, IP 192.168.30.206.
- Câblage ci-dessus. Une soudure un peu fragile : quelques trames bruitées, rejetées par le CRC (sans impact).
- Validé : lecture de l'état, consigne, mode, ventilation (silence → méga-turbo) depuis Jeedom.

### Clim du bas — à faire
- Même câblage : rouge → VIN, bleu → D16, blanc → D17, brun → GND.
- Firmware : changer `device_name` (ex. `espac-bas`) et `friendly_name` dans `firmware/espac.yaml`
  (ou via `--substitution`), premier flash en USB **hors clim**, puis OTA.
- Jeedom crée l'équipement automatiquement à la première connexion MQTT.
