#pragma once

#include <stdint.h>

namespace civ {
inline void encodeFrequency(uint32_t hz, uint8_t output[5]) {
  for (uint8_t i = 0; i < 5; ++i) {
    const uint8_t twoDigits = hz % 100;
    output[i] = static_cast<uint8_t>(((twoDigits / 10) << 4) | (twoDigits % 10));
    hz /= 100;
  }
}

inline bool decodeFrequency(const uint8_t input[5], uint32_t &hz) {
  uint64_t value = 0;
  uint64_t place = 1;
  for (uint8_t i = 0; i < 5; ++i) {
    const uint8_t low = input[i] & 0x0F;
    const uint8_t high = input[i] >> 4;
    if (low > 9 || high > 9) return false;
    value += (low + high * 10) * place;
    place *= 100;
  }
  if (value > UINT32_MAX) return false;
  hz = static_cast<uint32_t>(value);
  return true;
}
}  // namespace civ
