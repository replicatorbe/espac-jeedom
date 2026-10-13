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

### espac-rdc — clim du rez-de-chaussée
- Flashée le 13/10/2026 en USB, hors clim. Carte ESP32-WROOM-32D, MAC `84:1F:E8:1B:3C:54`,
  IP 192.168.30.83, firmware 0.6.0.
- Même firmware que le salon, nom changé à la compilation :
  `esphome -s device_name espac-rdc -s friendly_name "ESPAC RDC" run espac.yaml`
  (mises à jour OTA : ajouter `--device 192.168.30.83`).
- Même câblage : rouge → VIN, bleu → D16, blanc → D17, brun → GND.
- Jeedom a créé l'équipement `espac-rdc` tout seul à la première connexion MQTT (à ranger
  dans son objet).
- Posée sur la clim le 13/10/2026. Juste après le câblage, quelques trames abîmées
  (branchement clim sous tension) ; ensuite qualité de liaison à 100 %.
- Validé : liaison clim, consigne (confirmée par la clim), mise en marche depuis Jeedom
  (froid 22 °C, la clim démarre). Wi-Fi -49 dBm.
