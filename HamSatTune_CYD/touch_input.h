#pragma once

#include <Arduino.h>

struct TouchPoint {
  int16_t x;
  int16_t y;
};

struct TouchSample {
  int16_t x;
  int16_t y;
  int16_t pressure;
};

void touchBegin();
TouchSample touchReadSample();
bool touchCalibrationLoad();
bool touchCalibrationValid();
void touchCalibrationSetPoint(uint8_t index, TouchPoint point);
void touchCalibrationSave();
bool touchMapToScreen(const TouchPoint &point, int16_t &screenX,
                      int16_t &screenY);
