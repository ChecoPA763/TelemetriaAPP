# Siguientes pasos

Este prototipo está limpio para continuar trabajando, pero todavía no debe
considerarse firmware de producción.

## Primero

1. Compilar los tres entornos PlatformIO.
2. Compilar e instalar la APK desde este layout.
3. Confirmar pines, GND común y niveles de 3.3 V.
4. Resolver el conflicto potencial del RGB onboard.
5. Confirmar controlador TFT y GPIO16 expuesto del C6.

## Después, en banco

1. Probar UART interno durante 10 minutos sin errores ni timeouts.
2. Reiniciar solo RP2040 y comprobar detección/recuperación.
3. Cargar C6 y ESP32 juntos y comprobar la trama v2.
4. Conectar un BMS y después el segundo.
5. Probar desconexiones y confirmar que las edades aumentan.
6. Calibrar Hall, imanes y circunferencia.
7. Añadir sensores I2C uno por uno.
8. Medir PC817 en reposo y falla.
9. Añadir TFT/DFPlayer y vigilar alimentación/ruido.
10. Cortar/restaurar Wi-Fi sin detener UART.

## Mejoras directas

- Unificar umbrales de alarma entre app y RP2040.
- Congelar una v1 del protocolo DualMCU después de medirlo.
- Centralizar configuración no secreta de MQTT y hardware.
- Añadir gráfica histórica, GPS y tiempos de vuelta cuando existan datos reales.
- Implementar foreground service Android solo si se necesita grabar minimizado.

## Regla práctica

Modificar una capa a la vez. Primero lograr una compilación base reproducible;
después sensores; después interfaz local; finalmente prueba dinámica. El repo
anterior conserva simuladores y diagnósticos por si es necesario investigar una
falla, pero no deben volver a mezclarse con este prototipo principal.

