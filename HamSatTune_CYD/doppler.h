#pragma once

#include <cmath>

namespace hamsat {
constexpr double SPEED_OF_LIGHT = 299792458.0;

// Positive range rate means the satellite is moving away from the observer.
inline double receivedHz(double transmittedHz,
                         double rangeRateMetersPerSecond) {
  return transmittedHz *
         (1.0 - rangeRateMetersPerSecond / SPEED_OF_LIGHT);
}

inline double requiredUplinkHz(double satelliteReceiveHz,
                               double rangeRateMetersPerSecond) {
  return satelliteReceiveHz /
         (1.0 - rangeRateMetersPerSecond / SPEED_OF_LIGHT);
}

struct Transponder {
  double downlinkLowHz;
  double downlinkHighHz;
  double uplinkLowHz;
  double uplinkHighHz;
  bool inverted;
};

inline double uplinkForDownlink(const Transponder& transponder,
                                double nominalDownlinkHz) {
  const double span =
      transponder.downlinkHighHz - transponder.downlinkLowHz;
  if (span <= 0.0) {
    return NAN;
  }

  const double fraction =
      (nominalDownlinkHz - transponder.downlinkLowHz) / span;
  return transponder.inverted
             ? transponder.uplinkHighHz -
                   fraction * (transponder.uplinkHighHz -
                               transponder.uplinkLowHz)
             : transponder.uplinkLowHz +
                   fraction * (transponder.uplinkHighHz -
                               transponder.uplinkLowHz);
}
}  // namespace hamsat
