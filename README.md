# espac-jeedom

Firmware ESP32 (ESPHome) qui remplace le module wifi Airton ACW02 et pilote les
climatiseurs Airton par leur port série, en **passerelle MQTT pour Jeedom**.

Portage vers Jeedom du projet [ESPAC](https://github.com/git-greatguy/espac) de dOHMotik
(édition Home Assistant). Le code est réécrit : seuls les faits du protocole série
(offsets, bits, CRC) sont repris.

> Dépôt privé en attente d'une licence de l'auteur original.

- Contrat MQTT (source de vérité) : [docs/CONTRAT-MQTT.md](docs/CONTRAT-MQTT.md)
- Firmware, câblage et flash : [firmware/README.md](firmware/README.md)
