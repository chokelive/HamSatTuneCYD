#include "satellite_data.h"

namespace {
// Ported from HamSatTune's Doppler.sqf. SQF frequencies are in kHz;
// these embedded values are converted to Hz.
const SatelliteProfile profiles[] = {
    {"FO-29", 435850000, 145950000, "USB", "LSB",
     TransponderType::Reverse, 0, 0, "SSB"},
    {"FO-29", 435850000, 145950000, "CW", "CW",
     TransponderType::Reverse, 0, 0, "CW"},
    {"IO-86", 435880000, 145880000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM tone 88.5Hz"},
    {"IO-86", 145825000, 145825000, "FM", "FM",
     TransponderType::Normal, 0, 0, "APRS"},
    {"ISS", 145825000, 145825000, "FM", "FM",
     TransponderType::Normal, 0, 0, "APRS"},
    {"ISS", 437800000, 145990000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM tone 67.0Hz"},
    {"ISS", 145800000, 0, "FM", "FM",
     TransponderType::Normal, 0, 0, "SSTV"},
    {"ISS", 437550000, 0, "FM", "FM",
     TransponderType::Normal, 0, 0, "SSTV UHF"},
    {"RS-44", 435640000, 145965000, "USB", "LSB",
     TransponderType::Reverse, 0, -1600, "SSB"},
    {"RS-44", 435640000, 145965000, "CW", "CW",
     TransponderType::Reverse, 0, -2400, "CW"},
    {"RS-44", 435612000, 145991000, "USB-D", "LSB-D",
     TransponderType::Reverse, 0, 430, "V/U FT4"},
    {"SO-50", 436795000, 145850000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM tone 67.0Hz"},
    {"AO-27", 436795000, 145850000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM"},
    {"SO-124", 436885000, 145925000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM"},
    {"AO-123", 435400000, 145850000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM tone 67.0Hz"},
    {"SONATE-2", 145825000, 145825000, "FM", "FM",
     TransponderType::Normal, 0, 0, "APRS"},
    {"MO-122", 435825000, 145925000, "USB", "LSB",
     TransponderType::Reverse, 0, 0, "SSB"},
    {"MO-122", 435825000, 145925000, "CW", "CW",
     TransponderType::Reverse, 0, 0, "CW"},
    {"HADES-ICM (SO-125)", 436666000, 145875000, "FM", "FM",
     TransponderType::Normal, 0, 0, "FM"},
    {"UmKA 1 (RS40S)", 437625000, 0, "FM", "FM",
     TransponderType::Normal, 0, 0, "SSTV"},
};
}  // namespace

size_t satelliteProfileCount() {
  return sizeof(profiles) / sizeof(profiles[0]);
}

const SatelliteProfile *satelliteProfileAt(size_t index) {
  if (index >= satelliteProfileCount()) return nullptr;
  return &profiles[index];
}

const char *transponderTypeName(TransponderType type) {
  return type == TransponderType::Reverse ? "REV" : "NOR";
}
