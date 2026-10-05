# Firmware C++ // VIG1L-8 Tactical Edge Node (ESP32)

Micrologiciel officiel pour la carte microcontrôleur **ESP32** (NodeMCU-32S / ESP32-WROOM-32).

---

## 1. Schéma de Câblage des Broches (Pinout)

| Périphérique | Broche Composant | Broche ESP32 | Description |
| :--- | :--- | :--- | :--- |
| **Capteur DHT22** | DATA | **GPIO 4** | Température & Humidité (Bus 1-Wire) |
| **Capteur MQ-2** | A0 (Analog) | **GPIO 34** | Gaz combustible (Entrée ADC1 12 bits) |
| **Capteur HC-SR501** | OUT | **GPIO 14** | Détection présence infrarouge (PIR) |
| **Écran OLED 0.96"** | SDA | **GPIO 21** | Données I2C |
| **Écran OLED 0.96"** | SCL | **GPIO 22** | Horloge I2C |
| **Buzzer Piézo** | (+) Signal | **GPIO 18** | Alarme acoustique |
| **LED Rouge Alerte** | Anode (+) | **GPIO 19** | Témoin lumineux (avec résistance 220Ω) |
| **Alimentations** | VCC | **VIN (5V) / 3V3** | 5V pour MQ-2 & PIR, 3.3V pour DHT22 & OLED |
| **Masse** | GND | **GND** | Masse commune |

---

## 2. Bibliothèques Requises

Si vous utilisez **Arduino IDE** (Outils -> Gérer les bibliothèques) :
1. `PubSubClient` by Nick O'Leary
2. `ArduinoJson` by Benoît Blanchon (version 6 ou 7)
3. `Adafruit SSD1306` & `Adafruit GFX Library`
4. `DHT sensor library` by Adafruit & `Adafruit Unified Sensor`

Si vous utilisez **VS Code + PlatformIO** :  
Toutes les dépendances s'installent automatiquement grâce au fichier `platformio.ini`.

---

## 3. Configuration Réseau

Dans le code (`main.cpp` ou `firmware.ino`) :
* `WIFI_SSID` : Nom du hotspot Wi-Fi de table (ex: `VIGIL8_HOTSPOT`)
* `WIFI_PASSWORD` : Mot de passe du Wi-Fi
* `MQTT_SERVER` : Adresse IP du PC Serveur Local (`192.168.10.1`)
* `local_IP` : `192.168.10.20` (IP statique de l'ESP32)

---

## 4. Topics MQTT

* **Émission (Télémétrie toutes les 2s) :** `vigil8/sensors/telemetry`
* **Réception (Commandes actionneurs) :** `vigil8/commands/actuators`
