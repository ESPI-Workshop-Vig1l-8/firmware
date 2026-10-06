/**
 * ============================================================================
 *  AETHERCORP // SENTINEL-X — Firmware du nœud VIG1L-8 (ESP32)
 * ============================================================================
 *  Capteurs -> JSON -> MQTTS (TLS) vers le broker du PC Serveur Local.
 *
 *  Topics (DEVICE_ID = nom d'utilisateur MQTT, voir l'ACL de l'infra) :
 *    vigil8/<id>/telemetry  publié toutes les 2 s
 *    vigil8/<id>/event      publié à chaque changement d'état du PIR
 *    vigil8/<id>/status     {"online":true} à la connexion, {"online":false} en LWT (retenu)
 *    vigil8/<id>/cmd        reçu : {"buzzer":bool,"strobe":bool,"duration_s":int}
 *
 *  Format détaillé : README.md de ce dépôt et de infra.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "include/secrets.h manquant : copier include/secrets.example.h vers include/secrets.h et le remplir"
#endif

const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long STROBE_HALF_PERIOD_MS = 100;

WiFiClientSecure net;
PubSubClient mqtt(net);
DHT dht(PIN_DHT, DHT22);

String topicTelemetry, topicEvent, topicStatus, topicCmd;

// Un compteur par topic : un trou dans "seq" = message perdu
uint32_t seqTelemetry = 0;
uint32_t seqEvent = 0;

int lastPir = LOW;
uint16_t pirEvents = 0;       // détections depuis la dernière télémétrie

unsigned long lastTelemetry = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;

bool buzzerOn = false;
bool strobeOn = false;
unsigned long alarmUntil = 0;

// Arrondi à 0,1 pour un JSON lisible (25.8 et pas 25.799999)
static double round1(float x) {
  return roundf(x * 10.0f) / 10.0;
}

// ============================================================================
// ACTIONNEURS
// ============================================================================

void setAlarm(bool buzzer, bool strobe, int durationS) {
  buzzerOn = buzzer;
  strobeOn = strobe;
  digitalWrite(PIN_BUZZER, buzzerOn ? HIGH : LOW);
  if (!strobeOn) digitalWrite(PIN_LED_ALERTE, LOW);
  alarmUntil = (buzzerOn || strobeOn) ? millis() + (unsigned long)durationS * 1000UL : 0;
  Serial.printf("[CMD] buzzer=%d strobe=%d pendant %d s\n", buzzerOn, strobeOn, durationS);
}

void updateAlarm(unsigned long now) {
  if (alarmUntil && (long)(now - alarmUntil) >= 0) {
    setAlarm(false, false, 0);
  }
  if (strobeOn) {
    digitalWrite(PIN_LED_ALERTE, (now / STROBE_HALF_PERIOD_MS) % 2 ? HIGH : LOW);
  }
}

// Commande reçue sur vigil8/<id>/cmd
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[CMD] JSON invalide : %s\n", err.c_str());
    return;
  }
  int duration = doc["duration_s"] | ALARM_DEFAULT_S;
  duration = constrain(duration, 1, ALARM_MAX_S);
  setAlarm(doc["buzzer"] | false, doc["strobe"] | false, duration);
}

// ============================================================================
// PUBLICATIONS
// ============================================================================

bool publishJson(const String& topic, JsonDocument& doc, bool retained = false) {
  char buffer[384];
  size_t n = serializeJson(doc, buffer, sizeof(buffer));
  Serial.printf("[PUB] %s %s\n", topic.c_str(), buffer);
  return mqtt.connected() && mqtt.publish(topic.c_str(), (const uint8_t*)buffer, n, retained);
}

void publishTelemetry() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  bool dhtOk = !isnan(t) && !isnan(h);
  unsigned long now = millis();

  JsonDocument doc;
  doc["v"] = 1;
  doc["device_id"] = DEVICE_ID;
  doc["seq"] = seqTelemetry;
  doc["uptime_ms"] = now;
  // Pas de "dernière valeur connue" : une panne doit rester visible (null)
  if (dhtOk) {
    doc["temp_c"] = round1(t);
    doc["hum_pct"] = round1(h);
  } else {
    doc["temp_c"] = nullptr;
    doc["hum_pct"] = nullptr;
  }
  doc["gas_mv"] = (int)(analogReadMilliVolts(PIN_MQ2_AO) * MQ2_DIVIDER);
  doc["pir"] = digitalRead(PIN_PIR) == HIGH;
  doc["pir_events"] = pirEvents;
  JsonObject status = doc["status"].to<JsonObject>();
  status["dht"] = dhtOk ? "ok" : "error";
  status["gas_warm"] = now >= GAS_WARMUP_MS;
  status["rssi"] = WiFi.RSSI();

  if (publishJson(topicTelemetry, doc)) {
    pirEvents = 0;
  }
  // Incrémenté même si l'envoi échoue : le trou dans seq signale la perte
  seqTelemetry++;
}

void publishMotionEvent(bool state) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["device_id"] = DEVICE_ID;
  doc["seq"] = seqEvent++;
  doc["uptime_ms"] = millis();
  doc["type"] = "motion";
  doc["state"] = state;
  publishJson(topicEvent, doc);
}

// ============================================================================
// CONNEXIONS (non bloquantes : les capteurs continuent pendant les tentatives)
// ============================================================================

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
  WiFi.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.printf("[WIFI] Connexion à %s...\n", WIFI_SSID);
}

void ensureWifi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED || now - lastWifiAttempt < WIFI_RETRY_MS) return;
  Serial.println("[WIFI] Pas de connexion, nouvel essai");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = now;
}

void ensureMqtt(unsigned long now) {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (lastMqttAttempt && now - lastMqttAttempt < MQTT_RETRY_MS) return;
  lastMqttAttempt = now;

  Serial.printf("[MQTT] Connexion TLS à %s:%d (IP locale %s)...\n",
                MQTT_HOST, MQTT_PORT, WiFi.localIP().toString().c_str());
  // LWT : le broker publie {"online":false} (retenu) si le nœud disparaît
  if (!mqtt.connect(DEVICE_ID, DEVICE_ID, MQTT_PASSWORD,
                    topicStatus.c_str(), 1, true, "{\"online\":false}")) {
    // -2 : TCP/TLS refusé (IP, port, certificat) ; 4/5 : identifiants ou ACL
    Serial.printf("[MQTT] Échec (state=%d), nouvel essai dans %lu s\n",
                  mqtt.state(), MQTT_RETRY_MS / 1000);
    return;
  }
  Serial.println("[MQTT] Connecté");
  mqtt.subscribe(topicCmd.c_str(), 1);

  JsonDocument status;
  status["online"] = true;
  status["fw"] = FW_VERSION;
  status["ip"] = WiFi.localIP().toString();
  publishJson(topicStatus, status, true);
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.printf("\n=== SENTINEL-X %s // firmware %s ===\n", DEVICE_ID, FW_VERSION);

  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_LED_ESP, OUTPUT);
  pinMode(PIN_LED_ALERTE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED_ESP, LOW);
  digitalWrite(PIN_LED_ALERTE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  analogSetPinAttenuation(PIN_MQ2_AO, ADC_11db);  // plage 0-3.3 V
  dht.begin();

  String base = String("vigil8/") + DEVICE_ID + "/";
  topicTelemetry = base + "telemetry";
  topicEvent = base + "event";
  topicStatus = base + "status";
  topicCmd = base + "cmd";

  net.setCACert(MQTT_CA_CERT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);   // la télémétrie dépasse les 256 octets par défaut avec l'en-tête
  mqtt.setKeepAlive(30);

  startWifi();
}

void loop() {
  unsigned long now = millis();

  ensureWifi(now);
  ensureMqtt(now);
  mqtt.loop();
  updateAlarm(now);

  // PIR : événement immédiat à chaque changement (après calibration)
  if (now >= PIR_WARMUP_MS) {
    int pir = digitalRead(PIN_PIR);
    if (pir != lastPir) {
      lastPir = pir;
      digitalWrite(PIN_LED_ESP, pir);
      if (pir == HIGH) pirEvents++;
      publishMotionEvent(pir == HIGH);
    }
  }

  if (now - lastTelemetry >= TELEMETRY_INTERVAL_MS) {
    lastTelemetry = now;
    publishTelemetry();
  }

  delay(10);
}
