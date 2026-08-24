# Hardware y pines

Las conexiones internas indicadas aquí provienen del código y de pruebas del
proyecto. El manual comercial de la DualMCU contiene inconsistencias, por lo
que los cambios deben comprobarse físicamente antes de modificar estos valores.

## UART interno DualMCU

| Dirección | ESP32 | RP2040 |
| --- | --- | --- |
| ESP32 → RP2040 | TX GPIO17 | RX GPIO1 |
| RP2040 → ESP32 | RX GPIO16 | TX GPIO0 |

- 115200, 8N1.
- SW3: ambos interruptores en `ON`.
- SW2 A: USB al RP2040; SW2 B: USB al ESP32.

## ESP32 principal

| Función | Pin/configuración |
| --- | --- |
| Hall | GPIO2 |
| PC817/chasis | GPIO4, activo LOW provisional |
| I2C SDA/SCL | GPIO21/GPIO22, 400 kHz |
| PCA9548A | `0x70` provisional |
| MLX90614 | `0x5A` provisional |
| RX desde C6 | GPIO36/SENSOR_VP |

Revisar el posible conflicto del RGB onboard con GPIO4, GPIO25 y GPIO26.

## ESP32-C6

| Señal | Pin |
| --- | --- |
| TX hacia ESP32 principal | GPIO16 provisional |
| RX reservado | GPIO17 |
| Tierra | GND común |

C6 TX GPIO16 se conecta a ESP32 GPIO36. Ambos son 3.3 V. Confirmar que GPIO16
está expuesto en el modelo exacto antes de soldar.

## RP2040

| Función | Pines |
| --- | --- |
| DFPlayer TX/RX | GPIO8/GPIO9 |
| TFT DC/RST | GPIO14/GPIO15 |
| TFT SCK/MOSI | GPIO18/GPIO19 |
| TFT CS/BL | GPIO21/GPIO22 |

El código usa ILI9488; confirmar el controlador real del TFT.

## Mecánica provisional

- 4 imanes por revolución.
- Circunferencia rodada de 1257 mm.
- Ventana RPM de 200 ms.
- Detenido después de 500 ms sin pulsos.

Estos valores deben medirse en el kart y actualizarse en
`firmware/rp2040_interfaz/BoardConfig.h`.

