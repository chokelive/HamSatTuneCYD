#include <Arduino_GFX_Library.h>
#include <cmath>
#include <cstring>
#include "display_ui.h"
#include "satellite_data.h"

namespace {
constexpr int8_t TFT_BL = 21;
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t COLOR_BG = rgb565(7, 17, 28);
constexpr uint16_t COLOR_HEADER = rgb565(18, 40, 62);
constexpr uint16_t COLOR_PANEL = rgb565(20, 39, 57);
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

char shownRx[20] = "";
char shownTx[20] = "";
char shownUtc[13] = "";
char shownStatus[13] = "";
char shownRadio[17] = "";

// Built-in font cells have an opaque background, so changing one digit does
// not erase the whole card or briefly blank the other digits.
void updateTextCells(char *shown, const char *next, size_t cellCount,
                     int16_t x, int16_t y, uint8_t size,
                     uint16_t foreground, uint16_t background) {
  gfx->setFont();
  gfx->setTextSize(size);
  gfx->setTextColor(foreground, background);
  for (size_t i = 0; i < cellCount; ++i) {
    const char oldChar = i < strlen(shown) ? shown[i] : ' ';
    const char newChar = i < strlen(next) ? next[i] : ' ';
    if (oldChar == newChar) continue;
    gfx->setCursor(x + static_cast<int16_t>(i * 6 * size), y);
    gfx->print(newChar);
  }
  strlcpy(shown, next, cellCount + 1);
}

void rightAlignedStatus(const char *status, char *buffer, size_t bufferSize) {
  const size_t width = bufferSize - 1;
  memset(buffer, ' ', width);
  buffer[width] = '\0';
  const size_t length = strlen(status) < width ? strlen(status) : width;
  memcpy(buffer + width - length, status, length);
}

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

// The built-in font is 6 pixels wide per character at text size 1.
// Keep text inside its panel, including when a profile has a long comment.
void printFitted(const char *value, int16_t x, int16_t y,
                 int16_t maxWidth, uint8_t textSize) {
  const size_t maxChars = maxWidth / (6 * textSize);
  if (maxChars == 0) return;

  gfx->setTextSize(textSize);
  gfx->setCursor(x, y);
  const size_t length = strlen(value);
  if (length <= maxChars) {
    gfx->print(value);
    return;
  }

  const size_t prefixLength = maxChars > 3 ? maxChars - 3 : 0;
  for (size_t i = 0; i < prefixLength; ++i) gfx->print(value[i]);
  for (size_t i = prefixLength; i < maxChars; ++i) gfx->print('.');
}

void drawFrequencyCard(int16_t y, const char *label, const char *frequency,
                       const char *mode, const char *emptyMessage,
                       uint16_t accent) {
  const bool hasFrequency = frequency[0] != '-';
  const uint16_t shownAccent = hasFrequency ? accent : COLOR_MUTED;
  gfx->fillRoundRect(8, y, 304, 55, 8, COLOR_PANEL);
  gfx->fillRect(8, y + 8, 4, 39, shownAccent);
  gfx->setTextSize(1);
  gfx->setTextColor(shownAccent);
  gfx->setCursor(20, y + 6);
  gfx->print(label);
  if (hasFrequency) gfx->print("  MHz");

  if (!hasFrequency) {
    gfx->setTextColor(COLOR_MUTED);
    printFitted(emptyMessage, 20, y + 24, 280, 2);
    return;
  }

  // Monospaced digits keep the frequency aligned as Doppler values change.
  gfx->setFont();
  gfx->setTextColor(COLOR_WHITE);
  printFitted(frequency, 20, y + 22, 198, 3);

  const size_t modeChars = strlen(mode) < 5 ? strlen(mode) : 5;
  const int16_t badgeWidth = static_cast<int16_t>(modeChars * 12 + 16);
  const int16_t badgeX = 300 - badgeWidth;
  gfx->fillRoundRect(badgeX, y + 20, badgeWidth, 25, 5, COLOR_HEADER);
  gfx->setTextColor(accent);
  printFitted(mode, badgeX + 8, y + 24, badgeWidth - 16, 2);
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

void displayNetworkSetup() {
  gfx->fillScreen(COLOR_BG);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(16, 35);
  gfx->print("WiFi setup");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(16, 82);
  gfx->print("Connect to HamSatTune-CYD");
  gfx->setCursor(16, 103);
  gfx->print("Open 192.168.4.1 on your phone");
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(16, 144);
  gfx->print("Needed for UTC time and fresh satellite TLEs");
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

void displayMainScreen(const char *satelliteName, const char *profileComment,
                       const char *downlinkMode, const char *uplinkMode,
                       double receiveTargetHz, double transmitTargetHz,
                       const char *callsign, const char *gridLocator,
                       const char *utcTime, const char *trackingStatus,
                       const char *radioStatus) {
  char rxTarget[20];
  char txTarget[20];
  formatMHz(receiveTargetHz, rxTarget, sizeof(rxTarget));
  formatMHz(transmitTargetHz, txTarget, sizeof(txTarget));

  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 48, COLOR_HEADER);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(12, 8);
  gfx->print("HAMSATTUNE CYD by E29AHU");
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(204, 6);
  gfx->print("CALL:");
  gfx->setCursor(204, 19);
  gfx->print("GRID:");
  gfx->setTextColor(COLOR_WHITE);
  const char *shownCallsign = callsign[0] ? callsign : "--";
  const char *shownGrid = gridLocator[0] ? gridLocator : "--";
  gfx->setCursor(308 - static_cast<int16_t>(strlen(shownCallsign) * 6), 6);
  gfx->print(shownCallsign);
  gfx->setCursor(308 - static_cast<int16_t>(strlen(shownGrid) * 6), 19);
  gfx->print(shownGrid);
  gfx->setTextColor(COLOR_WHITE);
  char titleLine[64];
  snprintf(titleLine, sizeof(titleLine), "%s  %s", satelliteName,
           profileComment);
  printFitted(titleLine, 12, 29, 296, 2);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setTextSize(1);
  gfx->setCursor(12, 51);
  gfx->print(utcTime);
  gfx->setCursor(104, 51);
  gfx->print(radioStatus);
  gfx->setCursor(308 - static_cast<int16_t>(strlen(trackingStatus) * 6), 51);
  gfx->print(trackingStatus);

  drawFrequencyCard(62, "RX / DOWNLINK", rxTarget, downlinkMode,
                    "NO DOWNLINK", COLOR_CYAN);
  drawFrequencyCard(131, "TX / UPLINK", txTarget, uplinkMode,
                    "NO UPLINK", COLOR_AMBER);

  drawButton(8, 200, 145, 38, "SATELLITES", COLOR_BLUE);
  drawButton(167, 200, 145, 38, "SETUP", COLOR_PANEL);

  strlcpy(shownRx, rxTarget, sizeof(shownRx));
  strlcpy(shownTx, txTarget, sizeof(shownTx));
  strlcpy(shownUtc, utcTime, sizeof(shownUtc));
  rightAlignedStatus(trackingStatus, shownStatus, sizeof(shownStatus));
  strlcpy(shownRadio, radioStatus, sizeof(shownRadio));
}

void displayUpdateMainValues(double receiveTargetHz, double transmitTargetHz,
                             const char *utcTime, const char *trackingStatus,
                             const char *radioStatus) {
  char rxTarget[20];
  char txTarget[20];
  char statusCells[13];
  formatMHz(receiveTargetHz, rxTarget, sizeof(rxTarget));
  formatMHz(transmitTargetHz, txTarget, sizeof(txTarget));
  rightAlignedStatus(trackingStatus, statusCells, sizeof(statusCells));

  updateTextCells(shownUtc, utcTime, 12, 12, 51, 1, COLOR_MUTED, COLOR_BG);
  updateTextCells(shownStatus, statusCells, 12, 236, 51, 1,
                  COLOR_MUTED, COLOR_BG);
  updateTextCells(shownRadio, radioStatus, 16, 104, 51, 1,
                  COLOR_CYAN, COLOR_BG);
  updateTextCells(shownRx, rxTarget, 11, 20, 84, 3,
                  COLOR_WHITE, COLOR_PANEL);
  updateTextCells(shownTx, txTarget, 11, 20, 153, 3,
                  COLOR_WHITE, COLOR_PANEL);
}

void displaySetupMenu(const char *gridLocator, const char *callsign,
                      const char *radioStatus) {
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 44, COLOR_HEADER);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 12);
  gfx->print("SETUP");

  gfx->fillRoundRect(8, 50, 304, 44, 6, COLOR_PANEL);
  gfx->fillRect(8, 56, 3, 32, COLOR_BLUE);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(20, 55);
  gfx->print("GRID LOCATOR");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(20, 76);
  gfx->print("Current: ");
  gfx->setTextColor(COLOR_CYAN);
  gfx->print(gridLocator[0] ? gridLocator : "--");

  gfx->fillRoundRect(8, 100, 304, 44, 6, COLOR_PANEL);
  gfx->fillRect(8, 106, 3, 32, COLOR_BLUE);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(20, 105);
  gfx->print("CALLSIGN");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(20, 126);
  gfx->print("Current: ");
  gfx->setTextColor(COLOR_CYAN);
  gfx->print(callsign[0] ? callsign : "--");

  gfx->fillRoundRect(8, 150, 304, 44, 6, COLOR_PANEL);
  gfx->fillRect(8, 156, 3, 32, COLOR_BLUE);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(20, 155);
  gfx->print("IC-705 RADIO");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(20, 176);
  gfx->print(radioStatus);

  drawButton(216, 203, 96, 35, "BACK", COLOR_PANEL);
}

void displayRadioScreen(const RadioSnapshot &radio) {
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 36, COLOR_HEADER);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 10);
  gfx->print("IC-705 RADIO");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(12, 42);
  gfx->print(radioLinkText(radio.link));
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(12, 56);
  printFitted("Bluetooth device: HamSatTune-CYD", 12, 56, 296, 1);

  char rx[20], tx[20];
  formatMHz(radio.rxKnown ? radio.rxHz : 0, rx, sizeof(rx));
  formatMHz(radio.txKnown ? radio.txHz : 0, tx, sizeof(tx));
  gfx->setCursor(12, 75);
  gfx->setTextColor(COLOR_WHITE);
  gfx->print("RX ");
  gfx->print(rx);
  gfx->setCursor(165, 75);
  gfx->print("TX ");
  gfx->print(tx);
  gfx->setCursor(12, 90);
  gfx->setTextColor(radio.transmitting ? COLOR_AMBER : COLOR_MUTED);
  gfx->print(radio.pttKnown ? (radio.transmitting ? "TRANSMITTING" : "RECEIVING")
                            : "TX STATE UNKNOWN");

  gfx->fillRoundRect(8, 110, 304, 73, 5, COLOR_PANEL);
  gfx->setCursor(18, 121);
  gfx->setTextColor(COLOR_WHITE);
  printFitted("On IC-705: Pairing/Connect > Device Search",
              18, 121, 284, 1);
  gfx->setCursor(18, 148);
  gfx->setTextColor(COLOR_CYAN);
  printFitted("Search Data Device; select HamSatTune-CYD",
              18, 148, 284, 1);

  drawButton(8, 203, 96, 35, "PAIR", COLOR_BLUE);
  drawButton(112, 203, 96, 35,
             radio.control ? "STOP" : "START",
             radio.control ? COLOR_GREEN : COLOR_PANEL);
  drawButton(216, 203, 96, 35, "BACK", COLOR_PANEL);
}

void displayGridEditorScreen(const char *savedGridLocator,
                             const char *editGridLocator, size_t editLength) {
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 36, COLOR_HEADER);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 10);
  gfx->print("SETUP");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(12, 44);
  gfx->print("Grid locator (Maidenhead), 4 or 6 characters");

  gfx->fillRoundRect(12, 61, 296, 28, 5, COLOR_PANEL);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(22, 67);
  gfx->print(editGridLocator[0] ? editGridLocator : "----");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(142, 72);
  gfx->print("SAVED:");
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(184, 72);
  gfx->print(savedGridLocator[0] ? savedGridLocator : "--");

  const size_t nextPosition = editLength;
  const bool letterPosition = nextPosition < 2 || nextPosition >= 4;
  if (nextPosition >= 6) {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED);
    gfx->setCursor(12, 96);
    gfx->print("Six characters entered. Save or edit.");
  } else if (letterPosition) {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED);
    gfx->setCursor(12, 96);
    gfx->print(nextPosition >= 4 ? "Choose a letter A-X"
                                 : "Choose a letter A-R");
    const char lastLetter = nextPosition >= 4 ? 'X' : 'R';
    for (char key = 'A'; key <= lastLetter; ++key) {
      const int16_t index = key - 'A';
      const int16_t x = 12 + (index % 6) * 50;
      const int16_t y = 108 + (index / 6) * 22;
      gfx->fillRoundRect(x, y, 46, 19, 4, COLOR_BLUE);
      gfx->setTextSize(1);
      gfx->setTextColor(COLOR_WHITE);
      gfx->setCursor(x + 19, y + 5);
      gfx->print(key);
    }
  } else {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_MUTED);
    gfx->setCursor(12, 96);
    gfx->print("Choose a digit");
    for (uint8_t key = 0; key < 10; ++key) {
      const int16_t x = 35 + (key % 5) * 50;
      const int16_t y = 118 + (key / 5) * 26;
      gfx->fillRoundRect(x, y, 46, 22, 4, COLOR_BLUE);
      gfx->setTextSize(1);
      gfx->setTextColor(COLOR_WHITE);
      gfx->setCursor(x + 20, y + 7);
      gfx->print(key);
    }
  }

  drawButton(8, 203, 72, 35, "DEL", COLOR_BLUE);
  drawButton(86, 203, 72, 35, "CLEAR", COLOR_BLUE);
  drawButton(164, 203, 72, 35, "SAVE", COLOR_GREEN);
  drawButton(242, 203, 70, 35, "BACK", COLOR_PANEL);
}

void displayCallsignEditorScreen(const char *savedCallsign,
                                 const char *editCallsign, size_t editLength) {
  constexpr char KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/";
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 36, COLOR_HEADER);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 10);
  gfx->print("CALLSIGN");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(12, 44);
  gfx->print("A-Z, 0-9, /  (up to 12 characters)");

  gfx->fillRoundRect(12, 61, 296, 28, 5, COLOR_PANEL);
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setCursor(22, 67);
  gfx->print(editCallsign[0] ? editCallsign : "--");
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(190, 72);
  gfx->print("SAVED:");
  gfx->setTextColor(COLOR_WHITE);
  printFitted(savedCallsign[0] ? savedCallsign : "--", 232, 72, 76, 1);

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_MUTED);
  gfx->setCursor(12, 96);
  gfx->print(editLength >= 12 ? "Twelve characters entered. Save or edit."
                              : "Tap a character");
  for (size_t i = 0; i < sizeof(KEYS) - 1; ++i) {
    const int16_t x = 10 + static_cast<int16_t>((i % 10) * 30);
    const int16_t y = 108 + static_cast<int16_t>((i / 10) * 22);
    gfx->fillRoundRect(x, y, 27, 19, 4, COLOR_BLUE);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_WHITE);
    gfx->setCursor(x + 10, y + 5);
    gfx->print(KEYS[i]);
  }

  drawButton(8, 203, 72, 35, "DEL", COLOR_BLUE);
  drawButton(86, 203, 72, 35, "CLEAR", COLOR_BLUE);
  drawButton(164, 203, 72, 35, "SAVE", COLOR_GREEN);
  drawButton(242, 203, 70, 35, "BACK", COLOR_PANEL);
}

void displaySatelliteList(size_t rowCount, size_t selectedIndex,
                          size_t firstVisibleIndex) {
  constexpr size_t VISIBLE_ROWS = 4;
  gfx->fillScreen(COLOR_BG);
  gfx->fillRect(0, 0, 320, 48, COLOR_HEADER);
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setCursor(12, 8);
  gfx->print("HAMSATTUNE CYD by E29AHU");
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
    gfx->setTextColor(COLOR_WHITE);
    char rowTitle[96];
    snprintf(rowTitle, sizeof(rowTitle), "%s  %s", profile->name,
             profile->comment);
    printFitted(rowTitle, 17, y + 9, 284, 2);
  }

  drawButton(8, 203, 96, 35, "PREV", COLOR_BLUE);
  drawButton(112, 203, 96, 35, "NEXT", COLOR_BLUE);
  drawButton(216, 203, 96, 35, "BACK", COLOR_PANEL);
}
