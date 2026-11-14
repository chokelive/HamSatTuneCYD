#pragma once

#include "satellite_data.h"

struct TrackingResult {
  double receiveHz;
  double transmitHz;
  char utc[13];
  const char *status;
};

void trackingBegin();
TrackingResult trackingCalculate(const SatelliteProfile &profile,
                                 const char *gridLocator);
