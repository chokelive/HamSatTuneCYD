#pragma once

#include "satellite_data.h"

struct TrackingResult {
  double receiveHz;
  double transmitHz;
  char utc[13];
  const char *status;
  int32_t tuneOffsetHz;
  double azimuthDeg;
  double elevationDeg;
};

struct MapPoint {
  float latitudeDeg;
  float longitudeDeg;
};

constexpr size_t MAP_PATH_POINTS = 91;
struct MapSnapshot {
  float stationLatDeg;
  float stationLonDeg;
  float satelliteLatDeg;
  float satelliteLonDeg;
  float altitudeKm;
  MapPoint path[MAP_PATH_POINTS];
};

struct PassPrediction {
  const char *satelliteName;
  uint32_t aosUnix;
  uint32_t losUnix;
  float maxElevationDeg;
  float aosAzimuthDeg;
  float losAzimuthDeg;
};

void trackingBegin();
TrackingResult trackingCalculate(const SatelliteProfile &profile,
                                 const char *gridLocator, int32_t tuneOffsetHz);
bool trackingFreeTuneSupported(const SatelliteProfile &profile);
bool trackingFreeTuneAllowed(const SatelliteProfile &profile,
                            int32_t tuneOffsetHz);
bool trackingStationCoordinates(const char *gridLocator, float &latitudeDeg,
                                float &longitudeDeg);
bool trackingMapSnapshot(const SatelliteProfile &profile,
                         const char *gridLocator, MapSnapshot &snapshot);
void trackingPassBegin(const char *gridLocator, uint32_t nowUnix);
bool trackingPassTick(uint8_t sampleBudget);
bool trackingPassComplete();
const char *trackingPassStatus();
size_t trackingPassCount();
const PassPrediction *trackingPassAt(size_t index);
