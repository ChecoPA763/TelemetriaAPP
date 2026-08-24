"""Grabacion de sesiones de telemetria a archivos CSV.

Cada sesion escribe directamente a un .csv en disco mientras esta activa
(sin base de datos intermedia, segun lo decidido para la v1.1). Un renglon
por muestra de telemetria recibida por MQTT durante la sesion.

Columnas documentadas en docs/diseno/objetivo_interfaz_telemetria.md:
timestamp, seq, speed_kmh, rpm, temp_c, chassis_fault, bms1_voltage_v,
bms1_current_a, bms1_power_w, bms1_temp_c, bms1_soc_pct, bms2_voltage_v,
bms2_current_a, bms2_power_w, bms2_temp_c, bms2_soc_pct
"""

from __future__ import annotations

import csv
import datetime as _dt
import os
import time
from dataclasses import dataclass, field

from telemetry import TelemetrySnapshot


CSV_COLUMNS = [
    "timestamp",
    "seq",
    "speed_kmh",
    "rpm",
    "temp_c",
    "chassis_fault",
    "bms1_voltage_v",
    "bms1_current_a",
    "bms1_power_w",
    "bms1_temp_c",
    "bms1_soc_pct",
    "bms2_voltage_v",
    "bms2_current_a",
    "bms2_power_w",
    "bms2_temp_c",
    "bms2_soc_pct",
]


def _snapshot_to_row(snapshot: TelemetrySnapshot) -> dict:
    now = _dt.datetime.now().isoformat(timespec="milliseconds")
    return {
        "timestamp": now,
        "seq": snapshot.seq,
        "speed_kmh": snapshot.speed_kmh,
        "rpm": "" if snapshot.rpm is None else snapshot.rpm,
        "temp_c": snapshot.temp_c,
        "chassis_fault": snapshot.chassis_fault,
        "bms1_voltage_v": snapshot.bms1.voltage_v,
        "bms1_current_a": snapshot.bms1.current_a,
        "bms1_power_w": snapshot.bms1.power_w,
        "bms1_temp_c": "" if snapshot.bms1.temp_c is None else snapshot.bms1.temp_c,
        "bms1_soc_pct": "" if snapshot.bms1.soc_pct is None else snapshot.bms1.soc_pct,
        "bms2_voltage_v": snapshot.bms2.voltage_v,
        "bms2_current_a": snapshot.bms2.current_a,
        "bms2_power_w": snapshot.bms2.power_w,
        "bms2_temp_c": "" if snapshot.bms2.temp_c is None else snapshot.bms2.temp_c,
        "bms2_soc_pct": "" if snapshot.bms2.soc_pct is None else snapshot.bms2.soc_pct,
    }


def default_sessions_dir() -> str:
    """Carpeta donde se guardan los CSV de sesiones.

    En Android, App.user_data_dir es la ruta correcta para escritura sin
    permisos adicionales. Aqui solo se calcula un valor por defecto para
    Windows/desarrollo; main.py puede pasar un directorio distinto.
    """

    base = os.path.join(os.path.expanduser("~"), ".lobos_racing_sessions")
    os.makedirs(base, exist_ok=True)
    return base


def _slugify(name: str) -> str:
    keep = [c if c.isalnum() or c in ("-", "_") else "_" for c in name.strip()]
    slug = "".join(keep).strip("_")
    return slug or "sesion"


@dataclass
class SessionSummary:
    name: str
    path: str
    sample_count: int
    duration_s: float
    max_speed_kmh: float
    max_temp_c: float


@dataclass
class SessionRecorder:
    """Graba una sesion activa a un CSV; expone metricas en vivo."""

    sessions_dir: str = field(default_factory=default_sessions_dir)

    def __post_init__(self) -> None:
        self._active_name: str | None = None
        self._active_path: str | None = None
        self._file = None
        self._writer: csv.DictWriter | None = None
        self._started_at: float | None = None
        self.sample_count = 0
        self.max_speed_kmh = 0.0
        self.max_temp_c = float("-inf")

    @property
    def is_recording(self) -> bool:
        return self._file is not None

    def start(self, name: str | None = None) -> str:
        if self.is_recording:
            raise RuntimeError("Ya hay una sesion grabando; deten la actual primero.")

        os.makedirs(self.sessions_dir, exist_ok=True)
        today = _dt.date.today().isoformat()
        base_name = name or f"Vuelta_{today}"
        slug = _slugify(base_name)

        candidate = slug
        suffix = 1
        while os.path.exists(os.path.join(self.sessions_dir, f"{candidate}.csv")):
            suffix += 1
            candidate = f"{slug}_{suffix}"

        path = os.path.join(self.sessions_dir, f"{candidate}.csv")
        self._file = open(path, "w", newline="", encoding="utf-8")
        self._writer = csv.DictWriter(self._file, fieldnames=CSV_COLUMNS)
        self._writer.writeheader()
        self._active_name = candidate
        self._active_path = path
        self._started_at = time.monotonic()
        self.sample_count = 0
        self.max_speed_kmh = 0.0
        self.max_temp_c = float("-inf")
        return candidate

    def record(self, snapshot: TelemetrySnapshot) -> None:
        if not self.is_recording:
            return
        row = _snapshot_to_row(snapshot)
        self._writer.writerow(row)
        self._file.flush()
        self.sample_count += 1
        self.max_speed_kmh = max(self.max_speed_kmh, snapshot.speed_kmh)
        self.max_temp_c = max(self.max_temp_c, snapshot.temp_c)

    def stop(self) -> SessionSummary | None:
        if not self.is_recording:
            return None
        duration = time.monotonic() - (self._started_at or time.monotonic())
        summary = SessionSummary(
            name=self._active_name or "",
            path=self._active_path or "",
            sample_count=self.sample_count,
            duration_s=duration,
            max_speed_kmh=self.max_speed_kmh,
            max_temp_c=0.0 if self.max_temp_c == float("-inf") else self.max_temp_c,
        )
        try:
            self._file.close()
        finally:
            self._file = None
            self._writer = None
            self._active_name = None
            self._active_path = None
            self._started_at = None
        return summary

    def elapsed_s(self) -> float:
        if not self.is_recording or self._started_at is None:
            return 0.0
        return time.monotonic() - self._started_at

    def list_saved_sessions(self) -> list[SessionSummary]:
        """Lista los CSV ya guardados en sessions_dir, mas recientes primero."""

        if not os.path.isdir(self.sessions_dir):
            return []

        results: list[SessionSummary] = []
        entries = [
            entry for entry in os.scandir(self.sessions_dir) if entry.name.endswith(".csv")
        ]
        entries.sort(key=lambda e: e.stat().st_mtime, reverse=True)

        for entry in entries:
            try:
                with open(entry.path, "r", newline="", encoding="utf-8") as fh:
                    reader = csv.DictReader(fh)
                    sample_count = 0
                    max_speed = 0.0
                    max_temp = 0.0
                    first_timestamp: str | None = None
                    last_timestamp: str | None = None
                    for row in reader:
                        sample_count += 1
                        timestamp = row.get("timestamp")
                        if timestamp:
                            if first_timestamp is None:
                                first_timestamp = timestamp
                            last_timestamp = timestamp
                        try:
                            max_speed = max(max_speed, float(row.get("speed_kmh") or 0.0))
                            max_temp = max(max_temp, float(row.get("temp_c") or 0.0))
                        except ValueError:
                            continue
            except OSError:
                continue

            duration_s = 0.0
            if first_timestamp and last_timestamp:
                try:
                    first = _dt.datetime.fromisoformat(first_timestamp)
                    last = _dt.datetime.fromisoformat(last_timestamp)
                    duration_s = max(0.0, (last - first).total_seconds())
                except ValueError:
                    duration_s = 0.0

            results.append(
                SessionSummary(
                    name=entry.name[: -len(".csv")],
                    path=entry.path,
                    sample_count=sample_count,
                    duration_s=duration_s,
                    max_speed_kmh=max_speed,
                    max_temp_c=max_temp,
                )
            )
        return results


