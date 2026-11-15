#pragma once

#include <Arduino.h>
#include "radio_ic705.h"
#include "tracking.h"

bool displayBegin();
void displayCalibrationTarget(int16_t x, int16_t y, uint8_t step);
void displayMainScreen(const char *satelliteName, const char *profileComment,
                       const char *downlinkMode, const char *uplinkMode,
                       double receiveTargetHz, double transmitTargetHz,
                       double azimuthDeg, double elevationDeg,
                       const char *callsign, const char *gridLocator,
                       const char *utcTime, const char *trackingStatus,
                       const char *radioStatus, bool showCenterButton);
void displayUpdateMainValues(double receiveTargetHz, double transmitTargetHz,
                             double azimuthDeg, double elevationDeg,
                             const char *utcTime, const char *trackingStatus,
                             const char *radioStatus);
void displayMapScreen(const char *satelliteName, const char *callsign,
                      const char *utcTime,
                      const char *trackingStatus, double azimuthDeg,
                      double elevationDeg, const MapSnapshot &map,
                      bool stationValid, bool mapValid);
void displayPassList(const PassPrediction *passes, size_t count,
                     size_t firstVisible, uint32_t nowUnix,
                     const char *status);
void displayPassListCountdowns(const PassPrediction *passes, size_t count,
                               size_t firstVisible, uint32_t nowUnix);
void displayPassDetail(const PassPrediction &pass, uint32_t nowUnix);
void displayPassDetailCountdown(const PassPrediction &pass, uint32_t nowUnix);
void displayNetworkSetup();
void displaySatelliteList(size_t rowCount, size_t selectedIndex,
                         size_t firstVisibleIndex);
void displaySetupMenu(const char *gridLocator, const char *callsign,
                      const char *radioStatus);
void displayRadioScreen(const RadioSnapshot &radio);
void displayUpdateRadioValues(const RadioSnapshot &radio);
void displayGridEditorScreen(const char *savedGridLocator,
                             const char *editGridLocator, size_t editLength);
void displayCallsignEditorScreen(const char *savedCallsign,
                                 const char *editCallsign, size_t editLength);
