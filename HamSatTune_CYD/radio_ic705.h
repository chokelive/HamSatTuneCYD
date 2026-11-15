#pragma once

#include "satellite_data.h"
#include "tracking.h"

enum class RadioLinkState : uint8_t {
  Off, Waiting, Connected, Disconnected, Error
};

struct RadioSnapshot {
  RadioLinkState link = RadioLinkState::Off;
  bool control = false;
  bool fault = false;
  bool transmitting = false;
  bool pttKnown = false;
  bool rxKnown = false;
  bool txKnown = false;
  int32_t tuneOffsetHz = 0;
  uint32_t rxHz = 0;
  uint32_t txHz = 0;
  uint32_t version = 0;
};

void radioBegin();
void radioTick(const SatelliteProfile *profile, const TrackingResult &tracking);
RadioSnapshot radioSnapshot();
void radioPreparePairing();
void radioProfileChanged();
void radioCenterTune();
const char *radioMainStatus(const RadioSnapshot &snapshot);
const char *radioLinkText(RadioLinkState state);
