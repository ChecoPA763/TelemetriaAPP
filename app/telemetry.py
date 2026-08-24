"""Modelo y validacion de los snapshots de telemetria Lobos Racing."""

from __future__ import annotations

import json
import math
import time
from dataclasses import dataclass
from typing import Any


class TelemetryError(ValueError):
    """El payload no cumple el contrato minimo de telemetria."""


def _number(value: Any, field: str, *, optional: bool = False) -> float | None:
    if value is None and optional:
        return None
    if isinstance(value, bool):
        raise TelemetryError(f"{field} debe ser numerico")
    try:
        result = float(value)
    except (TypeError, ValueError) as exc:
        raise TelemetryError(f"{field} debe ser numerico") from exc
    if not math.isfinite(result):
        raise TelemetryError(f"{field} no es finito")
    return result


def _boolean(value: Any, default: bool) -> bool:
    return value if isinstance(value, bool) else default


def _milliseconds(value: Any, field: str, *, default: int = 0) -> int:
    if value is None:
        return default
    number = _number(value, field)
    if number < 0 or not number.is_integer():
        raise TelemetryError(f"{field} debe ser un entero no negativo")
    return int(number)


@dataclass(frozen=True, slots=True)
class BMSData:
    voltage_v: float
    current_a: float
    power_w: float
    temp_c: float | None
    soc_pct: float | None
    connected: bool
    valid: bool
    age_ms: int = 0

    @classmethod
    def from_mapping(cls, value: Any, name: str) -> "BMSData":
        if not isinstance(value, dict):
            raise TelemetryError(f"{name} debe ser un objeto")

        voltage = _number(value.get("voltage_v"), f"{name}.voltage_v")
        current = _number(value.get("current_a"), f"{name}.current_a")
        transmitted_power = _number(
            value.get("power_w"), f"{name}.power_w", optional=True
        )

        return cls(
            voltage_v=voltage,
            current_a=current,
            power_w=transmitted_power if transmitted_power is not None else voltage * current,
            temp_c=_number(value.get("temp_c"), f"{name}.temp_c", optional=True),
            soc_pct=_number(value.get("soc_pct"), f"{name}.soc_pct", optional=True),
            connected=_boolean(value.get("connected"), True),
            valid=_boolean(value.get("valid"), True),
            age_ms=_milliseconds(value.get("age_ms"), f"{name}.age_ms"),
        )


@dataclass(frozen=True, slots=True)
class TelemetrySnapshot:
    seq: int
    speed_kmh: float
    rpm: float | None
    temp_c: float
    chassis_fault: bool
    bms1: BMSData
    bms2: BMSData
    received_at: float
    data_valid: bool = True
    rp2040_online: bool = True
    data_age_ms: int = 0

    @classmethod
    def from_json(cls, payload: bytes | str) -> "TelemetrySnapshot":
        try:
            text = payload.decode("utf-8") if isinstance(payload, bytes) else payload
            value = json.loads(text)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise TelemetryError(f"JSON invalido: {exc}") from exc

        if not isinstance(value, dict):
            raise TelemetryError("el payload principal debe ser un objeto")

        seq_value = _number(value.get("seq"), "seq")
        if seq_value < 0 or not seq_value.is_integer():
            raise TelemetryError("seq debe ser un entero positivo")

        return cls(
            seq=int(seq_value),
            speed_kmh=_number(value.get("speed_kmh"), "speed_kmh"),
            rpm=_number(value.get("rpm"), "rpm", optional=True),
            temp_c=_number(value.get("temp_c"), "temp_c"),
            chassis_fault=_boolean(value.get("chassis_fault"), False),
            bms1=BMSData.from_mapping(value.get("bms1"), "bms1"),
            bms2=BMSData.from_mapping(value.get("bms2"), "bms2"),
            received_at=time.monotonic(),
            data_valid=_boolean(value.get("data_valid"), True),
            rp2040_online=_boolean(value.get("rp2040_online"), True),
            data_age_ms=_milliseconds(
                value.get("data_age_ms", value.get("raw_age_ms")),
                "data_age_ms",
            ),
        )

