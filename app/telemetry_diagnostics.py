"""Calculos de diagnostico que no dependen de la interfaz Kivy."""

from __future__ import annotations

from collections import deque
from enum import Enum


class Freshness(Enum):
    FRESH = "fresh"
    DELAYED = "delayed"
    STALE = "stale"


def classify_freshness(
    age_ms: int | float,
    *,
    valid: bool = True,
    delayed_after_ms: int = 1000,
    stale_after_ms: int = 3000,
) -> Freshness:
    """Clasifica la vigencia de un dato usando su edad acumulada."""

    age = float(age_ms)
    if age < 0:
        raise ValueError("age_ms no puede ser negativo")
    if delayed_after_ms < 0 or stale_after_ms <= delayed_after_ms:
        raise ValueError("umbrales de edad invalidos")
    if not valid or age >= stale_after_ms:
        return Freshness.STALE
    if age >= delayed_after_ms:
        return Freshness.DELAYED
    return Freshness.FRESH


class PacketFrequencyMeter:
    """Promedia el intervalo de los paquetes recientes y devuelve Hz."""

    def __init__(self, window_size: int = 12) -> None:
        if window_size < 2:
            raise ValueError("window_size debe ser al menos 2")
        self._timestamps: deque[float] = deque(maxlen=window_size)

    def reset(self) -> None:
        self._timestamps.clear()

    def add(self, received_at: float) -> float | None:
        timestamp = float(received_at)
        if self._timestamps and timestamp <= self._timestamps[-1]:
            return self.frequency_hz
        self._timestamps.append(timestamp)
        return self.frequency_hz

    @property
    def frequency_hz(self) -> float | None:
        if len(self._timestamps) < 2:
            return None
        elapsed = self._timestamps[-1] - self._timestamps[0]
        if elapsed <= 0:
            return None
        return (len(self._timestamps) - 1) / elapsed


