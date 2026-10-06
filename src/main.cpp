/**
 * DIAGNOSTIC EN DIRECT - CAPTEUR DE PRÉSENCE PIR HC-SR501 (ESP32)
 *
 * Câblage : VCC -> VIN (5V), GND -> GND, OUT -> GPIO 14
 * Réglages du module : cavalier sur H, potentiomètre Tx (délai) au minimum.
 */

#include <Arduino.h>

#define PIN_PIR       14  // Broche OUT (GPIO 14)
#define PIN_LED_ESP    2  // LED intégrée

const unsigned long WARMUP_MS = 30000;  // calibration du HC-SR501 après mise sous tension

int lastState = -1;
unsigned long lastChange = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // INPUT_PULLDOWN force la broche à 0V si le fil OUT est débranché
  pinMode(PIN_PIR, INPUT_PULLDOWN);
  pinMode(PIN_LED_ESP, OUTPUT);
  digitalWrite(PIN_LED_ESP, LOW);

  Serial.println("\n==============================================");
  Serial.println("  DIAGNOSTIC PIR HC-SR501 (GPIO 14)");
  Serial.println("  Préchauffage 30 s, ne pas bouger...");
  Serial.println("==============================================");
  while (millis() < WARMUP_MS) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("\n[PIR] Prêt.");
}

void loop() {
  int state = digitalRead(PIN_PIR);

  // Affichage uniquement sur changement d'état
  if (state != lastState) {
    unsigned long now = millis();
    if (lastState == -1) {
      Serial.printf("[PIR] État initial : %s\n", state == HIGH ? "HIGH" : "LOW");
    } else {
      Serial.printf("[PIR] %s (état précédent : %.1f s)\n",
                    state == HIGH ? ">>> MOUVEMENT DÉTECTÉ" : "    calme",
                    (now - lastChange) / 1000.0);
    }
    digitalWrite(PIN_LED_ESP, state);
    lastState = state;
    lastChange = now;
  }

  delay(50);
}
