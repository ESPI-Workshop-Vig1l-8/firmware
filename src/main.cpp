/**
 * ============================================================================
 *  AETHERCORP INDUSTRIAL SOLUTIONS // VIG1L-8 TACTICAL EDGE FIRMWARE
 *  Microcontrôleur : ESP32 (WROOM-32)
 *  Workshop EPSI M1 2026 - Consortium VIG1L-8
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ============================================================================
// 1. CONFIGURATION RÉSEAU & MQTT
// ============================================================================
const char* WIFI_SSID     = "VIGIL8_HOTSPOT";    // SSID du point d'accès Wi-Fi de table
const char* WIFI_PASSWORD = "AetherCorp2050!";   // Mot de passe Wi-Fi
const char* MQTT_SERVER   = "192.168.10.1";      // IP du PC Serveur Local
const int   MQTT_PORT     = 1883;                // Port MQTT (ou 8883 avec MQTTS/TLS)
const char* DEVICE_ID     = "VIG1L-8-NODE04";

// Configuration IP Statique de table (Sous-réseau 192.168.10.0/24)
IPAddress local_IP(192, 168, 10, 20);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

// Topics MQTT Standardisés
const char* TOPIC_TELEMETRY = "vigil8/sensors/telemetry";
const char* TOPIC_COMMANDS  = "vigil8/commands/actuators";

// ============================================================================
// 2. ASSIGNATION DES BROCHES MATÉRIELLES (PINOUT ESP32)
// ============================================================================
#define PIN_DHT        4    // DHT22 Data (Digital)
#define PIN_MQ2        34   // MQ-2 Gaz Analog (ADC1_CH6 - 3.3V Max)
#define PIN_PIR        14   // HC-SR501 Détecteur Présence (Digital In)
#define PIN_BUZZER     18   // Buzzer Piézoélectrique (PWM / Tone)
#define PIN_LED        19   // LED Rouge d'Alerte (Digital Out)
#define PIN_I2C_SDA    21   // OLED SDA
#define PIN_I2C_SCL    22   // OLED SCL

// Configuration Capteurs & Écran
#define DHTTYPE DHT22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

DHT dht(PIN_DHT, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================================
// 3. VARIABLES D'ÉTAT & CADENCEMENT
// ============================================================================
unsigned long lastTelemetryTime = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 2000; // Envoi toutes les 2 secondes

// État des capteurs
float currentTemp = 24.0;
float currentHum  = 45.0;
int   currentGas  = 18;
bool  currentPir  = false;

// État des actionneurs
bool buzzerActive = false;
bool strobeActive = false;
unsigned long buzzerAutoStopTime = 0;

// ============================================================================
// 4. GESTION DE L'ÉCRAN OLED 0.96" (I2C)
// ============================================================================
void updateOLED(const char* statusMsg) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // En-tête Tactique
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("VIG1L-8 // EDGE NODE");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Ligne 1 : IP & Wi-Fi
  display.setCursor(0, 13);
  display.print("IP: ");
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP().toString());
  } else {
    display.print("CONNEXION...");
  }

  // Ligne 2 : Télémétrie Température & Humidité
  display.setCursor(0, 25);
  display.print("T: ");
  display.print(currentTemp, 1);
  display.print("C  H: ");
  display.print(currentHum, 0);
  display.print("%");

  // Ligne 3 : Gaz MQ-2 & PIR
  display.setCursor(0, 37);
  display.print("GAZ: ");
  display.print(currentGas);
  display.print("ppm");
  if (currentGas > 60) {
    display.print(" [ALERTE]");
  }

  // Ligne 4 : Mouvement & Statut
  display.setCursor(0, 49);
  display.print("PIR: ");
  display.print(currentPir ? "INTRUS !" : "NORMAL");

  // Alerte visuelle inversée sur OLED si alarme active
  if (buzzerActive || currentGas > 60) {
    display.fillRect(0, 58, 128, 6, SSD1306_WHITE);
  }

  display.display();
}

// ============================================================================
// 5. CALLBACK MQTT (RÉCEPTION DES COMMANDES DEPUIS LE SERVEUR GO)
// ============================================================================
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Commande reçue sur topic : ");
  Serial.println(topic);

  // Parser le JSON de commande
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  if (error) {
    Serial.print("[MQTT] Erreur parsing JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Exemple de commande reçue : {"buzzer": true, "strobe": true, "duration_seconds": 5}
  if (doc.containsKey("buzzer")) {
    buzzerActive = doc["buzzer"].as<bool>();
    if (buzzerActive) {
      digitalWrite(PIN_BUZZER, HIGH);
      int duration = doc["duration_seconds"] | 5;
      buzzerAutoStopTime = millis() + (duration * 1000);
      Serial.printf("[ACTIONNEUR] Buzzer ACTIVÉ pour %d secondes\n", duration);
    } else {
      digitalWrite(PIN_BUZZER, LOW);
      buzzerAutoStopTime = 0;
      Serial.println("[ACTIONNEUR] Buzzer COUPÉ");
    }
  }

  if (doc.containsKey("strobe")) {
    strobeActive = doc["strobe"].as<bool>();
    digitalWrite(PIN_LED, strobeActive ? HIGH : LOW);
  }

  updateOLED(buzzerActive ? "ALARME ACTIVE" : "CMD REÇUE");
}

// ============================================================================
// 6. GESTION DES CONNEXIONS WI-FI & MQTT
// ============================================================================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WIFI] Connexion à : ");
  Serial.println(WIFI_SSID);

  // Configuration de l'IP statique de table
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connecté avec succès !");
    Serial.print("[WIFI] Adresse IP : ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] Échec Wi-Fi (Mode hors-ligne temporaire)");
  }
}

void connectMQTT() {
  while (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print("[MQTT] Connexion au broker Mosquitto (");
    Serial.print(MQTT_SERVER);
    Serial.print(")... ");

    if (mqttClient.connect(DEVICE_ID)) {
      Serial.println("CONNECTÉ !");
      // S'abonner aux commandes d'actionneurs
      mqttClient.subscribe(TOPIC_COMMANDS);
      Serial.print("[MQTT] Abonné à : ");
      Serial.println(TOPIC_COMMANDS);
    } else {
      Serial.print("Échec rc=");
      Serial.print(mqttClient.state());
      Serial.println(" -> Nouvelle tentative dans 3s");
      delay(3000);
    }
  }
}

// ============================================================================
// 7. LECTURE DES CAPTEURS & PUBLICATION MQTT
// ============================================================================
void readSensorsAndPublish() {
  // 1. Lecture DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) currentTemp = t;
  if (!isnan(h)) currentHum = h;

  // 2. Lecture MQ-2 (Valeur brute ADC 12 bits 0-4095 calibrée en ppm estimé)
  int rawADC = analogRead(PIN_MQ2);
  currentGas = map(rawADC, 0, 4095, 10, 300); // Échelle calibrée

  // 3. Lecture PIR HC-SR501
  currentPir = (digitalRead(PIN_PIR) == HIGH);

  // 4. Mise à jour de l'affichage OLED local
  updateOLED(currentPir ? "INTRUS DÉTECTÉ" : "STATUT NOMINAL");

  // 5. Structuration de la trame JSON pour Mosquitto
  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = (uint32_t)(millis() / 1000);
  doc["temp"]      = serialized(String(currentTemp, 1));
  doc["hum"]       = serialized(String(currentHum, 1));
  doc["gas"]       = currentGas;
  doc["motion"]    = currentPir;

  char buffer[256];
  size_t n = serializeJson(doc, buffer);

  // 6. Publication sur le topic MQTT
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_TELEMETRY, buffer, n);
    Serial.print("[MQTT PUB] ");
    Serial.println(buffer);
  } else {
    Serial.println("[MQTT] Non connecté - payload ignoré");
  }
}

// ============================================================================
// 8. SETUP & LOOP PRINCIPALE
// ============================================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- VIG1L-8 ESP32 INITIALISATION ---");

  // Configuration des broches
  pinMode(PIN_DHT, INPUT);
  pinMode(PIN_MQ2, INPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  // Initialisation I2C et OLED
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[OLED] Erreur : Écran 0x3C introuvable sur le bus I2C !");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 25);
    display.println("VIG1L-8 BOOTING...");
    display.display();
  }

  // Initialisation DHT22
  dht.begin();

  // Configuration Client MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);

  // Connexions
  connectWiFi();
  connectMQTT();
}

void loop() {
  // Maintenir Wi-Fi et MQTT
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqttClient.connected()) {
    connectMQTT();
  }
  mqttClient.loop();

  // Gestion de l'extinction automatique du buzzer après durée
  if (buzzerActive && buzzerAutoStopTime > 0 && millis() > buzzerAutoStopTime) {
    buzzerActive = false;
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED, LOW);
    Serial.println("[ACTIONNEUR] Buzzer auto-coupé (timeout)");
  }

  // Lecture et publication cadencée (toutes les 2s)
  unsigned long now = millis();
  if (now - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = now;
    readSensorsAndPublish();
  }
}
