#include "Interface.h"

#include <Arduino_GFX_Library.h>
#include <DFRobotDFPlayerMini.h>
#include <SPI.h>

#include "BoardConfig.h"

namespace lobos::ui {
namespace {

constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kCyan = 0x07FF;

Arduino_DataBus* bus = new Arduino_HWSPI(board::kTftDcPin, board::kTftCsPin,
                                          &SPI);
// Suposicion explicita: el TFT SPI 3.5" usa ILI9488. Si el modulo monta otro
// controlador, se cambia esta unica construccion; los pines no cambian.
Arduino_GFX* gfx = new Arduino_ILI9488_18bit(bus, board::kTftResetPin, 1, false);
DFRobotDFPlayerMini player;
bool displayReady = false;
bool audioReady = false;
uint16_t previousAlarms = 0;
uint32_t lastDrawMs = 0;

void field(int16_t x, int16_t y, uint16_t color, const char* label,
           const String& value) {
  gfx->setCursor(x, y);
  gfx->setTextColor(color, kBlack);
  gfx->setTextSize(2);
  gfx->print(label);
  gfx->print(value);
  gfx->print("      ");
}

}  // namespace

void begin() {
  pinMode(board::kTftBacklightPin, OUTPUT);
  digitalWrite(board::kTftBacklightPin, HIGH);
  SPI.setSCK(board::kTftSckPin);
  SPI.setTX(board::kTftMosiPin);
  SPI.begin();
  displayReady = gfx->begin();
  if (displayReady) {
    gfx->fillScreen(kBlack);
    gfx->setTextColor(kCyan);
    gfx->setTextSize(2);
    gfx->setCursor(12, 12);
    gfx->print("LOBOS RACING TELEMETRIA");
  }

  Serial2.setTX(board::kDfTxPin);
  Serial2.setRX(board::kDfRxPin);
  Serial2.begin(board::kDfBaud);
  audioReady = player.begin(Serial2, true, true);
  if (audioReady) player.volume(20);
  if (Serial) {
    Serial.print("[UI] TFT="); Serial.print(displayReady ? "ok" : "fail");
    Serial.print(" DFPlayer="); Serial.println(audioReady ? "ok" : "fail");
  }
}

void update(const ProcessedTelemetryFrame& frame) {
  const uint32_t now = millis();
  if (displayReady && now - lastDrawMs >= 200U) {
    lastDrawMs = now;
    field(12, 55, kWhite, "VEL  ", String(frame.speed_x10_kmh / 10.0, 1) + " km/h");
    field(12, 85, kWhite, "RPM  ", String(frame.rpm));
    field(12, 115, kYellow, "TEMP ", String(frame.temperature_x10_C / 10.0, 1) + " C");
    field(12, 145, kGreen, "BMS1 ", String(frame.bms1.soc_x10_pct / 10.0, 1) + "%");
    field(12, 175, kGreen, "BMS2 ", String(frame.bms2.soc_x10_pct / 10.0, 1) + "%");
    field(12, 205, frame.alarm_flags == 0 ? kGreen : kRed, "ALARM ",
          String(frame.alarm_flags, HEX));
  }

  const uint16_t newAlarms = frame.alarm_flags & ~previousAlarms;
  if (audioReady && newAlarms != 0) {
    // Archivo 0001.mp3: alarma generica. El mapa de voces se define después.
    player.play(1);
  }
  previousAlarms = frame.alarm_flags;
}

void serviceAudio() {
  if (audioReady && player.available()) {
    (void)player.readType();
    (void)player.read();
  }
}

}  // namespace lobos::ui

