# Firmware // Nœud VIG1L-8 Sentinel-X (ESP32)

Firmware C++ (PlatformIO, framework Arduino) pour l'ESP32 du boîtier Sentinel-X : lecture des capteurs, envoi des mesures en JSON au broker MQTT du PC Serveur Local via **MQTTS (TLS)**, LED de mouvement et LED d'avertissement environnement.

| Environnement PlatformIO | Fichier | Rôle |
|---|---|---|
| `esp32dev` (par défaut) | `src/main.cpp` | Firmware Sentinel-X |
| `diagnostic` | `src/diagnostic.cpp` | Test du câblage sans réseau : `pio run -e diagnostic -t upload` |

---

## 1. Câblage

| Composant | VCC | GND | Signal |
| :--- | :--- | :--- | :--- |
| **PIR HC-SR501** | VIN (5V) | GND | OUT → **GPIO 14** |
| **MQ-2** | VIN (5V) | GND | AO → 10 kΩ → **GPIO 34**, et 20 kΩ de GPIO 34 vers GND (pont diviseur : AO peut monter à 5V) |
| **DHT22** | 3V3 | GND | DATA → **GPIO 4** (+ pull-up 10 kΩ vers 3V3 si capteur nu) |
| **LED mouvement** | — | cathode → GND | **GPIO 19** → 220 Ω → anode |
| **LED environnement** | — | cathode → GND | **GPIO 18** → 220 Ω → anode |

GND commun à tous les composants. Réglages du HC-SR501 : cavalier sur **H**, potentiomètre Tx au minimum.

| LED | Comportement |
|---|---|
| Mouvement (GPIO 19) + LED intégrée | allumées pendant une détection du PIR |
| Environnement (GPIO 18), **fixe** | premier avertissement local : température > 40 °C ou gaz > 800 mV (plafonds fixes avec hystérésis, réglables dans `config.h` ; gaz ignoré pendant le préchauffage) |
| Environnement (GPIO 18), **clignotante** | alerte envoyée par le serveur (commande `strobe`) : c'est l'IA qui décide des anomalies |

---

## 2. Configuration

1. `include/config.h` (versionné) : identifiant du nœud, IP du serveur et port, IP statique, broches, intervalles.
2. `include/secrets.h` (**non versionné**) : copier `include/secrets.example.h` vers `include/secrets.h`, puis renseigner :
   - `WIFI_SSID` / `WIFI_PASSWORD` : hotspot de table ;
   - `MQTT_PASSWORD` : mot de passe du compte `DEVICE_ID` (entrée `MQTT_DEVICE_ACCOUNTS` du `.env` de l'infra) ;
   - `MQTT_CA_CERT` : contenu de `infra/certs/ca/ca.crt` (généré par `infra/scripts/gen-certs.sh`).

`DEVICE_ID` est aussi le nom d'utilisateur MQTT : l'ACL du broker n'autorise un nœud qu'à publier sur ses propres topics.

---

## 3. Données envoyées

| Topic | Quand | Contenu |
|---|---|---|
| `vigil8/<DEVICE_ID>/telemetry` | toutes les 2 s | mesures (voir ci-dessous) |
| `vigil8/<DEVICE_ID>/event` | changement d'état du PIR | `{"v":1,"device_id":"…","seq":12,"uptime_ms":36685120,"type":"motion","state":true}` |
| `vigil8/<DEVICE_ID>/status` | connexion (retenu) | `{"online":true,"fw":"0.2.0","ip":"192.168.10.20"}`, et `{"online":false}` publié par le broker si le nœud disparaît |
| `vigil8/<DEVICE_ID>/cmd` | reçu | `{"strobe":true,"duration_s":8}` : LED environnement clignotante (durée 1 à 60 s, 5 s par défaut) |

Télémétrie :

```json
{
  "v": 1,
  "device_id": "VIG1L-8-NODE04",
  "seq": 18342,
  "uptime_ms": 36684012,
  "temp_c": 25.8,
  "hum_pct": 61.5,
  "gas_mv": 259,
  "pir": false,
  "pir_events": 0,
  "status": { "dht": "ok", "gas_warm": true, "env_warn": false, "rssi": -58 }
}
```

- `status.env_warn` : plafond local gaz/température dépassé (LED environnement fixe). Simple indication : la détection d'anomalies est faite par l'IA.
- `temp_c` / `hum_pct` valent `null` et `status.dht` vaut `"error"` si le DHT22 ne répond pas (pas de « dernière valeur connue »).
- `gas_mv` : tension de la sortie AO du MQ-2 en mV (non étalonnée, ce ne sont pas des ppm). `status.gas_warm` est `false` pendant les 3 premières minutes.
- `seq` : un compteur par topic. Un trou = message perdu ; un retour à 0 avec un petit `uptime_ms` = redémarrage.
- `pir_events` : nombre de détections depuis le message précédent (un événement perdu reste compté).

Le format complet (stockage CouchDB, annotations) est décrit dans le README du dépôt `infra`.

---

## 4. Moniteur série (115200 bauds)

```
[MQTT] Connexion TLS à 192.168.10.1:18883 (IP locale 192.168.10.20)...
[MQTT] Connecté
[PUB] vigil8/VIG1L-8-NODE04/telemetry {"v":1,"device_id":"VIG1L-8-NODE04","seq":0,...}
```

En cas d'échec de connexion MQTT, `state=-2` indique un problème réseau ou TLS (IP, port, certificat) ; `state=4` ou `5`, un problème d'identifiants.
