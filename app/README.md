# Aplicación Kivy

Aplicación Android/Windows con cinco pantallas: General, BMS 1, BMS 2,
Telemetría y Registro.

## Archivos

| Archivo | Función |
| --- | --- |
| `main.py` | Interfaz, navegación y coordinación. |
| `mqtt_client.py` | MQTT 3.1.1 sobre TLS. |
| `telemetry.py` | Validación del JSON. |
| `telemetry_diagnostics.py` | Frecuencia y vigencia de datos. |
| `alarm_thresholds.py` | Umbrales visuales. |
| `session_recorder.py` | Sesiones CSV. |
| `csv_exporter.py` | Exportación Android/escritorio. |

## Ejecutar

```powershell
python -m pip install -r requirements.txt
python main.py
```

## Compilar APK

```bash
buildozer android debug
```

Configuración actual: versión 1.1.1, Android mínimo 24, API 36, arm64-v8a y
permiso `INTERNET`.

