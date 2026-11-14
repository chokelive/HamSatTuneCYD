#pragma once

#include <Arduino.h>
#include "radio_ic705.h"

bool displayBegin();
void displayCalibrationTarget(int16_t x, int16_t y, uint8_t step);
void displayMainScreen(const char *satelliteName, const char *profileComment,
                       const char *downlinkMode, const char *uplinkMode,
                       double receiveTargetHz, double transmitTargetHz,
                       const char *callsign, const char *gridLocator,
                       const char *utcTime, const char *trackingStatus,
                       const char *radioStatus);
void displayUpdateMainValues(double receiveTargetHz, double transmitTargetHz,
                             const char *utcTime, const char *trackingStatus,
                             const char *radioStatus);
void displayNetworkSetup();
void displaySatelliteList(size_t rowCount, size_t selectedIndex,
                         size_t firstVisibleIndex);
void displaySetupMenu(const char *gridLocator, const char *callsign,
                      const char *radioStatus);
void displayRadioScreen(const RadioSnapshot &radio);
void displayGridEditorScreen(const char *savedGridLocator,
                             const char *editGridLocator, size_t editLength);
void displayCallsignEditorScreen(const char *savedCallsign,
                                 const char *editCallsign, size_t editLength);
