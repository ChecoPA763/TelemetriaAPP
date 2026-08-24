"""Interfaz Android/Windows de telemetria Lobos Racing (v1.1.1).

Reorganiza la app en 5 pantallas navegables por swipe: General, BMS1, BMS2,
Telemetria y Registro, segun el diseno documentado en
docs/diseno/objetivo_interfaz_telemetria.md.

No modifica el contrato MQTT/JSON ni el modelo de datos existente
(telemetry.py, mqtt_client.py se usan tal cual).
"""

from __future__ import annotations

import os
import time

from kivy.app import App
from kivy.clock import Clock
from kivy.core.window import Window
from kivy.graphics import Color, Line, RoundedRectangle
from kivy.metrics import dp, sp
from kivy.properties import ListProperty, StringProperty
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.button import Button
from kivy.uix.gridlayout import GridLayout
from kivy.uix.label import Label
from kivy.uix.screenmanager import Screen, ScreenManager, SlideTransition
from kivy.uix.scrollview import ScrollView
from kivy.uix.textinput import TextInput
from kivy.utils import platform

from alarm_thresholds import AlarmLevel, AlarmThresholds
from csv_exporter import CsvExporter
from mqtt_client import TelemetryMQTTClient
from session_recorder import SessionRecorder, SessionSummary
from telemetry import BMSData, TelemetrySnapshot
from telemetry_diagnostics import Freshness, PacketFrequencyMeter, classify_freshness


MQTT_HOST = "b940a7d4912449ec9b61efdfbace2a91.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "LobosRacing"
MQTT_TOPIC = "lobos/kart01/telemetry"
STALE_AFTER_SECONDS = 3.0

SCREEN_ORDER = ["general", "bms1", "bms2", "telemetria", "registro"]
SCREEN_TITLES = {
    "general": "GENERAL",
    "bms1": "BMS 1",
    "bms2": "BMS 2",
    "telemetria": "TELEMETRÍA",
    "registro": "REGISTRO",
}
# Etiquetas cortas para las pestañas (5 caben en el ancho de un teléfono sin
# desbordar el texto); el título completo se sigue usando en SCREEN_TITLES
# para otros usos (ej. mensajes).
TAB_LABELS = {
    "general": "GENERAL",
    "bms1": "BMS 1",
    "bms2": "BMS 2",
    "telemetria": "TELEM.",
    "registro": "REGISTRO",
}

BG = (0.035, 0.047, 0.063, 1)
CARD = (0.065, 0.082, 0.106, 1)
CARD_ALT = (0.052, 0.068, 0.090, 1)
BORDER = (0.15, 0.18, 0.23, 1)
TEXT = (0.94, 0.95, 0.97, 1)
MUTED = (0.53, 0.58, 0.65, 1)
RED = (0.90, 0.14, 0.20, 1)
GREEN = (0.20, 0.78, 0.49, 1)
YELLOW = (0.96, 0.65, 0.18, 1)
BLUE = (0.23, 0.57, 0.92, 1)
CYAN = (0.16, 0.73, 0.68, 1)

ALARM_COLOR = {
    AlarmLevel.OK: TEXT,
    AlarmLevel.WARN: YELLOW,
    AlarmLevel.CRIT: RED,
}


def android_status_bar_inset() -> float:
    """Altura en pixeles de la barra superior, con fallback conservador."""

    if platform != "android":
        return 0.0
    try:
        from jnius import autoclass

        PythonActivity = autoclass("org.kivy.android.PythonActivity")
        resources = PythonActivity.mActivity.getResources()
        resource_id = resources.getIdentifier("status_bar_height", "dimen", "android")
        if resource_id:
            return float(resources.getDimensionPixelSize(resource_id))
    except Exception:
        pass
    return dp(24)


# ---------------------------------------------------------------------------
# Widgets reutilizables
# ---------------------------------------------------------------------------


class Card(BoxLayout):
    background_color = ListProperty(CARD)
    border_color = ListProperty(BORDER)

    def __init__(self, radius=18, **kwargs):
        super().__init__(**kwargs)
        self._radius = dp(radius)
        with self.canvas.before:
            self._bg_color = Color(*self.background_color)
            self._bg = RoundedRectangle(pos=self.pos, size=self.size, radius=[self._radius])
            self._border_color = Color(*self.border_color)
            self._border = Line(
                rounded_rectangle=(self.x, self.y, self.width, self.height, self._radius),
                width=1,
            )
        self.bind(pos=self._update_canvas, size=self._update_canvas)
        self.bind(background_color=self._update_colors, border_color=self._update_colors)

    def _update_canvas(self, *_):
        self._bg.pos = self.pos
        self._bg.size = self.size
        self._border.rounded_rectangle = (
            self.x,
            self.y,
            self.width,
            self.height,
            self._radius,
        )

    def _update_colors(self, *_):
        self._bg_color.rgba = self.background_color
        self._border_color.rgba = self.border_color


class StatusPill(Label):
    pill_color = ListProperty((0.25, 0.28, 0.32, 1))

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.size_hint = (None, None)
        self.height = dp(32)
        self.padding = (dp(13), 0)
        self.bind(texture_size=self._fit_width)
        with self.canvas.before:
            self._pill = Color(*self.pill_color)
            self._pill_rect = RoundedRectangle(pos=self.pos, size=self.size, radius=[dp(16)])
        self.bind(pos=self._update_canvas, size=self._update_canvas)
        self.bind(pill_color=self._update_color)

    def _fit_width(self, *_):
        self.width = self.texture_size[0] + dp(26)

    def _update_canvas(self, *_):
        self._pill_rect.pos = self.pos
        self._pill_rect.size = self.size

    def _update_color(self, *_):
        self._pill.rgba = self.pill_color


def text_label(text="", *, size=14, color=TEXT, bold=False, **kwargs):
    label = Label(
        text=text,
        color=color,
        font_size=sp(size),
        bold=bold,
        halign=kwargs.pop("halign", "left"),
        valign=kwargs.pop("valign", "middle"),
        **kwargs,
    )
    label.bind(size=lambda widget, value: setattr(widget, "text_size", value))
    return label


class MiniMetric(BoxLayout):
    def __init__(self, title: str, value: str = "--", accent=TEXT, **kwargs):
        super().__init__(orientation="vertical", spacing=dp(1), **kwargs)
        self.title_label = text_label(title, size=10, color=MUTED, bold=True)
        self.value_label = text_label(value, size=17, color=accent, bold=True)
        self.add_widget(self.title_label)
        self.add_widget(self.value_label)

    def set_value(self, value: str, color=None):
        self.value_label.text = value
        if color is not None:
            self.value_label.color = color


class SocBar(BoxLayout):
    def __init__(self, accent=CYAN, **kwargs):
        super().__init__(size_hint_y=None, height=dp(7), **kwargs)
        self._ratio = 0.0
        self._accent = accent
        with self.canvas.before:
            Color(0.12, 0.14, 0.18, 1)
            self._bg = RoundedRectangle(radius=[dp(4)])
            self._fill_color = Color(*accent)
            self._fill = RoundedRectangle(radius=[dp(4)])
        self.bind(pos=self._update, size=self._update)

    def set_ratio(self, ratio: float, color=None):
        self._ratio = max(0.0, min(1.0, ratio))
        if color is not None:
            self._fill_color.rgba = color
        self._update()

    def _update(self, *_):
        self._bg.pos = self.pos
        self._bg.size = self.size
        self._fill.pos = self.pos
        self._fill.size = (self.width * self._ratio, self.height)


class BMSMiniCard(Card):
    """Tarjeta completa de BMS usada en General y en las pantallas dedicadas."""

    def __init__(self, title: str, accent, **kwargs):
        super().__init__(orientation="vertical", padding=dp(15), spacing=dp(8), **kwargs)
        self.accent = accent

        header = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(6))
        header.add_widget(text_label(title, size=14, bold=True))
        self.state_label = text_label(
            "SIN DATOS", size=10, color=MUTED, bold=True, halign="right"
        )
        header.add_widget(self.state_label)
        self.add_widget(header)

        row1 = GridLayout(cols=3, spacing=dp(8), size_hint_y=None, height=dp(38))
        self.voltage = MiniMetric("VOLTAJE", "-- V", accent)
        self.current = MiniMetric("CORRIENTE", "-- A", accent)
        self.power = MiniMetric("POTENCIA", "-- W")
        row1.add_widget(self.voltage)
        row1.add_widget(self.current)
        row1.add_widget(self.power)
        self.add_widget(row1)

        row2 = GridLayout(cols=2, spacing=dp(8), size_hint_y=None, height=dp(38))
        self.temp = MiniMetric("TEMP.", "-- °C")
        self.soc = MiniMetric("CARGA", "-- %")
        row2.add_widget(self.temp)
        row2.add_widget(self.soc)
        self.add_widget(row2)

        self.soc_bar = SocBar(accent=accent)
        self.add_widget(self.soc_bar)

    def show_data(
        self, data: BMSData, thresholds: AlarmThresholds, transport_age_ms: int = 0
    ):
        freshness = classify_freshness(
            data.age_ms + transport_age_ms, valid=data.connected and data.valid
        )
        if freshness is Freshness.STALE:
            self.show_stale()
            return
        self.state_label.text = (
            "RETRASADO" if freshness is Freshness.DELAYED else "CONECTADO"
        )
        self.state_label.color = (
            YELLOW if freshness is Freshness.DELAYED else GREEN
        )

        self.voltage.set_value(f"{data.voltage_v:.1f} V")
        self.current.set_value(f"{data.current_a:.1f} A")
        self.power.set_value(f"{data.power_w:.0f} W")

        temp_level = thresholds.temp_bms.level(data.temp_c)
        temp_color = ALARM_COLOR[temp_level]
        self.temp.set_value(
            "-- °C" if data.temp_c is None else f"{data.temp_c:.1f} °C", temp_color
        )

        soc_level = thresholds.soc_bms.level(data.soc_pct)
        soc_color = ALARM_COLOR[soc_level]
        self.soc.set_value(
            "-- %" if data.soc_pct is None else f"{data.soc_pct:.1f} %", soc_color
        )
        ratio = 0.0 if data.soc_pct is None else data.soc_pct / 100.0
        bar_color = soc_color if soc_level != AlarmLevel.OK else self.accent
        self.soc_bar.set_ratio(ratio, bar_color)

    def show_stale(self):
        self.state_label.text = "SIN DATOS"
        self.state_label.color = RED
        for metric, suffix in (
            (self.voltage, "V"),
            (self.current, "A"),
            (self.power, "W"),
            (self.temp, "°C"),
            (self.soc, "%"),
        ):
            metric.set_value(f"-- {suffix}", MUTED)
        self.soc_bar.set_ratio(0.0, MUTED)


class BMSDetailCard(Card):
    """Tarjeta de detalle usada en las pantallas BMS1/BMS2."""

    def __init__(self, title: str, accent, **kwargs):
        super().__init__(orientation="vertical", padding=dp(16), spacing=dp(10), **kwargs)
        self.accent = accent

        header = BoxLayout(size_hint_y=None, height=dp(28), spacing=dp(6))
        header.add_widget(text_label(f"{title} - DETALLE", size=14, bold=True))
        self.badge_label = text_label(
            "CONECTADO", size=10, color=GREEN, bold=True, halign="right"
        )
        header.add_widget(self.badge_label)
        self.add_widget(header)

        grid = GridLayout(cols=2, spacing=dp(10), size_hint_y=None, height=dp(96))
        self.voltage = MiniMetric("VOLTAJE", "-- V", accent)
        self.current = MiniMetric("CORRIENTE", "-- A", accent)
        self.power = MiniMetric("POTENCIA", "-- W")
        self.temp = MiniMetric("TEMPERATURA", "-- °C")
        grid.add_widget(self.voltage)
        grid.add_widget(self.current)
        grid.add_widget(self.power)
        grid.add_widget(self.temp)
        self.add_widget(grid)

        self.alarm_note = text_label("", size=11, color=YELLOW, size_hint_y=None, height=dp(0))
        self.add_widget(self.alarm_note)

        self.add_widget(text_label("CARGA (SoC)", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(16)))
        self.soc_bar = SocBar(accent=accent)
        self.add_widget(self.soc_bar)
        self.soc_value = text_label("-- %", size=11, color=MUTED, halign="right", size_hint_y=None, height=dp(18))
        self.add_widget(self.soc_value)

    def show_data(
        self, data: BMSData, thresholds: AlarmThresholds, transport_age_ms: int = 0
    ):
        freshness = classify_freshness(
            data.age_ms + transport_age_ms, valid=data.connected and data.valid
        )
        if freshness is Freshness.STALE:
            self.show_stale()
            return
        temp_level = thresholds.temp_bms.level(data.temp_c)
        soc_level = thresholds.soc_bms.level(data.soc_pct)

        if temp_level == AlarmLevel.CRIT:
            self.badge_label.text = "TEMP. CRÍTICA"
            self.badge_label.color = RED
        elif temp_level == AlarmLevel.WARN:
            self.badge_label.text = "TEMP. ALTA"
            self.badge_label.color = YELLOW
        elif soc_level != AlarmLevel.OK:
            self.badge_label.text = "CARGA BAJA"
            self.badge_label.color = ALARM_COLOR[soc_level]
        elif freshness is Freshness.DELAYED:
            self.badge_label.text = "RETRASADO"
            self.badge_label.color = YELLOW
        else:
            self.badge_label.text = "CONECTADO"
            self.badge_label.color = GREEN

        self.voltage.set_value(f"{data.voltage_v:.1f} V")
        self.current.set_value(f"{data.current_a:.1f} A")
        self.power.set_value(f"{data.power_w:.0f} W")

        temp_color = ALARM_COLOR[temp_level]
        self.temp.set_value(
            "-- °C" if data.temp_c is None else f"{data.temp_c:.1f} °C", temp_color
        )
        if temp_level != AlarmLevel.OK and data.temp_c is not None:
            threshold = thresholds.temp_bms
            limit = threshold.crit if temp_level == AlarmLevel.CRIT else threshold.warn
            self.alarm_note.text = f"Sobre el umbral ({limit:.0f} °C) - configurable en Ajustes"
        else:
            self.alarm_note.text = ""

        ratio = 0.0 if data.soc_pct is None else data.soc_pct / 100.0
        soc_color = ALARM_COLOR[soc_level]
        bar_color = soc_color if soc_level != AlarmLevel.OK else self.accent
        self.soc_bar.set_ratio(ratio, bar_color)
        if data.soc_pct is None:
            self.soc_value.text = "-- %"
            self.soc_value.color = MUTED
        else:
            note = ""
            if soc_level == AlarmLevel.WARN:
                note = f" - por debajo del {thresholds.soc_bms.warn:.0f}%"
            elif soc_level == AlarmLevel.CRIT:
                note = f" - por debajo del {thresholds.soc_bms.crit:.0f}%"
            self.soc_value.text = f"{data.soc_pct:.1f} %{note}"
            self.soc_value.color = soc_color

    def show_stale(self):
        self.badge_label.text = "SIN DATOS"
        self.badge_label.color = RED
        for metric, suffix in (
            (self.voltage, "V"),
            (self.current, "A"),
            (self.power, "W"),
            (self.temp, "°C"),
        ):
            metric.set_value(f"-- {suffix}", MUTED)
        self.alarm_note.text = ""
        self.soc_bar.set_ratio(0.0, MUTED)
        self.soc_value.text = "-- %"
        self.soc_value.color = MUTED


def advanced_data_placeholder() -> Card:
    """Bloque 'Datos avanzados' con campos futuros aun no enviados por el firmware."""

    card = Card(
        orientation="vertical",
        padding=dp(15),
        spacing=dp(6),
        size_hint_y=None,
        height=dp(150),
        background_color=CARD_ALT,
    )
    card.add_widget(text_label("DATOS AVANZADOS (FUTURO)", size=10, color=MUTED, bold=True))
    for label_text in (
        "Voltaje por celda",
        "Diferencia máx. entre celdas",
        "Estado de alarmas BMS",
    ):
        row = BoxLayout(size_hint_y=None, height=dp(28))
        row.add_widget(text_label(label_text, size=11, color=MUTED))
        row.add_widget(text_label("No disponible", size=10, color=(0.36, 0.39, 0.44, 1), halign="right"))
        card.add_widget(row)
    return card


# ---------------------------------------------------------------------------
# Pantallas
# ---------------------------------------------------------------------------


class TopBar(BoxLayout):
    """Encabezado + pestañas, compartido por las 5 pantallas."""

    def __init__(self, dashboard: "TelemetryDashboard", **kwargs):
        safe_top = android_status_bar_inset()
        super().__init__(
            orientation="vertical", size_hint_y=None, height=dp(96) + safe_top, **kwargs
        )
        self.dashboard = dashboard

        header = BoxLayout(
            size_hint_y=None,
            height=dp(54) + safe_top,
            padding=(dp(14), safe_top, dp(14), 0),
            spacing=dp(8),
        )
        titles = BoxLayout(orientation="vertical")
        titles.add_widget(text_label("LOBOS RACING", size=19, bold=True))
        titles.add_widget(text_label("TELEMETRÍA", size=10, color=MUTED, bold=True))
        header.add_widget(titles)

        self.status_pill = StatusPill(text="DESCONECTADO", color=TEXT, font_size=sp(10), bold=True)
        header.add_widget(self.status_pill)
        self.add_widget(header)

        tabs = BoxLayout(size_hint_y=None, height=dp(38), padding=(dp(4), 0))
        self.tab_buttons: dict[str, Button] = {}
        for name in SCREEN_ORDER:
            btn = Button(
                text=TAB_LABELS[name],
                font_size=sp(9.5),
                bold=True,
                background_normal="",
                background_down="",
                background_color=(0, 0, 0, 0),
                color=TEXT,
                halign="center",
                valign="middle",
                shorten=True,
                shorten_from="right",
                padding=(dp(2), 0),
            )
            btn.bind(size=lambda w, _v: setattr(w, "text_size", w.size))
            btn.bind(on_release=lambda _w, n=name: self.dashboard.go_to_screen(n))
            self.tab_buttons[name] = btn
            tabs.add_widget(btn)
        self.add_widget(tabs)

    def set_active(self, name: str):
        for key, btn in self.tab_buttons.items():
            btn.color = TEXT if key == name else MUTED

    def set_status(self, text: str, color):
        self.status_pill.text = text
        self.status_pill.pill_color = color


class BaseScreen(Screen):
    """Pantalla con scroll vertical y una TopBar compartida arriba."""

    def __init__(self, dashboard: "TelemetryDashboard", **kwargs):
        super().__init__(**kwargs)
        self.dashboard = dashboard
        self._swipe_start: tuple[float, float] | None = None

        root = BoxLayout(orientation="vertical")
        root.add_widget(dashboard.build_topbar_for(self))

        self.scroll = ScrollView(do_scroll_x=False, bar_width=dp(3))
        self.content = BoxLayout(
            orientation="vertical",
            spacing=dp(11),
            size_hint_y=None,
            padding=(dp(14), dp(11), dp(14), dp(14)),
        )
        self.content.bind(minimum_height=self.content.setter("height"))
        self.scroll.add_widget(self.content)
        root.add_widget(self.scroll)
        self.add_widget(root)

    def on_touch_down(self, touch):
        if self.collide_point(*touch.pos):
            self._swipe_start = touch.pos
        return super().on_touch_down(touch)

    def on_touch_up(self, touch):
        start = self._swipe_start
        self._swipe_start = None
        if start is not None:
            delta_x = touch.x - start[0]
            delta_y = touch.y - start[1]
            if abs(delta_x) >= dp(60) and abs(delta_x) > abs(delta_y) * 1.2:
                current_index = SCREEN_ORDER.index(self.name)
                target_index = current_index + (-1 if delta_x > 0 else 1)
                if 0 <= target_index < len(SCREEN_ORDER):
                    self.dashboard.go_to_screen(SCREEN_ORDER[target_index])
                    return True
        return super().on_touch_up(touch)


class GeneralScreen(BaseScreen):
    def __init__(self, dashboard: "TelemetryDashboard", **kwargs):
        super().__init__(dashboard, name="general", **kwargs)

        self.conn_card = Card(
            orientation="horizontal",
            size_hint_y=None,
            height=dp(62),
            padding=dp(10),
            spacing=dp(8),
            background_color=CARD_ALT,
        )
        self.password_input = TextInput(
            hint_text="Contraseña MQTT",
            password=True,
            multiline=False,
            write_tab=False,
            size_hint_y=None,
            height=dp(42),
            padding=(dp(12), dp(11)),
            background_normal="",
            background_active="",
            background_color=(0.025, 0.035, 0.05, 1),
            foreground_color=TEXT,
            hint_text_color=MUTED,
            cursor_color=RED,
            disabled_foreground_color=GREEN,
            font_size=sp(14),
        )
        env_password = os.environ.get("LOBOS_MQTT_PASSWORD", "")
        if env_password:
            self.password_input.text = env_password

        self.connect_button = Button(
            text="CONECTAR",
            size_hint=(None, None),
            size=(dp(112), dp(42)),
            background_normal="",
            background_down="",
            background_color=RED,
            color=(1, 1, 1, 1),
            bold=True,
            font_size=sp(12),
        )
        self.connect_button.bind(on_release=lambda *_: dashboard.toggle_connection())
        self.password_input.bind(on_text_validate=lambda *_: dashboard.toggle_connection())
        self.conn_card.add_widget(self.password_input)
        self.conn_card.add_widget(self.connect_button)
        self.content.add_widget(self.conn_card)
        self.connection_error_label = text_label(
            "", size=10, color=RED, size_hint_y=None, height=dp(20)
        )
        self.content.add_widget(self.connection_error_label)

        speed_card = Card(
            orientation="vertical",
            size_hint_y=None,
            height=dp(200),
            padding=dp(16),
            spacing=dp(2),
        )
        speed_card.add_widget(text_label("VELOCIDAD", size=11, color=MUTED, bold=True, size_hint_y=None, height=dp(22)))
        self.speed_label = text_label("--", size=64, bold=True, halign="center")
        speed_card.add_widget(self.speed_label)
        speed_card.add_widget(text_label("km/h", size=14, color=MUTED, bold=True, halign="center", size_hint_y=None, height=dp(26)))

        summary = GridLayout(cols=3, size_hint_y=None, height=dp(46), spacing=dp(8))
        self.temp_metric = MiniMetric("TEMP.", "-- °C")
        self.chassis_metric = MiniMetric("CHASIS", "--", GREEN)
        self.signal_metric = MiniMetric("SEÑAL", "--", MUTED)
        summary.add_widget(self.temp_metric)
        summary.add_widget(self.chassis_metric)
        summary.add_widget(self.signal_metric)
        speed_card.add_widget(summary)
        self.content.add_widget(speed_card)

        self.bms1_card = BMSMiniCard("BMS 1", CYAN, size_hint_y=None, height=dp(178))
        self.bms2_card = BMSMiniCard("BMS 2", BLUE, size_hint_y=None, height=dp(178))
        self.content.add_widget(self.bms1_card)
        self.content.add_widget(self.bms2_card)


class BMSScreen(BaseScreen):
    def __init__(self, dashboard: "TelemetryDashboard", *, which: str, title: str, accent, **kwargs):
        super().__init__(dashboard, name=which, **kwargs)
        self.which = which
        self.detail_card = BMSDetailCard(title, accent, size_hint_y=None, height=dp(268))
        self.content.add_widget(self.detail_card)
        self.content.add_widget(advanced_data_placeholder())


class TelemetriaScreen(BaseScreen):
    def __init__(self, dashboard: "TelemetryDashboard", **kwargs):
        super().__init__(dashboard, name="telemetria", **kwargs)

        chart_card = Card(orientation="vertical", padding=dp(14), spacing=dp(8), size_hint_y=None, height=dp(158))
        chart_card.add_widget(text_label("VELOCIDAD - GRÁFICA", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(16)))
        placeholder = Card(
            size_hint_y=None,
            height=dp(110),
            background_color=CARD_ALT,
            border_color=BORDER,
        )
        placeholder.add_widget(
            text_label(
                "Gráfica histórica - pendiente para próxima iteración",
                size=10,
                color=(0.42, 0.46, 0.52, 1),
                halign="center",
                valign="middle",
            )
        )
        chart_card.add_widget(placeholder)
        self.content.add_widget(chart_card)

        extra = Card(size_hint_y=None, height=dp(64))
        row = GridLayout(cols=3, spacing=dp(8))
        self.rpm_metric = MiniMetric("RPM", "--", CYAN)
        self.temp_metric = MiniMetric("TEMP.", "-- °C")
        self.chassis_metric = MiniMetric("CHASIS", "--", GREEN)
        row.add_widget(self.rpm_metric)
        row.add_widget(self.temp_metric)
        row.add_widget(self.chassis_metric)
        extra.add_widget(row)
        self.content.add_widget(extra)

        diag = Card(
            orientation="vertical",
            size_hint_y=None,
            height=dp(214),
            padding=dp(13),
            spacing=dp(4),
            background_color=CARD_ALT,
        )
        diag.add_widget(text_label("DIAGNÓSTICO DE SEÑAL", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(18)))
        self.diag_labels: dict[str, Label] = {}
        for key, title in (
            ("seq", "Secuencia"),
            ("rx", "Paquetes recibidos"),
            ("lost", "Paquetes perdidos"),
            ("freq", "Frecuencia"),
            ("age", "Último paquete"),
            ("status", "Estado MQTT"),
        ):
            row = BoxLayout(size_hint_y=None, height=dp(20))
            row.add_widget(text_label(title, size=11, color=MUTED))
            value_label = text_label("--", size=11, color=TEXT, bold=True, halign="right")
            self.diag_labels[key] = value_label
            row.add_widget(value_label)
            diag.add_widget(row)
        self.error_label = text_label("", size=10, color=RED, size_hint_y=None, height=dp(16))
        diag.add_widget(self.error_label)
        self.content.add_widget(diag)

        future = Card(orientation="vertical", padding=dp(15), spacing=dp(6), size_hint_y=None, height=dp(100), background_color=CARD_ALT)
        future.add_widget(text_label("SENSORES FUTUROS", size=10, color=MUTED, bold=True))
        for label_text in ("GPS / mapa de circuito", "Tiempo de vuelta"):
            row = BoxLayout(size_hint_y=None, height=dp(24))
            row.add_widget(text_label(label_text, size=11, color=MUTED))
            row.add_widget(text_label("No disponible", size=10, color=(0.36, 0.39, 0.44, 1), halign="right"))
            future.add_widget(row)
        self.content.add_widget(future)


class RegistroScreen(BaseScreen):
    def __init__(self, dashboard: "TelemetryDashboard", **kwargs):
        super().__init__(dashboard, name="registro", **kwargs)

        self.session_card = Card(orientation="vertical", padding=dp(15), spacing=dp(8), size_hint_y=None, height=dp(156))
        self.session_card.add_widget(text_label("SESIÓN ACTUAL", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(16)))

        toggle_row = BoxLayout(size_hint_y=None, height=dp(50), spacing=dp(8))
        names_col = BoxLayout(orientation="vertical")
        self.session_name_label = text_label("Sin sesión activa", size=13, bold=True)
        self.session_status_label = text_label("Detenido", size=10, color=MUTED, bold=True)
        names_col.add_widget(self.session_name_label)
        names_col.add_widget(self.session_status_label)
        toggle_row.add_widget(names_col)

        self.record_button = Button(
            text="INICIAR",
            size_hint=(None, None),
            size=(dp(104), dp(40)),
            background_normal="",
            background_down="",
            background_color=RED,
            color=(1, 1, 1, 1),
            bold=True,
            font_size=sp(11),
        )
        self.record_button.bind(on_release=lambda *_: dashboard.toggle_recording())
        toggle_row.add_widget(self.record_button)
        self.session_card.add_widget(toggle_row)

        live_row = GridLayout(cols=3, spacing=dp(8), size_hint_y=None, height=dp(40))
        self.samples_metric = MiniMetric("MUESTRAS", "0")
        self.max_speed_metric = MiniMetric("V. MÁX.", "--", GREEN)
        self.max_temp_metric = MiniMetric("TEMP. MÁX.", "--")
        live_row.add_widget(self.samples_metric)
        live_row.add_widget(self.max_speed_metric)
        live_row.add_widget(self.max_temp_metric)
        self.session_card.add_widget(live_row)
        self.content.add_widget(self.session_card)

        self.saved_card = Card(orientation="vertical", padding=dp(15), spacing=dp(6), size_hint_y=None, height=dp(108), background_color=CARD_ALT)
        self.saved_card.add_widget(text_label("SESIONES GUARDADAS", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(16)))
        self.export_status_label = text_label(
            "", size=10, color=MUTED, size_hint_y=None, height=dp(20)
        )
        self.saved_card.add_widget(self.export_status_label)
        self.saved_list_box = BoxLayout(orientation="vertical", size_hint_y=None, spacing=dp(4))
        self.saved_list_box.bind(minimum_height=self.saved_list_box.setter("height"))
        self.saved_card.add_widget(self.saved_list_box)
        self.content.add_widget(self.saved_card)

        cols_card = Card(orientation="vertical", padding=dp(15), spacing=dp(4), size_hint_y=None, height=dp(110), background_color=CARD_ALT)
        cols_card.add_widget(text_label("COLUMNAS DEL CSV", size=10, color=MUTED, bold=True, size_hint_y=None, height=dp(16)))
        cols_card.add_widget(
            text_label(
                ", ".join(
                    [
                        "timestamp", "seq", "speed_kmh", "rpm", "temp_c", "chassis_fault",
                        "bms1_voltage_v", "bms1_current_a", "bms1_power_w", "bms1_temp_c",
                        "bms1_soc_pct", "bms2_voltage_v", "bms2_current_a", "bms2_power_w",
                        "bms2_temp_c", "bms2_soc_pct",
                    ]
                ),
                size=10,
                color=MUTED,
            )
        )
        self.content.add_widget(cols_card)

        self._empty_label = text_label(
            "Aún no hay sesiones guardadas.", size=11, color=MUTED, size_hint_y=None, height=dp(24)
        )
        self.saved_list_box.add_widget(self._empty_label)

    def render_saved_sessions(self, sessions: list[SessionSummary]):
        self.saved_list_box.clear_widgets()
        if not sessions:
            self.saved_list_box.add_widget(self._empty_label)
            self.saved_card.height = dp(108)
            return

        for summary in sessions:
            row = BoxLayout(size_hint_y=None, height=dp(48), spacing=dp(8))
            info = BoxLayout(orientation="vertical")
            info.add_widget(text_label(summary.name, size=12, bold=True))
            minutes = summary.duration_s / 60.0
            info.add_widget(
                text_label(
                    f"{summary.sample_count} muestras | {minutes:.1f} min",
                    size=10,
                    color=MUTED,
                )
            )
            row.add_widget(info)
            export_button = Button(
                text="EXPORTAR",
                size_hint=(None, None),
                size=(dp(92), dp(38)),
                background_normal="",
                background_down="",
                background_color=(0.10, 0.42, 0.48, 1),
                color=TEXT,
                bold=True,
                font_size=sp(10),
            )
            export_button.bind(
                on_release=lambda _button, path=summary.path: self.dashboard.export_session(path)
            )
            row.add_widget(export_button)
            self.saved_list_box.add_widget(row)

        self.saved_card.height = dp(74) + dp(52) * len(sessions)


# ---------------------------------------------------------------------------
# Dashboard: coordina pantallas, MQTT y grabacion
# ---------------------------------------------------------------------------


class TelemetryDashboard(BoxLayout):
    error_text = StringProperty("")

    def __init__(self, sessions_dir: str | None = None, **kwargs):
        super().__init__(orientation="vertical", **kwargs)
        self.mqtt_state = "disconnected"
        self.last_snapshot: TelemetrySnapshot | None = None
        self.last_seq: int | None = None
        self.packets_received = 0
        self.packets_lost = 0
        self._stale_rendered = True
        self.frequency_meter = PacketFrequencyMeter(window_size=12)
        self.thresholds = AlarmThresholds.defaults()
        self.recorder = SessionRecorder(sessions_dir=sessions_dir) if sessions_dir else SessionRecorder()
        self.csv_exporter = CsvExporter()

        self.mqtt = TelemetryMQTTClient(
            on_telemetry=self._telemetry_from_thread,
            on_status=self._status_from_thread,
            on_error=self._error_from_thread,
        )

        self.manager = ScreenManager(transition=SlideTransition(duration=0.18))
        self._topbars: list[TopBar] = []

        self.general_screen = GeneralScreen(self)
        self.bms1_screen = BMSScreen(self, which="bms1", title="BMS 1", accent=CYAN)
        self.bms2_screen = BMSScreen(self, which="bms2", title="BMS 2", accent=BLUE)
        self.telemetria_screen = TelemetriaScreen(self)
        self.registro_screen = RegistroScreen(self)

        for screen in (
            self.general_screen,
            self.bms1_screen,
            self.bms2_screen,
            self.telemetria_screen,
            self.registro_screen,
        ):
            self.manager.add_widget(screen)

        self.add_widget(self.manager)
        self._set_active_tab("general")

        Clock.schedule_interval(self._refresh_health, 0.25)
        Clock.schedule_interval(self._refresh_recording_ui, 0.5)
        self._refresh_saved_sessions()

    # -- Barra superior compartida ------------------------------------------------

    def build_topbar_for(self, _screen: Screen) -> TopBar:
        topbar = TopBar(self)
        self._topbars.append(topbar)
        return topbar

    def _set_active_tab(self, name: str):
        for topbar in self._topbars:
            topbar.set_active(name)

    def go_to_screen(self, name: str):
        if name not in SCREEN_ORDER:
            return
        current = self.manager.current
        if current == name:
            return
        current_index = SCREEN_ORDER.index(current) if current in SCREEN_ORDER else 0
        target_index = SCREEN_ORDER.index(name)
        self.manager.transition.direction = "left" if target_index > current_index else "right"
        self.manager.current = name
        self._set_active_tab(name)
        if name == "registro":
            self._refresh_saved_sessions()

    # -- Conexion MQTT --------------------------------------------------------

    def toggle_connection(self):
        screen = self.general_screen
        if self.mqtt_state in ("connected", "connecting", "retrying"):
            self.mqtt.stop()
            self._apply_status("disconnected", "Desconectado")
            return

        password = screen.password_input.text
        if not password:
            self._show_error("Escribe la contraseña MQTT de LobosRacing.")
            return
        self._show_error("")
        self.mqtt.connect(MQTT_HOST, MQTT_PORT, MQTT_USER, password, MQTT_TOPIC)

    def _telemetry_from_thread(self, snapshot: TelemetrySnapshot):
        Clock.schedule_once(lambda _dt, data=snapshot: self._show_snapshot(data), 0)

    def _status_from_thread(self, state: str, message: str):
        Clock.schedule_once(lambda _dt, s=state, m=message: self._apply_status(s, m), 0)

    def _error_from_thread(self, message: str):
        Clock.schedule_once(lambda _dt, msg=message: self._show_error(msg), 0)

    def _apply_status(self, state: str, message: str):
        self.mqtt_state = state
        colors = {
            "connected": GREEN,
            "connecting": YELLOW,
            "retrying": YELLOW,
            "error": RED,
            "disconnected": (0.27, 0.30, 0.35, 1),
        }
        text = message.upper()
        color = colors.get(state, MUTED)
        for topbar in self._topbars:
            topbar.set_status(text, color)

        connected = state in ("connected", "connecting", "retrying")
        screen = self.general_screen
        screen.connect_button.text = "SALIR" if connected else "CONECTAR"
        screen.connect_button.background_color = (0.25, 0.29, 0.35, 1) if connected else RED
        screen.password_input.disabled = connected
        if connected:
            screen.password_input.text = ""
            screen.password_input.hint_text = "MQTT configurado"
            screen.password_input.background_color = (0, 0, 0, 0)
        else:
            screen.password_input.hint_text = "Contraseña MQTT"
            screen.password_input.background_color = (0.025, 0.035, 0.05, 1)

        if state in ("disconnected", "error"):
            self.frequency_meter.reset()
            self.telemetria_screen.diag_labels["freq"].text = "--"

        self.telemetria_screen.diag_labels["status"].text = message.upper()
        self.telemetria_screen.diag_labels["status"].color = color if state != "disconnected" else MUTED

    # -- Datos de telemetria ----------------------------------------------------

    def _show_snapshot(self, snapshot: TelemetrySnapshot):
        if self.last_seq is not None and snapshot.seq > self.last_seq + 1:
            self.packets_lost += snapshot.seq - self.last_seq - 1
        self.last_seq = snapshot.seq
        self.last_snapshot = snapshot
        self.packets_received += 1
        self._stale_rendered = False
        frequency_hz = self.frequency_meter.add(snapshot.received_at)

        self._render_general(snapshot)
        self._render_bms_screens(snapshot)
        self._render_telemetria(snapshot, frequency_hz)
        self._refresh_health(0)
        self._show_error("")

        freshness = classify_freshness(
            self._effective_age_ms(snapshot), valid=snapshot.data_valid
        )
        if (
            self.recorder.is_recording
            and snapshot.rp2040_online
            and freshness is not Freshness.STALE
        ):
            self.recorder.record(snapshot)

    def _render_general(self, snapshot: TelemetrySnapshot):
        screen = self.general_screen
        screen.speed_label.text = f"{snapshot.speed_kmh:.1f}"
        screen.speed_label.color = TEXT

        temp_level = self.thresholds.temp_kart.level(snapshot.temp_c)
        screen.temp_metric.set_value(f"{snapshot.temp_c:.1f} °C", ALARM_COLOR[temp_level])
        screen.chassis_metric.set_value(
            "FALLA" if snapshot.chassis_fault else "OK",
            RED if snapshot.chassis_fault else GREEN,
        )
        freshness = classify_freshness(
            snapshot.data_age_ms, valid=snapshot.data_valid
        )
        if not snapshot.rp2040_online:
            screen.signal_metric.set_value("RP OFFLINE", YELLOW)
        elif freshness is Freshness.DELAYED:
            screen.signal_metric.set_value("RETRASO", YELLOW)
        elif freshness is Freshness.STALE:
            screen.signal_metric.set_value("VENCIDA", RED)
        else:
            screen.signal_metric.set_value("OK", GREEN)

        screen.bms1_card.show_data(
            snapshot.bms1, self.thresholds, snapshot.data_age_ms
        )
        screen.bms2_card.show_data(
            snapshot.bms2, self.thresholds, snapshot.data_age_ms
        )

    def _render_bms_screens(self, snapshot: TelemetrySnapshot):
        self.bms1_screen.detail_card.show_data(
            snapshot.bms1, self.thresholds, snapshot.data_age_ms
        )
        self.bms2_screen.detail_card.show_data(
            snapshot.bms2, self.thresholds, snapshot.data_age_ms
        )

    def _render_telemetria(self, snapshot: TelemetrySnapshot, frequency_hz: float | None):
        screen = self.telemetria_screen
        screen.rpm_metric.set_value("--" if snapshot.rpm is None else f"{snapshot.rpm:.0f}", CYAN)
        temp_level = self.thresholds.temp_kart.level(snapshot.temp_c)
        screen.temp_metric.set_value(f"{snapshot.temp_c:.1f} °C", ALARM_COLOR[temp_level])
        screen.chassis_metric.set_value(
            "FALLA" if snapshot.chassis_fault else "OK",
            RED if snapshot.chassis_fault else GREEN,
        )
        screen.diag_labels["seq"].text = f"#{snapshot.seq}"
        screen.diag_labels["rx"].text = str(self.packets_received)
        screen.diag_labels["lost"].text = str(self.packets_lost)
        screen.diag_labels["freq"].text = (
            "--" if frequency_hz is None else f"{frequency_hz:.2f} Hz"
        )

    @staticmethod
    def _effective_age_ms(snapshot: TelemetrySnapshot) -> int:
        local_age_ms = max(
            0, int((time.monotonic() - snapshot.received_at) * 1000)
        )
        return snapshot.data_age_ms + local_age_ms

    def _clear_stale_values(self):
        gscreen = self.general_screen
        gscreen.speed_label.text = "--"
        gscreen.speed_label.color = MUTED
        gscreen.temp_metric.set_value("-- °C", MUTED)
        gscreen.chassis_metric.set_value("--", MUTED)
        gscreen.signal_metric.set_value("VENCIDA", RED)
        gscreen.bms1_card.show_stale()
        gscreen.bms2_card.show_stale()
        self.bms1_screen.detail_card.show_stale()
        self.bms2_screen.detail_card.show_stale()
        self.telemetria_screen.rpm_metric.set_value("--", MUTED)
        self.telemetria_screen.temp_metric.set_value("-- °C", MUTED)
        self.telemetria_screen.chassis_metric.set_value("--", MUTED)

    def _refresh_health(self, _dt):
        screen = self.telemetria_screen
        if self.last_snapshot is None:
            return

        age_ms = self._effective_age_ms(self.last_snapshot)
        freshness = classify_freshness(
            age_ms, valid=self.last_snapshot.data_valid
        )
        screen.diag_labels["age"].text = f"{age_ms / 1000.0:.1f} s"
        screen.diag_labels["age"].color = {
            Freshness.FRESH: GREEN,
            Freshness.DELAYED: YELLOW,
            Freshness.STALE: RED,
        }[freshness]

        if freshness is Freshness.STALE:
            if not self._stale_rendered:
                self._stale_rendered = True
                self._clear_stale_values()
            status_text, status_color = "SIN TELEMETRÍA VÁLIDA", RED
        elif not self.last_snapshot.rp2040_online:
            self._stale_rendered = False
            status_text, status_color = "RP2040 SIN RESPUESTA", YELLOW
        elif freshness is Freshness.DELAYED:
            self._stale_rendered = False
            status_text, status_color = "TELEMETRÍA RETRASADA", YELLOW
        else:
            self._stale_rendered = False
            status_text, status_color = "MQTT CONECTADO", GREEN

        if self.mqtt_state == "connected":
            screen.diag_labels["status"].text = status_text
            screen.diag_labels["status"].color = status_color
            for topbar in self._topbars:
                topbar.set_status(status_text, status_color)

    def _show_error(self, message: str):
        self.general_screen.connection_error_label.text = message
        self.telemetria_screen.error_label.text = message

    # -- Registro / grabacion de sesiones --------------------------------------

    def toggle_recording(self):
        screen = self.registro_screen
        if self.recorder.is_recording:
            summary = self.recorder.stop()
            screen.record_button.text = "INICIAR"
            screen.record_button.background_color = RED
            screen.session_status_label.text = "Detenido"
            screen.session_status_label.color = MUTED
            if summary is not None:
                screen.session_name_label.text = f"Última sesión: {summary.name}"
            self._refresh_saved_sessions()
        else:
            name = self.recorder.start()
            screen.record_button.text = "DETENER"
            screen.record_button.background_color = (0.25, 0.29, 0.35, 1)
            screen.session_name_label.text = name
            screen.session_status_label.text = "Grabando - 00:00:00"
            screen.session_status_label.color = RED

    def _refresh_recording_ui(self, _dt):
        screen = self.registro_screen
        if not self.recorder.is_recording:
            return
        elapsed = self.recorder.elapsed_s()
        hh = int(elapsed // 3600)
        mm = int((elapsed % 3600) // 60)
        ss = int(elapsed % 60)
        screen.session_status_label.text = f"Grabando - {hh:02d}:{mm:02d}:{ss:02d}"
        screen.samples_metric.set_value(str(self.recorder.sample_count))
        screen.max_speed_metric.set_value(f"{self.recorder.max_speed_kmh:.1f}")
        max_temp = self.recorder.max_temp_c
        temp_level = self.thresholds.temp_kart.level(max_temp if max_temp != float("-inf") else None)
        screen.max_temp_metric.set_value(
            "--" if max_temp == float("-inf") else f"{max_temp:.1f}°",
            ALARM_COLOR[temp_level],
        )

    def _refresh_saved_sessions(self):
        sessions = self.recorder.list_saved_sessions()
        self.registro_screen.render_saved_sessions(sessions)

    def export_session(self, path: str):
        screen = self.registro_screen
        screen.export_status_label.text = "Selecciona dónde guardar el CSV..."
        screen.export_status_label.color = CYAN
        self.csv_exporter.export(path, self._export_finished)

    def _export_finished(self, success: bool, message: str):
        Clock.schedule_once(lambda _dt: self._show_export_result(success, message), 0)

    def _show_export_result(self, success: bool, message: str):
        label = self.registro_screen.export_status_label
        label.text = message
        label.color = GREEN if success else (MUTED if "cancelada" in message.lower() else RED)

    def shutdown(self):
        self.mqtt.stop()
        if self.recorder.is_recording:
            self.recorder.stop()


class LobosTelemetryApp(App):
    title = "Lobos Racing Telemetría"

    def build(self):
        if platform not in ("android", "ios"):
            Window.size = (430, 850)
        Window.clearcolor = BG
        sessions_dir = os.path.join(self.user_data_dir, "sessions")
        self.dashboard = TelemetryDashboard(sessions_dir=sessions_dir)
        return self.dashboard

    def on_stop(self):
        self.dashboard.shutdown()


if __name__ == "__main__":
    LobosTelemetryApp().run()

