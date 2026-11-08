#include <Preferences.h>
#include <SPI.h>
#include <cmath>
#include "touch_input.h"

namespace {
constexpr int8_t TOUCH_CS = 33;
constexpr int8_t TOUCH_SCK = 25;
constexpr int8_t TOUCH_MISO = 39;
constexpr int8_t TOUCH_MOSI = 32;
SPIClass touchSPI(HSPI);
Preferences preferences;
TouchPoint calibration[3];

uint16_t readTouchAdc(uint8_t command) {
  touchSPI.transfer(command);
  return touchSPI.transfer16(0) >> 3;
}
}  // namespace

void touchBegin() {
  touchSPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
}

TouchSample touchReadSample() {
  touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(TOUCH_CS, LOW);

  const uint16_t z1 = readTouchAdc(0xB1);
  const uint16_t z2 = readTouchAdc(0xC1);
  const int16_t pressure = static_cast<int16_t>(4095 + z1 - z2);
  uint32_t totalX = 0;
  uint32_t totalY = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    totalY += readTouchAdc(0xD1);
    totalX += readTouchAdc(0x91);
  }

  digitalWrite(TOUCH_CS, HIGH);
  touchSPI.endTransaction();
  return {static_cast<int16_t>(totalX / 3),
          static_cast<int16_t>(totalY / 3), pressure};
}

bool touchCalibrationValid() {
  const long dx1 = calibration[1].x - calibration[0].x;
  const long dy1 = calibration[1].y - calibration[0].y;
  const long dx2 = calibration[2].x - calibration[0].x;
  const long dy2 = calibration[2].y - calibration[0].y;
  return abs(dx1 * dy2 - dy1 * dx2) > 10000;
}

bool touchCalibrationLoad() {
  preferences.begin("hamsat", true);
  const bool stored = preferences.getBytesLength("touch") == sizeof(calibration);
  if (stored) preferences.getBytes("touch", calibration, sizeof(calibration));
  preferences.end();
  return stored && touchCalibrationValid();
}

void touchCalibrationSetPoint(uint8_t index, TouchPoint point) {
  if (index < 3) calibration[index] = point;
}

void touchCalibrationSave() {
  if (!touchCalibrationValid()) return;
  preferences.begin("hamsat", false);
  preferences.putBytes("touch", calibration, sizeof(calibration));
  preferences.end();
}

bool touchMapToScreen(const TouchPoint &point, int16_t &screenX,
                      int16_t &screenY) {
  const double dx1 = calibration[1].x - calibration[0].x;
  const double dy1 = calibration[1].y - calibration[0].y;
  const double dx2 = calibration[2].x - calibration[0].x;
  const double dy2 = calibration[2].y - calibration[0].y;
  const double determinant = dx1 * dy2 - dy1 * dx2;
  if (fabs(determinant) < 10000.0) return false;

  const double px = point.x - calibration[0].x;
  const double py = point.y - calibration[0].y;
  const double u = (px * dy2 - py * dx2) / determinant;
  const double v = (dx1 * py - dy1 * px) / determinant;
  screenX = static_cast<int16_t>(lround(18.0 + 284.0 * u));
  screenY = static_cast<int16_t>(lround(18.0 + 204.0 * v));
  return true;
}
