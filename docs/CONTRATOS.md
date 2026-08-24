# Contratos de comunicación

Este archivo resume los tres contratos que deben mantenerse sincronizados.
Las implementaciones exactas están en `firmware/libraries/` y
`firmware/common/telemetry.schema.json`.

## MQTT/JSON

- MQTT 3.1.1 sobre TLS, puerto 8883.
- Topic: `lobos/kart01/telemetry`.
- QoS 0, sin retain, máximo 5 Hz.
- Campos mínimos: `seq`, `speed_kmh`, `temp_c`, `bms1`, `bms2`.

Campos principales actuales:

```json
{
  "seq": 1,
  "uptime_ms": 1234,
  "speed_kmh": 42.3,
  "rpm": 560,
  "temp_c": 38.7,
  "chassis_fault": false,
  "data_age_ms": 20,
  "data_valid": true,
  "rp2040_online": true,
  "bms1": {
    "voltage_v": 52.8,
    "current_a": 12.3,
    "temp_c": 31.5,
    "soc_pct": 87.5,
    "age_ms": 250,
    "connected": true,
    "valid": true
  },
  "bms2": {
    "voltage_v": 52.6,
    "current_a": 11.9,
    "temp_c": 32.0,
    "soc_pct": 86.8,
    "age_ms": 300,
    "connected": true,
    "valid": true
  }
}
```

También pueden publicarse `state_flags`, `alarm_flags`, secuencias de origen,
`processing_time_us`, `raw_age_ms`, `flags` y `power_w`. La app calcula potencia
si falta y aplica defaults compatibles cuando no existen los campos de edad.

## UART interno ESP32 ↔ RP2040

- Versión experimental `0`.
- Little-endian, COBS y delimitador `0x00`.
- CRC16-CCITT, polinomio `0x1021`, inicial `0xFFFF`.
- Cabecera: `LR`, versión, tipo y longitud.
- `RawSensors`: 52 bytes antes de COBS, enviado a 20 Hz.
- `ProcessedTelemetry`: 56 bytes antes de COBS, respuesta a 10 Hz.

Las muestras transportan secuencias, Hall, temperatura, banderas, dos BMS,
velocidad, RPM, alarmas, tiempo de proceso y edad.

## UART ESP32-C6 → ESP32 principal

- Versión `2`, trama fija de 100 bytes.
- Magic `0xB5 0x4D`, tipo dual BMS `1`.
- Payload de 92 bytes.
- Little-endian y CRC16-Modbus.
- Dos registros de 43 bytes con flags, edad, pack, corriente, SoC,
  temperatura y 16 celdas.
- Frecuencia aproximada: 2 Hz.

La v2 no es compatible con la antigua trama de 96 bytes. Actualizar C6 y ESP32
principal juntos.

## Regla de cambio

No cambiar orden, tamaño, unidades o significado manteniendo la misma versión.
Cualquier cambio exige actualizar emisor, receptor, esquema JSON y este archivo
en el mismo commit.

