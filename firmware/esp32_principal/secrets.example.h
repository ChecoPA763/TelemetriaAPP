#pragma once

constexpr char LOBOS_WIFI_SSID[] = "TU_RED";
constexpr char LOBOS_WIFI_PASSWORD[] = "TU_PASSWORD";
constexpr char LOBOS_MQTT_HOST[] = "TU_CLUSTER.s1.eu.hivemq.cloud";
constexpr uint16_t LOBOS_MQTT_PORT = 8883;
constexpr char LOBOS_MQTT_USERNAME[] = "TU_USUARIO";
constexpr char LOBOS_MQTT_PASSWORD[] = "TU_PASSWORD_MQTT";
constexpr char LOBOS_MQTT_TOPIC[] = "lobos/kart01/telemetry";

// false por defecto: valida la identidad del broker con la CA configurada.
// true se permite unicamente para aislar problemas en una prueba de laboratorio.
constexpr bool LOBOS_MQTT_ALLOW_INSECURE = false;
constexpr char LOBOS_MQTT_CA_CERT[] = R"EOF(
-----BEGIN CERTIFICATE-----
PEGA_AQUI_LA_CA_RAIZ_QUE_VALIDA_EL_CERTIFICADO_DEL_BROKER
-----END CERTIFICATE-----
)EOF";

