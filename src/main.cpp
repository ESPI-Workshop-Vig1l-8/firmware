/**
 * DIAGNOSTIC EN DIRECT - CAPTEUR DE PRÉSENCE PIR HC-SR501
 */

#include <Arduino.h>

#define PIN_PIR       14  // Broche OUT (GPIO 14)
#define PIN_LED_ESP    2  // LED intégrée

void setup() {
  Serial.begin(115200);
  delay(1000);

  // INPUT_PULLDOWN force la broche à 0V si aucun signal n'est envoyé
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_LED_ESP, OUTPUT);

  Serial.println("\n==============================================");
  Serial.println("  DIAGNOSTIC EN TEMPS RÉEL DU PIN GPIO 14");
  Serial.println("==============================================");
}

void loop() {
  int val = digitalRead(PIN_PIR);

  digitalWrite(PIN_LED_ESP, val);

  if (val == HIGH) {
    Serial.println(">>> ETAT: 1 (HIGH) -> MOUVEMENT DÉTECTÉ ou 3.3V continu");
  } else {
    Serial.println("    ETAT: 0 (LOW)  -> CALME (0V)");
  }

  delay(400);
}
