#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>
#include "display_ui.h"
#include "fonts/FreeSansBold10pt7b.h"
#include "satellite_data.h"

namespace {
constexpr int8_t TFT_BL = 21;
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t COLOR_BG = rgb565(7, 17, 28);
constexpr uint16_t COLOR_HEADER = rgb565(18, 40, 62);
constexpr uint16_t COLOR_PANEL = rgb565(20, 39, 57);
constexpr uint16_t COLOR_RX_PANEL = rgb565(9, 48, 56);
constexpr uint16_t COLOR_TX_PANEL = rgb565(53, 38, 23);
constexpr uint16_t COLOR_SELECTED = rgb565(31, 83, 139);
constexpr uint16_t COLOR_BLUE = rgb565(42, 103, 166);
constexpr uint16_t COLOR_CYAN = rgb565(91, 220, 226);
constexpr uint16_t COLOR_GREEN = rgb565(36, 137, 83);
constexpr uint16_t COLOR_AMBER = rgb565(255, 190, 68);
constexpr uint16_t COLOR_WHITE = rgb565(248, 250, 252);
constexpr uint16_t COLOR_MUTED = rgb565(169, 193, 207);

Arduino_DataBus *tftBus = new Arduino_ESP32SPI(
    2 /* DC */, 15 /* CS */, 14 /* SCK */, 13 /* MOSI */, 12 /* MISO */);
Arduino_GFX *gfx = new Arduino_ILI9341(tftBus, -1 /* RST */, 1 /* landscape */);

void formatMHz(double frequencyHz, char *output, size_t outputSize) {
  if (frequencyHz <= 0.0) {
    snprintf(output, outputSize, "--");
    return;
  }
  const uint32_t roundedHz = static_cast<uint32_t>(lround(frequencyHz));
  snprintf(output, outputSize, "%lu.%06lu",
           static_cast<unsigned long>(roundedHz / 1000000UL),
           static_cast<unsigned long>(roundedHz % 1000000UL));
}

void formatCorrection(double targetHz, double nominalHz, char *output,
                      size_t outputSize) {
  if (targetHz <= 0.0 || nominalHz <= 0.0) {
    snprintf(output, outputSize, "No uplink");
    return;
  }
  const long deltaHz = lround(targetHz - nominalHz);
  const unsigned long magnitude =
      static_cast<unsigned long>(deltaHz < 0 ? -deltaHz : deltaHz);
  snprintf(output, outputSize, "%c%lu.%03lu kHz",
           deltaHz < 0 ? '-' : '+', magnitude / 1000UL, magnitude % 1000UL);
}

void drawFrequencyCard(int16_t x, const char *label, const char *frequency,
                       const char *correction, const char *nominal,
                       const char *mode, uint16_t background,
                       uint16_t accent) {
  gfx->fillRoundRect(x, 56, 151, 108, 8, background);
  gfx->fillRoundRect(x, 56, 151, 22, 8, accent);
  gfx->fillRect(x, 69, 151, 9, accent);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_BG);
  gfx->setCursor(x + 10, 64);
  gfx->print(label);
  // Use a font drawn at its native size instead of enlarging the 6x8 bitmap.
  gfx->setFont(&FreeSansBold10pt7b);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(x + 10, 105);
  gfx->print(frequency);
  gfx->setFont();
  gfx->setTextSize(1);
  gfx->setTextColor(accent);
  gfx->setCursor(x + 10, 117);
  gfx->print(correction);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(x + 10, 134);
  gfx->print("Base ");
  gfx->print(nominal);
  gfx->setCursor(x + 10, 149);
  gfx->print("Mode ");
  gfx->print(mode);
}

void drawButton(int16_t x, int16_t y, int16_t width, int16_t height,
                const char *label, uint16_t color) {
  gfx->fillRoundRect(x, y, width, height, 6, color);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  const int16_t textWidth = static_cast<int16_t>(strlen(label) * 12);
  gfx->setCursor(x + (width - textWidth) / 2, y + (height - 16) / 2);
  gfx->print(label);
}
}  // namespace

bool displayBegin() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  return gfx->begin();
}

void displayCalibrationTarget(int16_t x, int16_t y, uint8_t step) {
  gfx->fillScreen(COLOR_BG);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(24, 22);
  gfx->print("Touch setup");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(24, 48);
  gfx->print("Tap the crosshair to calibrate");
  gfx->setCursor(24, 63);
  gfx->print("Point ");
  gfx->print(step);
  gfx->print(" of 3");
  gfx->drawFastHLine(x - 12, y, 25, COLOR_AMBER);
  gfx->drawFastVLine(x, y - 12, 25, COLOR_AMBER);
  gfx->fillCircle(x, y, 3, COLOR_WHITE);
}

void displayMainScreen(const char *satelliteName, const char *profileDetail,
                       const char *downlinkMode, const char *uplinkMode,
                       double downlinkNominalHz, double uplinkNominalHz,
                       double receiveTargetHz, double transmitTargetHz,
                       bool autoDopplerDemo) {
  char rxTarget[20];
  char txTarget[20];
  char rxNominal[20];
  char txNominal[20];
  char rxCorrection[20];
  char txCorrection[20];
  formatMHz(receiveTargetHz, rxTarget, sizeof(rxTarget));
  formatMHz(transmitTargetHz, txTarget, sizeof(txTarget));
  formatMHz(downlinkNominalHz, rxNominal, sizeof(rxNominal));
  formatMHz(uplinkNominalHz, txNominal, sizeof(txNominal));
  formatCorrection(receiveTargetHz, downlinkNominalHz, rxCorrection,
                   sizeof(rxCorrection));
  formatCorrection(transmitTargetHz, uplinkNominalHz, txCorrection,
                   sizeof(txCorrection));

  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 48, COLOR_HEADER);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(12, 8);
  gfx->print("HamSatTune CYD");
  gfx->setTextColor(COLOR_AMBER);
  gfx->setCursor(205, 8);
  gfx->print("DEMO / NO RADIO");
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(12, 24);
  gfx->print(satelliteName);

  drawFrequencyCard(6, "RX / DOWNLINK", rxTarget, rxCorrection, rxNominal,
                    downlinkMode, COLOR_RX_PANEL, COLOR_CYAN);
  drawFrequencyCard(163, "TX / UPLINK", txTarget, txCorrection, txNominal,
                    uplinkMode, COLOR_TX_PANEL, COLOR_AMBER);

  gfx->fillRoundRect(8, 169, 304, 27, 5, COLOR_PANEL);
  gfx->fillRect(8, 174, 3, 17, COLOR_BLUE);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(17, 179);
  gfx->print(profileDetail);

  drawButton(8, 200, 145, 38, "SATELLITES", COLOR_BLUE);
  drawButton(167, 200, 145, 38,
             autoDopplerDemo ? "DEMO: ON" : "DEMO: OFF",
             autoDopplerDemo ? COLOR_GREEN : COLOR_PANEL);
}

void displaySatelliteList(size_t rowCount, size_t selectedIndex,
                          size_t firstVisibleIndex) {
  constexpr size_t VISIBLE_ROWS = 4;
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 48, COLOR_HEADER);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 8);
  gfx->print("HamSatTune CYD");
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(12, 24);
  gfx->print("SELECT SATELLITE");
  char pageLabel[16];
  const size_t lastVisible =
      firstVisibleIndex + VISIBLE_ROWS < rowCount
          ? firstVisibleIndex + VISIBLE_ROWS
          : rowCount;
  snprintf(pageLabel, sizeof(pageLabel), "%u-%u/%u",
           static_cast<unsigned>(rowCount ? firstVisibleIndex + 1 : 0),
           static_cast<unsigned>(lastVisible), static_cast<unsigned>(rowCount));
  gfx->setTextColor(COLOR_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(266, 8);
  gfx->print(pageLabel);

  for (size_t i = 0; i < VISIBLE_ROWS; ++i) {
    const size_t itemIndex = firstVisibleIndex + i;
    if (itemIndex >= rowCount) continue;
    const SatelliteProfile *profile = satelliteProfileAt(itemIndex);
    if (profile == nullptr) continue;
    const int16_t y = 51 + static_cast<int16_t>(i * 37);
    const bool selected = itemIndex == selectedIndex;
    gfx->fillRoundRect(8, y, 304, 35, 5,
                       selected ? COLOR_SELECTED : COLOR_PANEL);
    gfx->fillRect(8, y + 5, 3, 25, selected ? COLOR_CYAN : COLOR_BLUE);
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(17, y + 3);
    gfx->print(profile->name);
    gfx->setTextSize(1);
    if (selected) {
      gfx->setTextColor(COLOR_CYAN);
      gfx->setCursor(267, y + 6);
      gfx->print("ACTIVE");
    }
    gfx->setTextColor(COLOR_MUTED);
    gfx->setCursor(17, y + 23);
    gfx->print(profile->downlinkMode);
    gfx->print('>');
    gfx->print(profile->uplinkMode);
    gfx->print(" | ");
    gfx->print(profile->comment);
    gfx->print(" | ");
    gfx->print(transponderTypeName(profile->transponderType));
  }

  drawButton(8, 203, 96, 35, "PREV", COLOR_BLUE);
  drawButton(112, 203, 96, 35, "NEXT", COLOR_BLUE);
  drawButton(216, 203, 96, 35, "BACK", COLOR_PANEL);
}
