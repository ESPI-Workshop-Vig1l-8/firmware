#pragma once
/**
 * Modèle : copier ce fichier vers include/secrets.h (ignoré par git) et le remplir.
 * Ne jamais commiter secrets.h.
 */

#define WIFI_SSID     "VIGIL8_HOTSPOT"
#define WIFI_PASSWORD ""

// Mot de passe du compte MQTT DEVICE_ID (voir MQTT_DEVICE_ACCOUNTS dans infra/.env)
#define MQTT_PASSWORD ""

// Certificat de l'autorité locale : contenu de infra/certs/ca/ca.crt
// (généré par infra/scripts/gen-certs.sh)
static const char MQTT_CA_CERT[] = R"PEM(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)PEM";
