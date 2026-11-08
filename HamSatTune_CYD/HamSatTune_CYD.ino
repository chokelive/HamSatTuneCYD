#include "display_ui.h"
#include "doppler.h"
#include "satellite_data.h"
#include "touch_input.h"

bool touchCalibrated = false;
bool touchConsumed = false;
bool autoDopplerDemo = false;
size_t selectedProfileIndex = 0;
size_t profileCount = 0;
size_t firstVisibleProfile = 0;
bool satelliteListOpen = false;
constexpr size_t VISIBLE_PROFILE_ROWS = 4;

void drawScreen() {
  satelliteListOpen = false;
  const SatelliteProfile *profile =
      satelliteProfileAt(selectedProfileIndex);
  if (profile == nullptr) return;

  const double downlinkNominal =
      static_cast<double>(profile->downlinkHz) + profile->downlinkOffsetHz;
  const double uplinkNominal = profile->uplinkHz == 0
                                   ? 0.0
                                   : static_cast<double>(profile->uplinkHz) +
                                         profile->uplinkOffsetHz;
  const double simulatedRangeRate = autoDopplerDemo ? -3000.0 : 0.0;
  const double receiveTarget =
      hamsat::receivedHz(downlinkNominal, simulatedRangeRate);
  const double transmitTarget = uplinkNominal > 0.0
                                    ? hamsat::requiredUplinkHz(
                                          uplinkNominal, simulatedRangeRate)
                                    : 0.0;
  char profileDetail[64];
  snprintf(profileDetail, sizeof(profileDetail), "%s | %s>%s | %s",
           transponderTypeName(profile->transponderType),
           profile->downlinkMode, profile->uplinkMode, profile->comment);

  displayMainScreen(profile->name, profileDetail, profile->downlinkMode,
                    profile->uplinkMode, downlinkNominal, uplinkNominal,
                    receiveTarget, transmitTarget, autoDopplerDemo);
}

void drawSatelliteList() {
  satelliteListOpen = true;
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

  if (satelliteListOpen) {
    if (x >= 8 && x < 312 && y >= 51 && y < 199 &&
        (y - 51) % 37 < 35) {
      const size_t row = static_cast<size_t>((y - 51) / 37);
      const size_t profileIndex = firstVisibleProfile + row;
      if (profileIndex < profileCount) {
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
    autoDopplerDemo = !autoDopplerDemo;
    drawScreen();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("HamSatTune CYD - touch UI demo; no radio control");

  if (!displayBegin()) {
    Serial.println("Display init failed; check CYD model and wiring.");
    while (true) delay(1000);
  }
  touchBegin();
  profileCount = satelliteProfileCount();

  touchCalibrated = touchCalibrationLoad();
  if (!touchCalibrated) {
    Serial.println("First run: tap the three displayed crosshairs to calibrate.");
    touchCalibrated = calibrateTouch();
  } else {
    Serial.println("Touch calibration loaded. Send C in Serial Monitor to recalibrate.");
  }

  drawScreen();
  Serial.printf("Loaded %u satellite/mode profiles from embedded SQF data.\n",
                static_cast<unsigned>(satelliteProfileCount()));
  Serial.println("Tap SATELLITES for the list or DEMO: ON/OFF. No radio commands are sent.");
}

void loop() {
  handleTouch();

  if (Serial.available() > 0) {
    const char command = Serial.read();
    if (command == 'c' || command == 'C') {
      touchCalibrated = calibrateTouch();
      drawScreen();
    }
  }
  delay(10);
}
