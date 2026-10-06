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
 *    vigil8/<id>/cmd        reçu : {"strobe":bool,"duration_s":int} -> LED environnement clignotante
 *
 *  LED : mouvement (PIR) ; environnement fixe = plafond local gaz/température
 *  dépassé (premier avertissement), clignotante = alerte décidée par le serveur.
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

bool tempWarn = false;         // plafond local de température dépassé
bool gasWarn = false;          // plafond local de gaz dépassé
bool envWarn = false;          // l'un des deux
bool alertOn = false;          // alerte envoyée par le serveur
unsigned long alertUntil = 0;

// Arrondi à 0,1 pour un JSON lisible (25.8 et pas 25.799999)
static double round1(float x) {
  return roundf(x * 10.0f) / 10.0;
}

// ============================================================================
// ACTIONNEURS
// ============================================================================

void setAlert(bool on, int durationS) {
  alertOn = on;
  alertUntil = on ? millis() + (unsigned long)durationS * 1000UL : 0;
  Serial.printf("[CMD] alerte serveur %s (%d s)\n", on ? "ON" : "OFF", durationS);
}

// Plafonds fixes avec hystérésis : premier avertissement local, sans décision d'anomalie
void updateEnvWarn(bool dhtOk, float tempC, int gasMv, bool gasWarm) {
  tempWarn = dhtOk && tempC > (tempWarn ? TEMP_WARN_C - TEMP_HYSTERESIS_C : TEMP_WARN_C);
  gasWarn = gasWarm && gasMv > (gasWarn ? GAS_WARN_MV - GAS_HYSTERESIS_MV : GAS_WARN_MV);
  if ((tempWarn || gasWarn) != envWarn) {
    envWarn = tempWarn || gasWarn;
    Serial.printf("[ENV] avertissement local %s (T=%.1f °C, gaz=%d mV)\n",
                  envWarn ? "ON" : "OFF", tempC, gasMv);
  }
}

// LED environnement : clignote pour une alerte serveur, sinon fixe si plafond dépassé
void updateEnvLed(unsigned long now) {
  if (alertOn && (long)(now - alertUntil) >= 0) {
    setAlert(false, 0);
  }
  bool blink = (now / STROBE_HALF_PERIOD_MS) % 2;
  digitalWrite(PIN_LED_ENV, alertOn ? blink : envWarn);
}

// Commande reçue sur vigil8/<id>/cmd
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[CMD] JSON invalide : %s\n", err.c_str());
    return;
  }
  int duration = doc["duration_s"] | ALERT_DEFAULT_S;
  duration = constrain(duration, 1, ALERT_MAX_S);
  setAlert(doc["strobe"] | false, duration);
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
  int gasMv = (int)(analogReadMilliVolts(PIN_MQ2_AO) * MQ2_DIVIDER);
  bool gasWarm = now >= GAS_WARMUP_MS;
  updateEnvWarn(dhtOk, t, gasMv, gasWarm);
  doc["gas_mv"] = gasMv;
  doc["pir"] = digitalRead(PIN_PIR) == HIGH;
  doc["pir_events"] = pirEvents;
  JsonObject status = doc["status"].to<JsonObject>();
  status["dht"] = dhtOk ? "ok" : "error";
  status["gas_warm"] = gasWarm;
  status["env_warn"] = envWarn;
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
  pinMode(PIN_LED_MOTION, OUTPUT);
  pinMode(PIN_LED_ENV, OUTPUT);
  digitalWrite(PIN_LED_ESP, LOW);
  digitalWrite(PIN_LED_MOTION, LOW);
  digitalWrite(PIN_LED_ENV, LOW);
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
  updateEnvLed(now);

  // PIR : événement immédiat à chaque changement (après calibration)
  if (now >= PIR_WARMUP_MS) {
    int pir = digitalRead(PIN_PIR);
    if (pir != lastPir) {
      lastPir = pir;
      digitalWrite(PIN_LED_ESP, pir);
      digitalWrite(PIN_LED_MOTION, pir);
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
