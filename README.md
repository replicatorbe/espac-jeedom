# espac-jeedom

Firmware ESP32 (ESPHome) qui remplace le module wifi Airton ACW02 et pilote les
climatiseurs Airton par leur port série, en **passerelle MQTT pour Jeedom**.

Portage vers Jeedom du projet [ESPAC](https://github.com/git-greatguy/espac) de dOHMotik
(édition Home Assistant). Le code est réécrit : seuls les faits du protocole série
(offsets, bits, CRC) sont repris.

Merci à dOHMotik pour ESPAC et pour avoir publié ses sources sous GPL-3.0 :
👉 https://github.com/git-greatguy/espac

- Contrat MQTT (source de vérité) : [docs/CONTRAT-MQTT.md](docs/CONTRAT-MQTT.md)
- Firmware, câblage et flash : [firmware/README.md](firmware/README.md)
- Plugin Jeedom associé : [jeedom-plugin-espacbe](https://github.com/replicatorbe/jeedom-plugin-espacbe)

## Licence

GPL-3.0-only — voir [LICENSE](LICENSE) et [NOTICE](NOTICE).
Projet dérivé d'[ESPAC](https://github.com/git-greatguy/espac)
(© 2025-2026 git-greatguy / Dohmotik, GPL-3.0-only).
