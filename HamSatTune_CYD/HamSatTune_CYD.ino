#include "display_ui.h"
#include "satellite_data.h"
#include "touch_input.h"
#include "tracking.h"
#include "radio_ic705.h"
#include <Preferences.h>

bool touchCalibrated = false;
bool touchConsumed = false;
size_t selectedProfileIndex = 0;
size_t profileCount = 0;
size_t firstVisibleProfile = 0;
bool satelliteListOpen = false;
enum class SetupPage { Closed, Menu, GridEditor, CallsignEditor, Radio };
SetupPage setupPage = SetupPage::Closed;
TrackingResult lastTracking{0, 0, "--:--:-- UTC", "NO UTC"};
uint32_t lastRadioVersion = 0;
char gridLocator[7] = "";
char gridLocatorEdit[7] = "";
size_t gridLocatorEditLength = 0;
char callsign[13] = "";
char callsignEdit[13] = "";
size_t callsignEditLength = 0;
constexpr size_t VISIBLE_PROFILE_ROWS = 4;

bool validGridLocator(const char *value) {
  const size_t length = strlen(value);
  if (length == 0) return true;
  if (length != 4 && length != 6) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    if (i < 2 && (c < 'A' || c > 'R')) return false;
    if ((i == 2 || i == 3) && (c < '0' || c > '9')) return false;
    if (i >= 4 && (c < 'A' || c > 'X')) return false;
  }
  return true;
}

bool validCallsign(const char *value) {
  const size_t length = strlen(value);
  if (length > 12) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    if ((c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != '/') {
      return false;
    }
  }
  return true;
}

void loadGridLocator() {
  Preferences preferences;
  preferences.begin("hamsat", true);
  preferences.getString("grid", gridLocator, sizeof(gridLocator));
  preferences.end();
  if (!validGridLocator(gridLocator)) gridLocator[0] = '\0';
}

void saveGridLocator() {
  if (!validGridLocator(gridLocatorEdit)) return;
  strcpy(gridLocator, gridLocatorEdit);
  Preferences preferences;
  preferences.begin("hamsat", false);
  preferences.putString("grid", gridLocator);
  preferences.end();
  radioProfileChanged();
}

void loadCallsign() {
  Preferences preferences;
  preferences.begin("hamsat", true);
  preferences.getString("callsign", callsign, sizeof(callsign));
  preferences.end();
  if (!validCallsign(callsign)) callsign[0] = '\0';
}

void saveCallsign() {
  if (!validCallsign(callsignEdit)) return;
  strcpy(callsign, callsignEdit);
  Preferences preferences;
  preferences.begin("hamsat", false);
  preferences.putString("callsign", callsign);
  preferences.end();
}

void drawSetupMenu() {
  satelliteListOpen = false;
  setupPage = SetupPage::Menu;
  const RadioSnapshot radio = radioSnapshot();
  displaySetupMenu(gridLocator, callsign, radioMainStatus(radio));
}

void drawRadio() {
  setupPage = SetupPage::Radio;
  const RadioSnapshot radio = radioSnapshot();
  displayRadioScreen(radio);
  lastRadioVersion = radio.version;
}

void drawGridEditor() {
  setupPage = SetupPage::GridEditor;
  displayGridEditorScreen(gridLocator, gridLocatorEdit,
                          gridLocatorEditLength);
}

void drawCallsignEditor() {
  setupPage = SetupPage::CallsignEditor;
  displayCallsignEditorScreen(callsign, callsignEdit, callsignEditLength);
}

void drawScreen() {
  satelliteListOpen = false;
  setupPage = SetupPage::Closed;
  const SatelliteProfile *profile =
      satelliteProfileAt(selectedProfileIndex);
  if (profile == nullptr) return;

  lastTracking = trackingCalculate(*profile, gridLocator);
  const RadioSnapshot radio = radioSnapshot();
  displayMainScreen(profile->name, profile->comment, profile->downlinkMode,
                    profile->uplinkMode, lastTracking.receiveHz,
                    lastTracking.transmitHz, callsign, gridLocator,
                    lastTracking.utc, lastTracking.status,
                    radioMainStatus(radio));
}

void refreshScreenValues() {
  const SatelliteProfile *profile = satelliteProfileAt(selectedProfileIndex);
  if (profile == nullptr) return;
  lastTracking = trackingCalculate(*profile, gridLocator);
  if (satelliteListOpen || setupPage != SetupPage::Closed) return;
  const RadioSnapshot radio = radioSnapshot();
  displayUpdateMainValues(lastTracking.receiveHz, lastTracking.transmitHz,
                          lastTracking.utc, lastTracking.status,
                          radioMainStatus(radio));
}

void drawSatelliteList() {
  satelliteListOpen = true;
  setupPage = SetupPage::Closed;
  displaySatelliteList(profileCount, selectedProfileIndex, firstVisibleProfile);
}

bool captureCalibrationPoint(int16_t screenX, int16_t screenY, uint8_t step,
                             TouchPoint &rawPoint) {
  uint32_t start = millis();
  while (touchReadSample().pressure >= 300) {
    if (millis() - start > 30000) return false;
    delay(10);
  }

  displayCalibrationTarget(screenX, screenY, step);
  Serial.printf("Touch calibration point %u at screen %d,%d\n", step,
                screenX, screenY);

  start = millis();
  TouchSample sample{};
  while (sample.pressure < 300) {
    sample = touchReadSample();
    if (sample.pressure >= 300) break;
    if (millis() - start > 30000) return false;
    delay(10);
  }

  delay(30);
  long totalX = 0;
  long totalY = 0;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < 12; ++i) {
    sample = touchReadSample();
    if (sample.pressure < 300) break;
    totalX += sample.x;
    totalY += sample.y;
    ++samples;
    delay(5);
  }
  while (touchReadSample().pressure >= 300) delay(10);

  if (samples == 0) return false;
  rawPoint = {static_cast<int16_t>(totalX / samples),
              static_cast<int16_t>(totalY / samples)};
  Serial.printf("  raw touch %d,%d\n", rawPoint.x, rawPoint.y);
  return true;
}

bool calibrateTouch() {
  const int16_t screenX[3] = {18, 302, 18};
  const int16_t screenY[3] = {18, 18, 222};
  for (uint8_t i = 0; i < 3; ++i) {
    TouchPoint rawPoint{};
    if (!captureCalibrationPoint(screenX[i], screenY[i], i + 1, rawPoint)) {
      Serial.println("Touch calibration timed out.");
      return false;
    }
    touchCalibrationSetPoint(i, rawPoint);
  }
  if (!touchCalibrationValid()) {
    Serial.println("Touch points invalid; send C and calibrate again.");
    return false;
  }
  touchCalibrationSave();
  Serial.println("Touch calibration saved.");
  return true;
}

void handleTouch() {
  const TouchSample sample = touchReadSample();
  if (sample.pressure < 300) {
    touchConsumed = false;
    return;
  }
  if (touchConsumed || !touchCalibrated) return;
  touchConsumed = true;

  const TouchPoint rawPoint{sample.x, sample.y};
  int16_t x;
  int16_t y;
  if (!touchMapToScreen(rawPoint, x, y)) return;
  Serial.printf("Touch at %d,%d\n", x, y);

  if (setupPage == SetupPage::Menu) {
    if (x >= 8 && x < 312 && y >= 50 && y < 94) {
      strcpy(gridLocatorEdit, gridLocator);
      gridLocatorEditLength = strlen(gridLocatorEdit);
      drawGridEditor();
    } else if (x >= 8 && x < 312 && y >= 100 && y < 144) {
      strcpy(callsignEdit, callsign);
      callsignEditLength = strlen(callsignEdit);
      drawCallsignEditor();
    } else if (x >= 8 && x < 312 && y >= 150 && y < 194) {
      drawRadio();
    } else if (x >= 216 && x < 312 && y >= 203 && y < 238) {
      drawScreen();
    }
    return;
  }

  if (setupPage == SetupPage::Radio) {
    if (y >= 203 && y < 238) {
      if (x >= 8 && x < 104) {
        radioPreparePairing();
        drawRadio();
      } else if (x >= 112 && x < 208) {
        radioToggleControl();
        drawRadio();
      } else if (x >= 216 && x < 312) {
        drawSetupMenu();
      }
    }
    return;
  }

  if (setupPage == SetupPage::GridEditor) {
    if (y >= 203 && y < 238) {
      if (x >= 8 && x < 80) {
        if (gridLocatorEditLength > 0) {
          gridLocatorEdit[--gridLocatorEditLength] = '\0';
        }
        drawGridEditor();
      } else if (x >= 86 && x < 158) {
        gridLocatorEditLength = 0;
        gridLocatorEdit[0] = '\0';
        drawGridEditor();
      } else if (x >= 164 && x < 236) {
        if (validGridLocator(gridLocatorEdit)) {
          saveGridLocator();
          drawSetupMenu();
        }
      } else if (x >= 242 && x < 312) {
        drawSetupMenu();
      }
    } else if (gridLocatorEditLength < 6) {
      const size_t position = gridLocatorEditLength;
      const bool letterPosition = position < 2 || position >= 4;
      char key = '\0';
      if (letterPosition && x >= 12 && x < 308 && y >= 108 && y < 193) {
        const int16_t col = (x - 12) / 50;
        const int16_t row = (y - 108) / 22;
        if ((x - 12) % 50 < 46 && (y - 108) % 22 < 19) {
          key = static_cast<char>('A' + row * 6 + col);
          const char lastLetter = position >= 4 ? 'X' : 'R';
          if (key > lastLetter) key = '\0';
        }
      } else if (!letterPosition && x >= 35 && x < 285 &&
                 y >= 118 && y < 166) {
        const int16_t col = (x - 35) / 50;
        const int16_t row = (y - 118) / 26;
        if (col < 5 && (x - 35) % 50 < 46 &&
            (y - 118) % 26 < 22) {
          key = static_cast<char>('0' + row * 5 + col);
        }
      }
      if (key != '\0') {
        gridLocatorEdit[gridLocatorEditLength++] = key;
        gridLocatorEdit[gridLocatorEditLength] = '\0';
        drawGridEditor();
      }
    }
    return;
  }

  if (setupPage == SetupPage::CallsignEditor) {
    if (y >= 203 && y < 238) {
      if (x >= 8 && x < 80) {
        if (callsignEditLength > 0) {
          callsignEdit[--callsignEditLength] = '\0';
        }
        drawCallsignEditor();
      } else if (x >= 86 && x < 158) {
        callsignEditLength = 0;
        callsignEdit[0] = '\0';
        drawCallsignEditor();
      } else if (x >= 164 && x < 236) {
        if (validCallsign(callsignEdit)) {
          saveCallsign();
          drawSetupMenu();
        }
      } else if (x >= 242 && x < 312) {
        drawSetupMenu();
      }
    } else if (callsignEditLength < 12 && x >= 10 && x < 310 &&
               y >= 108 && y < 193) {
      constexpr char KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/";
      const int16_t col = (x - 10) / 30;
      const int16_t row = (y - 108) / 22;
      const size_t keyIndex = static_cast<size_t>(row * 10 + col);
      if ((x - 10) % 30 < 27 && (y - 108) % 22 < 19 &&
          keyIndex < sizeof(KEYS) - 1) {
        callsignEdit[callsignEditLength++] = KEYS[keyIndex];
        callsignEdit[callsignEditLength] = '\0';
        drawCallsignEditor();
      }
    }
    return;
  }

  if (satelliteListOpen) {
    if (x >= 8 && x < 312 && y >= 51 && y < 199 &&
        (y - 51) % 37 < 35) {
      const size_t row = static_cast<size_t>((y - 51) / 37);
      const size_t profileIndex = firstVisibleProfile + row;
      if (profileIndex < profileCount) {
        if (profileIndex != selectedProfileIndex) radioProfileChanged();
        selectedProfileIndex = profileIndex;
        drawScreen();
      }
    } else if (y >= 203 && y < 238) {
      if (x >= 8 && x < 104 && firstVisibleProfile > 0) {
        firstVisibleProfile = firstVisibleProfile > VISIBLE_PROFILE_ROWS
                                  ? firstVisibleProfile - VISIBLE_PROFILE_ROWS
                                  : 0;
        drawSatelliteList();
      } else if (x >= 112 && x < 208) {
        const size_t maxFirst = profileCount > VISIBLE_PROFILE_ROWS
                                    ? profileCount - VISIBLE_PROFILE_ROWS
                                    : 0;
        firstVisibleProfile =
            firstVisibleProfile + VISIBLE_PROFILE_ROWS < profileCount
                ? firstVisibleProfile + VISIBLE_PROFILE_ROWS
                : maxFirst;
        drawSatelliteList();
      } else if (x >= 216 && x < 312) {
        drawScreen();
      }
    }
    return;
  }

  if (y >= 200 && y < 238 && x >= 8 && x < 153) {
    const size_t maxFirst = profileCount > VISIBLE_PROFILE_ROWS
                                ? profileCount - VISIBLE_PROFILE_ROWS
                                : 0;
    firstVisibleProfile = selectedProfileIndex > 1
                              ? selectedProfileIndex - 1
                              : 0;
    if (firstVisibleProfile > maxFirst) firstVisibleProfile = maxFirst;
    drawSatelliteList();
  } else if (y >= 200 && y < 238 && x >= 167 && x < 312) {
    drawSetupMenu();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("HamSatTune CYD - satellite tracking and IC-705 control");

  if (!displayBegin()) {
    Serial.println("Display init failed; check CYD model and wiring.");
    while (true) delay(1000);
  }
  touchBegin();
  profileCount = satelliteProfileCount();
  loadGridLocator();
  loadCallsign();

  touchCalibrated = touchCalibrationLoad();
  if (!touchCalibrated) {
    Serial.println("First run: tap the three displayed crosshairs to calibrate.");
    touchCalibrated = calibrateTouch();
  } else {
    Serial.println("Touch calibration loaded. Send C in Serial Monitor to recalibrate.");
  }

  trackingBegin();
  radioBegin();

  drawScreen();
  Serial.printf("Loaded %u satellite/mode profiles from embedded SQF data.\n",
                static_cast<unsigned>(satelliteProfileCount()));
  Serial.println("Open SETUP > IC-705 RADIO, then connect from the IC-705 to HamSatTune-CYD.");
  Serial.println("Radio control starts OFF.");
}

void loop() {
  handleTouch();

  static uint32_t lastDraw = 0;
  const SatelliteProfile *profile = satelliteProfileAt(selectedProfileIndex);
  const uint32_t interval = profile && strcmp(profile->comment, "V/U FT4") == 0
                                ? 250 : 1000;
  if (millis() - lastDraw >= interval) {
    lastDraw = millis();
    refreshScreenValues();
  }
  radioTick(profile, lastTracking);
  if (setupPage == SetupPage::Radio) {
    const RadioSnapshot radio = radioSnapshot();
    if (radio.version != lastRadioVersion) {
      displayRadioScreen(radio);
      lastRadioVersion = radio.version;
    }
  }

  if (Serial.available() > 0) {
    const char command = Serial.read();
    if (command == 'c' || command == 'C') {
      touchCalibrated = calibrateTouch();
      drawScreen();
    }
  }
  delay(10);
}
