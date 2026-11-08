#pragma once

#include <Arduino.h>

bool displayBegin();
void displayCalibrationTarget(int16_t x, int16_t y, uint8_t step);
void displayMainScreen(const char *satelliteName, const char *profileDetail,
                       const char *downlinkMode, const char *uplinkMode,
                       double downlinkNominalHz, double uplinkNominalHz,
                       double receiveTargetHz, double transmitTargetHz,
                       bool autoDopplerDemo);
void displaySatelliteList(size_t rowCount, size_t selectedIndex,
                         size_t firstVisibleIndex);
