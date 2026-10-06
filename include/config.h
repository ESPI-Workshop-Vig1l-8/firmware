#pragma once
/**
 * Configuration du nœud Sentinel-X (non secrète).
 * Les mots de passe et le certificat sont dans secrets.h (non versionné).
 */

#define FW_VERSION "0.2.0"

// Identifiant du nœud = nom d'utilisateur MQTT (voir MQTT_DEVICE_ACCOUNTS dans infra/.env)
#define DEVICE_ID "VIG1L-8-NODE04"

// ---------------------------------------------------------------------------
// Réseau de table (192.168.10.0/24, le PC Serveur Local est en .1)
// ---------------------------------------------------------------------------
#define MQTT_HOST "192.168.10.1"   // doit figurer dans le certificat du broker
#define MQTT_PORT 18883            // port MQTTS publié par infra/docker-compose.yaml

#define USE_STATIC_IP 1
#define STATIC_IP   192, 168, 10, 20
#define GATEWAY_IP  192, 168, 10, 1
#define SUBNET_MASK 255, 255, 255, 0

// ---------------------------------------------------------------------------
// Broches (ESP32)
// ---------------------------------------------------------------------------
#define PIN_PIR        14  // HC-SR501 OUT
#define PIN_MQ2_AO     34  // MQ-2 AO via pont diviseur 10k/20k (ADC1, compatible Wi-Fi)
#define PIN_DHT         4  // DHT22 DATA
#define PIN_LED_ESP     2  // LED intégrée : suit le PIR
#define PIN_LED_ALERTE 19  // LED externe : "strobe" commandé par le serveur
#define PIN_BUZZER     18  // buzzer actif

// ---------------------------------------------------------------------------
// Mesures
// ---------------------------------------------------------------------------
#define TELEMETRY_INTERVAL_MS 2000UL     // 1 mesure / 2 s (maximum du DHT22)
#define PIR_WARMUP_MS         30000UL    // calibration du HC-SR501
#define GAS_WARMUP_MS         180000UL   // préchauffage du MQ-2 (status.gas_warm)
#define MQ2_DIVIDER           1.5f       // pont 10k/20k : tension AO = tension broche x 1.5

#define ALARM_DEFAULT_S 5     // durée si la commande ne précise pas duration_s
#define ALARM_MAX_S     60
