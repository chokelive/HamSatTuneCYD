#pragma once

#include <Arduino.h>

enum class TransponderType : uint8_t {
  Normal,
  Reverse,
};

struct SatelliteProfile {
  const char *name;
  uint32_t downlinkHz;
  uint32_t uplinkHz;
  const char *downlinkMode;
  const char *uplinkMode;
  TransponderType transponderType;
  int32_t downlinkOffsetHz;
  int32_t uplinkOffsetHz;
  const char *comment;
};

size_t satelliteProfileCount();
const SatelliteProfile *satelliteProfileAt(size_t index);
const char *transponderTypeName(TransponderType type);
