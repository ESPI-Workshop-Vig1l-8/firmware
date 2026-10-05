/**
 * ============================================================================
 *  AETHERCORP INDUSTRIAL SOLUTIONS // VIG1L-8 - TEST RAPIDE PIR HC-SR501
 *  Microcontrôleur : ESP32 (WROOM-32)
 *  Environnement : PlatformIO
 * ============================================================================
 */

#include <Arduino.h>

#define PIN_PIR        14  // Broche OUT du capteur (GPIO 14)
#define PIN_LED_ESP     2  // LED bleue intégrée de l'ESP32

int lastState = LOW;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LED_ESP, OUTPUT);
  digitalWrite(PIN_LED_ESP, LOW);

  Serial.println("\n==============================================");
  Serial.println("  TEST CAPTEUR DE PRÉSENCE PIR (HC-SR501)");
  Serial.println("==============================================");
  Serial.println("[INFO] Calibration thermique du capteur...");
  Serial.println("[INFO] Ne bougez pas devant le capteur pendant 15s...");

  // Compte à rebours de calibration avec clignotement de la LED
  for (int i = 15; i > 0; i--) {
    Serial.printf("Pret dans %d s...\n", i);
    digitalWrite(PIN_LED_ESP, !digitalRead(PIN_LED_ESP));
    delay(1000);
  }
  digitalWrite(PIN_LED_ESP, LOW);

  Serial.println("\n>>> CAPTEUR PRÊT ! PASSEZ VOTRE MAIN DEVANT <<<\n");
}

void loop() {
  int currentState = digitalRead(PIN_PIR);

  // Détection du passage de Rien -> Mouvement
  if (currentState == HIGH && lastState == LOW) {
    digitalWrite(PIN_LED_ESP, HIGH); // Allume la LED bleue de la carte
    Serial.println("🚨 [ALERTE] MOUVEMENT DÉTECTÉ ! (Présence humaine)");
    lastState = HIGH;
  }
  // Détection de la fin du mouvement
  else if (currentState == LOW && lastState == HIGH) {
    digitalWrite(PIN_LED_ESP, LOW); // Éteint la LED
    Serial.println("✅ [CALME] Fin de détection (Zone sécurisée)");
    lastState = LOW;
  }

  delay(100);
}
