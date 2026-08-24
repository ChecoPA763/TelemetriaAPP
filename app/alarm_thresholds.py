"""Umbrales de alarma configurables para la interfaz Lobos Racing.

Los valores por defecto fueron definidos por Checo (2026-08-19):
- Temperatura del kart y de ambos BMS: amarillo >= 60 C, rojo >= 80 C.
- Carga (SoC) de ambos BMS: amarillo <= 50 %, rojo <= 20 %.
- Voltaje y corriente: sin alarma por defecto. Un pack de litio no baja de
  voltaje de forma lineal con la carga, asi que un umbral de voltaje fijo
  seria impreciso; el soc_pct que calcula el BMS es la fuente confiable.

Estos valores no estan escritos directamente en los widgets: viven aqui,
en un solo lugar, para poder exponerlos despues en una pantalla de Ajustes
sin tocar la logica de las tarjetas.
"""

from __future__ import annotations

from dataclasses import dataclass


class AlarmLevel:
    OK = "ok"
    WARN = "warn"
    CRIT = "crit"


@dataclass
class Thresholds:
    """Umbral de advertencia/critico para una variable que sube (temperatura)
    o para una variable que baja (SoC), segun `higher_is_worse`."""

    warn: float | None
    crit: float | None
    higher_is_worse: bool = True

    def level(self, value: float | None) -> str:
        if value is None or self.warn is None or self.crit is None:
            return AlarmLevel.OK
        if self.higher_is_worse:
            if value >= self.crit:
                return AlarmLevel.CRIT
            if value >= self.warn:
                return AlarmLevel.WARN
            return AlarmLevel.OK
        else:
            if value <= self.crit:
                return AlarmLevel.CRIT
            if value <= self.warn:
                return AlarmLevel.WARN
            return AlarmLevel.OK


@dataclass
class AlarmThresholds:
    """Conjunto editable de umbrales usados por toda la app."""

    temp_kart: Thresholds
    temp_bms: Thresholds
    soc_bms: Thresholds

    @classmethod
    def defaults(cls) -> "AlarmThresholds":
        return cls(
            temp_kart=Thresholds(warn=60.0, crit=80.0, higher_is_worse=True),
            temp_bms=Thresholds(warn=60.0, crit=80.0, higher_is_worse=True),
            soc_bms=Thresholds(warn=50.0, crit=20.0, higher_is_worse=False),
        )

