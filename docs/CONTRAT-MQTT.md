# Contrat MQTT ESPAC ↔ Jeedom (v1)

Source de vérité partagée entre le firmware ESP32 (`firmware/`) et le plugin Jeedom `espacbe`.
L'ESP32 est une **passerelle** : il reflète l'état réel de la clim Airton et exécute les commandes.
Aucune régulation sur l'ESP (elle se fait dans Jeedom).

Broker : Mosquitto sur la box Jeedom `192.168.0.125:1883` (anonyme pour l'instant).

## Topics

`<dev>` = nom ESPHome du module (ex. `espac-salon`), `[a-z0-9-]`.

| Topic | Sens | Retain | Payload |
|---|---|---|---|
| `espac/<dev>/status` | ESP → | oui | `online` / `offline` (LWT) |
| `espac/<dev>/info` | ESP → | oui | JSON `{"name","mac","ip","version","fw":"espac-jeedom"}` publié à chaque connexion (`version` = version du firmware espac-jeedom) |
| `espac/<dev>/state/<clé>` | ESP → | oui | valeur (voir tableau) |
| `espac/<dev>/set/<clé>` | → ESP | non | valeur (voir tableau) |
| `espac/<dev>/set/resync` | → ESP | non | n'importe quoi : republie tous les états |

Découverte côté Jeedom : abonnement à `espac/+/status` et `espac/+/info`.

## Clés

| Clé | state | set | Valeurs |
|---|---|---|---|
| `power` | ✔ | ✔ | `ON` / `OFF` |
| `mode` | ✔ | ✔ | `auto` `cool` `dry` `fan_only` `heat` |
| `target_temperature` | ✔ | ✔ | entier 16–31 (°C) |
| `fan` | ✔ | ✔ | `auto` `quiet` `very_low` `low` `medium` `high` `very_high` `turbo` `humidity` |
| `swing_v` | ✔ | ✔ | `off` `auto` `very_high` `high` `medium` `low` `very_low` |
| `swing_h` | ✔ | ✔ | `ON` / `OFF` |
| `display` | ✔ | ✔ | `ON` / `OFF` |
| `mute` | ✔ | ✔ | `ON` / `OFF` (bip de la clim). Non renvoyé par la clim : l'état publié est la dernière commande (défaut `ON` au démarrage) |
| `clean` | ✔ | ✔ | `ON` / `OFF` (nettoyage / ioniseur) |
| `room_temperature` | ✔ | ✘ | entier °C (sonde interne de la clim, octet RX `[10]`) |
| `unit_link` | ✔ | ✘ | `ON` si une trame valide de la clim a été reçue < 60 s, sinon `OFF` |
| `last_frame` | ✔ | ✘ | hex de la dernière trame RX (diagnostic, non retenu) |
| `rssi` | ✔ | ✘ | dBm |
| `uptime` | ✔ | ✘ | secondes |

Valeurs invalides en `set` : ignorées (log WARN côté ESP). Après un `set` valide l'ESP
envoie la trame à la clim puis republie immédiatement le `state` correspondant (optimiste),
corrigé ensuite par la trame suivante de la clim.

## Protocole série Airton (rappel, UART 9600 8N1)

Trame TX (24 o) : `7A 7A 21 D5 18 00 00 A1 00 00 00 00` + `[b12] [b13] [b14] [b15] [b16]` + `00×5` + CRC16 (ESPHome `crc16()`, MSB d'abord).

- b12 = power(bit3) | mode(bits0-2 : 0 auto,1 cool,2 dry,3 fan_only,4 heat) | fan(quartet haut : 0 auto,1 very_low,2 low,3 medium,4 high,5 very_high,6 humidity,13 turbo)
- b13 = consigne − 16
- b14 = swing_h(quartet haut, 1=ON) | swing_v(quartet bas, 0..6 dans l'ordre ci-dessus)
- b15 = drapeaux. Valeurs émises : aucun `0x04`, clean `0x44`, display `0x88`, display+clean `0xC8`
  (= `display ? 0x88 : 0x04` | `clean ? 0x40 : 0`). Les bits 0,1,4,5 (inconnus) reçus de la clim sont recopiés.
  En RX : display = bit 3, clean = bit 6 (l'original normalisait 0/128 → aucun, 140 → display, 204 → les deux).
- b16 = volume (1 = mute)

Trame RX (≥ 34 o) : `[10]` temp. pièce, `[13]` = b12, `[14]` = b13, `[15]` = b14, `[16]` = b15.
Le volume (b16) n'a pas d'équivalent identifié en RX. Le CRC des trames RX n'est pas encore confirmé
(hypothèse : même CRC sur tout sauf les 2 derniers octets ; vérification désactivée par défaut).

Notes d'implémentation :
- `power=OFF` : l'original envoie b12 = `0x05` (mode « Off », bit3 à 0) en conservant le quartet ventilation.
  Côté ESP on mémorise le dernier mode pour le restaurer à `power=ON`. En RX, bit3 à 0 ⇒ `power=OFF`
  (et si bits0-2 = 5, on ne change pas `mode`).
- L'ESP **n'envoie aucune trame avant d'avoir reçu au moins une trame RX valide** (sinon b13/b15 inconnus).
  Les `set` reçus avant cette première trame sont ignorés (WARN) et aucun `state` climatisation n'est publié.
- Plusieurs `set` reçus à moins de 150 ms d'intervalle sont regroupés en une seule trame.
- Pendant ~3 s après une commande, les trames RX qui contredisent la commande sont ignorées (latence clim).

## Observations sur la vraie clim (10/10/2026)

Structure commune des trames RX : `7A 7A [émetteur] 21 [longueur totale] 00 00 [type] … [CRC16 MSB LSB]`.
Le CRC RX est le même que le CRC TX (CRC-16/MODBUS sur toute la trame sauf les 2 derniers octets) : **confirmé**.

| Type `[7]` | Longueur | Émetteur `[2]` | Rôle |
|---|---|---|---|
| `A4` | 13 | `D1` | battement de cœur, toutes les 10 s (compte pour `unit_link`) |
| `A3` | 34 | `D5` | état complet, envoyé à chaque changement (télécommande ou commande) |
| `A6` | 28 | `D5` | complément suivant chaque état (octets nuls observés) |

Octet consigne `[14]` : bits 0-4 = consigne − 16 ; bit 6 (`0x40`) = ventilation **silence**
(avec quartet ventilation = 0) ; bits 5 et 7 inconnus, recopiés tels quels en TX.

Cycle de la touche ventilation de la télécommande (confirmé) :
`very_high`(5) → `turbo`(13, « méga-turbo ») → `humidity`(6, rôle exact inconnu) → `auto`(0)
→ `quiet`(0 + bit 0x40) → `very_low`(1) → `low`(2) → `medium`(3) → `high`(4) → `very_high`(5). `[21]` reflète le mode (0 auto, 1 cool, 2 dry, 3 fan_only, 4 heat).
La clim n'envoie **pas** d'état périodique : uniquement à la mise sous tension et à chaque changement.
