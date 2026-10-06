/**
 * DIAGNOSTIC MATÉRIEL - VIG1L-8 (ESP32)
 *
 * Au démarrage : vérifie le câblage et l'alimentation de chaque composant,
 * puis affiche les mesures en continu sur le moniteur série (115200 bauds).
 *
 * Câblage :
 *   PIR HC-SR501 : VCC -> VIN (5V), GND -> GND, OUT -> GPIO 14
 *   MQ-2         : VCC -> VIN (5V), GND -> GND, AO -> pont 10k/20k -> GPIO 34
 *   DHT22        : VCC -> 3V3, GND -> GND, DATA -> GPIO 4 (+ pull-up 10k vers 3V3 si capteur nu)
 *   LED alerte   : GPIO 19 -> 220 Ω -> LED -> GND
 */

#include <Arduino.h>
#include <DHT.h>

#define PIN_PIR        14  // Sortie OUT du HC-SR501
#define PIN_MQ2_AO     34  // Sortie analogique du MQ-2 (ADC1, compatible Wi-Fi)
#define PIN_DHT         4  // Données du DHT22
#define PIN_LED_ESP     2  // LED intégrée
#define PIN_LED_ALERTE 19  // LED mouvement
#define PIN_LED_ENV    18  // LED environnement

const unsigned long PIR_WARMUP_MS      = 30000;  // calibration du HC-SR501 après mise sous tension
const unsigned long REPORT_INTERVAL_MS = 2000;   // le DHT22 ne supporte qu'une lecture toutes les 2 s
const float         MQ2_DIVIDER        = 1.5f;   // pont 10k/20k : tension AO = tension broche × 1.5

DHT dht(PIN_DHT, DHT22);

int lastPir = -1;
unsigned long lastPirChange = 0;
unsigned long lastReport = 0;

// ============================================================================
// VÉRIFICATIONS AU DÉMARRAGE
// ============================================================================

void checkLeds() {
  Serial.println("[LED]   Clignotement x3 : la LED de la carte ET les 2 LED externes doivent clignoter");
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_ESP, HIGH);
    digitalWrite(PIN_LED_ALERTE, HIGH);
    digitalWrite(PIN_LED_ENV, HIGH);
    delay(250);
    digitalWrite(PIN_LED_ESP, LOW);
    digitalWrite(PIN_LED_ALERTE, LOW);
    digitalWrite(PIN_LED_ENV, LOW);
    delay(250);
  }
  Serial.println("        Si une LED externe reste éteinte : la retourner (patte longue côté GPIO 19 / GPIO 18)");
}

void checkDht() {
  delay(2000);  // le DHT22 a besoin de ~2 s après la mise sous tension
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("[DHT22] ERREUR : aucune réponse -> VCC sur 3V3, GND, DATA sur GPIO 4, pull-up 10k (capteur nu) ?");
  } else {
    Serial.printf("[DHT22] OK : %.1f °C, %.1f %% d'humidité\n", t, h);
  }
}

// Plusieurs lectures pour repérer un fil AO débranché (valeurs instables),
// un MQ-2 non alimenté (≈ 0 V) ou un pont diviseur manquant (saturation).
void checkMq2() {
  const int samples = 20;
  int minMv = 5000, maxMv = 0;
  long sumMv = 0;
  for (int i = 0; i < samples; i++) {
    int mv = analogReadMilliVolts(PIN_MQ2_AO);
    sumMv += mv;
    if (mv < minMv) minMv = mv;
    if (mv > maxMv) maxMv = mv;
    delay(10);
  }
  int avgMv = sumMv / samples;

  Serial.printf("[MQ-2]  broche %d mV (min %d / max %d), AO ≈ %d mV : ",
                avgMv, minMv, maxMv, (int)(avgMv * MQ2_DIVIDER));
  if (maxMv - minMv > 300) {
    Serial.println("INSTABLE -> fil AO débranché ou mal enfoncé ?");
  } else if (avgMv < 50) {
    Serial.println("PAS DE SIGNAL -> VCC du MQ-2 sur VIN (5V), GND, fil AO ?");
  } else if (avgMv > 3000) {
    Serial.println("SATURÉ -> pont diviseur absent ? Débrancher AO pour protéger l'ESP32");
  } else {
    Serial.println("OK (valeurs stables après 1 à 3 min de préchauffage)");
  }
}

void checkPir() {
  Serial.printf("[PIR]   État actuel : %s\n", digitalRead(PIN_PIR) == HIGH ? "HIGH" : "LOW");
  Serial.println("        Un PIR débranché lit LOW comme \"aucun mouvement\" : agiter la main après le préchauffage (30 s)");
}

// ============================================================================
// SETUP & LOOP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  // INPUT_PULLDOWN force la broche à 0V si le fil OUT est débranché
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_LED_ESP, OUTPUT);
  pinMode(PIN_LED_ALERTE, OUTPUT);
  pinMode(PIN_LED_ENV, OUTPUT);
  digitalWrite(PIN_LED_ESP, LOW);
  digitalWrite(PIN_LED_ALERTE, LOW);
  analogSetPinAttenuation(PIN_MQ2_AO, ADC_11db);  // plage de mesure 0-3.3 V
  dht.begin();

  Serial.println("\n==============================================");
  Serial.println("  VIG1L-8 // DIAGNOSTIC MATÉRIEL");
  Serial.println("==============================================");
  checkLeds();
  checkDht();
  checkMq2();
  checkPir();
  Serial.println("==============================================\n");
}

void loop() {
  unsigned long now = millis();
  bool pirReady = now >= PIR_WARMUP_MS;

  // PIR : réaction immédiate aux changements d'état (après préchauffage)
  int pir = digitalRead(PIN_PIR);
  if (pirReady && pir != lastPir) {
    if (lastPir != -1) {
      Serial.printf("[PIR]   %s (état précédent : %.1f s)\n",
                    pir == HIGH ? ">>> MOUVEMENT DÉTECTÉ" : "calme",
                    (now - lastPirChange) / 1000.0);
    }
    digitalWrite(PIN_LED_ESP, pir);
    digitalWrite(PIN_LED_ALERTE, pir);
    lastPir = pir;
    lastPirChange = now;
  }

  // Mesures périodiques de tous les capteurs
  if (now - lastReport >= REPORT_INTERVAL_MS) {
    lastReport = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int gasMv = analogReadMilliVolts(PIN_MQ2_AO) * MQ2_DIVIDER;

    if (isnan(t) || isnan(h)) {
      Serial.print("[MESURE] DHT22 ERREUR        | ");
    } else {
      Serial.printf("[MESURE] T=%5.1f °C H=%5.1f %% | ", t, h);
    }
    Serial.printf("gaz AO=%4d mV | PIR=", gasMv);
    if (pirReady) {
      Serial.println(pir == HIGH ? "MOUVEMENT" : "calme");
    } else {
      Serial.printf("préchauffage (%lu s)\n", (PIR_WARMUP_MS - now) / 1000);
    }
  }

  delay(50);
}
