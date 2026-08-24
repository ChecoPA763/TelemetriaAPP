"""Cliente MQTT/TLS no bloqueante para la app Kivy."""

from __future__ import annotations

import ssl
import threading
import uuid
from collections.abc import Callable

import certifi
import paho.mqtt.client as mqtt

from telemetry import TelemetryError, TelemetrySnapshot


StatusCallback = Callable[[str, str], None]
TelemetryCallback = Callable[[TelemetrySnapshot], None]
ErrorCallback = Callable[[str], None]


class TelemetryMQTTClient:
    def __init__(
        self,
        on_telemetry: TelemetryCallback,
        on_status: StatusCallback,
        on_error: ErrorCallback,
    ) -> None:
        self._on_telemetry = on_telemetry
        self._on_status = on_status
        self._on_error = on_error
        self._client: mqtt.Client | None = None
        self._topic = ""
        self._lock = threading.Lock()
        self._manual_disconnect = False

    def connect(
        self,
        host: str,
        port: int,
        username: str,
        password: str,
        topic: str,
    ) -> None:
        if not all((host.strip(), username.strip(), password, topic.strip())):
            self._on_error("Host, usuario, contrasena y topic son obligatorios.")
            return

        self.stop()
        self._manual_disconnect = False
        self._topic = topic.strip()

        client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=f"LobosAndroid-{uuid.uuid4().hex[:10]}",
            protocol=mqtt.MQTTv311,
        )
        client.username_pw_set(username.strip(), password)
        # certifi aporta una CA confiable tambien dentro del APK, donde no siempre
        # existe el mismo almacen de certificados que en Windows.
        client.tls_set_context(ssl.create_default_context(cafile=certifi.where()))
        client.reconnect_delay_set(min_delay=1, max_delay=30)
        client.on_connect = self._handle_connect
        client.on_disconnect = self._handle_disconnect
        client.on_message = self._handle_message

        with self._lock:
            self._client = client

        self._on_status("connecting", "Conectando")
        try:
            client.connect_async(host.strip(), port, keepalive=30)
            client.loop_start()
        except Exception as exc:
            self._on_status("error", "Error de conexion")
            self._on_error(f"No se pudo iniciar MQTT: {exc}")

    def stop(self) -> None:
        with self._lock:
            client = self._client
            self._client = None

        if client is None:
            return

        self._manual_disconnect = True
        try:
            client.disconnect()
        except Exception:
            pass
        try:
            client.loop_stop()
        except Exception:
            pass

    def _handle_connect(self, client, userdata, flags, reason_code, properties) -> None:
        if reason_code == 0:
            result, _ = client.subscribe(self._topic, qos=0)
            if result == mqtt.MQTT_ERR_SUCCESS:
                self._on_status("connected", "MQTT conectado")
            else:
                self._on_status("error", "Error de suscripcion")
                self._on_error(f"No se pudo suscribir; codigo MQTT {result}.")
        else:
            self._on_status("error", "Conexion rechazada")
            self._on_error(f"HiveMQ rechazo la conexion: {reason_code}")

    def _handle_disconnect(
        self, client, userdata, disconnect_flags, reason_code, properties
    ) -> None:
        if self._manual_disconnect:
            self._on_status("disconnected", "Desconectado")
        else:
            self._on_status("retrying", "Reconectando")

    def _handle_message(self, client, userdata, message) -> None:
        if message.topic != self._topic:
            return
        try:
            snapshot = TelemetrySnapshot.from_json(message.payload)
        except TelemetryError as exc:
            self._on_error(f"Paquete descartado: {exc}")
            return
        self._on_telemetry(snapshot)
