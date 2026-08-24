# Firmware integrado

Este directorio contiene únicamente el candidato que se seguirá modificando.

## Componentes

- `esp32_principal/`: sensores, recepción C6, enlace RP2040 y MQTT/TLS.
- `rp2040_interfaz/`: RPM/velocidad, estados, alarmas, TFT y DFPlayer.
- `esp32_c6_bms/`: dos enlaces BLE Daly y UART hacia el ESP32 principal.
- `libraries/LobosDualMcuProtocol/`: protocolo ESP32 ↔ RP2040.
- `libraries/LobosC6BmsProtocol/`: protocolo ESP32-C6 → ESP32.
- `common/telemetry.schema.json`: contrato JSON publicado por MQTT.

## Compilar

```powershell
platformio run -e esp32_principal
platformio run -e rp2040_interfaz
platformio run -e esp32_c6_bms
```

El C6 usa PIOArduino. La placa física exacta puede requerir cambiar `board` en
`platformio.ini` sin modificar el protocolo.

## Credenciales

Copiar `esp32_principal/secrets.example.h` como `secrets.h`. Mantener
`LOBOS_MQTT_ALLOW_INSECURE = false` para validar TLS. `secrets.h` nunca se sube
a Git.

## Advertencia

Los pines externos, la polaridad del PC817, el modelo de TFT, el número de
imanes y la circunferencia aún requieren confirmación física. C6 y ESP32
principal deben actualizarse juntos porque el protocolo C6 vigente es v2.

