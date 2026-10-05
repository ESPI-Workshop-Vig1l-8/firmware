/**
 * TEST RAPIDE - CAPTEUR DE PRÉSENCE PIR HC-SR501
 * Carte : ESP32 Dev Module
 */

#define PIN_PIR       14  // Broche OUT du capteur
#define PIN_LED_ESP    2  // LED bleue intégrée sur la plupart des ESP32

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
  Serial.println("[INFO] Calibration du capteur en cours...");
  Serial.println("[INFO] Attendez 20 à 30 secondes sans bouger devant...");

  // Petit compte à rebours de warm-up (le capteur a besoin de se calibrer)
  for (int i = 15; i > 0; i--) {
    Serial.printf("Pret dans %d s...\n", i);
    digitalWrite(PIN_LED_ESP, !digitalRead(PIN_LED_ESP)); // Clignote pendant le warm-up
    delay(1000);
  }
  digitalWrite(PIN_LED_ESP, LOW);

  Serial.println("\n>>> CAPTEUR PRÊT ! PASSE TA MAIN DEVANT <<<\n");
}

void loop() {
  int currentState = digitalRead(PIN_PIR);

  // Détection d'un front montant (changement d'état : rien -> mouvement)
  if (currentState == HIGH && lastState == LOW) {
    digitalWrite(PIN_LED_ESP, HIGH); // Allume la LED bleue de l'ESP32
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
