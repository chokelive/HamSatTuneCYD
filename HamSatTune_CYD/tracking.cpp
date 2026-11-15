#include "tracking.h"

#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <sys/time.h>
#include <time.h>
#include <cmath>
#include <cstring>

#include "display_ui.h"
#include "doppler.h"
#include "src/sgp4/Sgp4.h"

namespace {
constexpr char TLE_URL[] = "https://www.amsat.org/tle/current/nasabare.txt";
constexpr uint32_t FETCH_INTERVAL_SECONDS = 12UL * 60UL * 60UL;
constexpr double MAX_TLE_AGE_DAYS = 7.0;

struct TleRecord {
  char name[25];
  char line1[70];
  char line2[70];
};

TleRecord tleRecords[24]{};
size_t tleCount = 0;
uint32_t lastFetch = 0;
Sgp4 orbit;
char activeLine1[70] = "";
char activeGrid[7] = "";

bool nameMatches(const char *profile, const char *tleName) {
  if (strcasecmp(profile, tleName) == 0) return true;
  if (strcmp(profile, "ISS") == 0 && strncasecmp(tleName, "ISS", 3) == 0) return true;
  if (strncmp(profile, "HADES-ICM", 9) == 0 &&
      strncasecmp(tleName, "HADES-ICM", 9) == 0) return true;
  if (strncmp(profile, "UmKA 1", 6) == 0 &&
      (strncasecmp(tleName, "UMKA", 4) == 0 ||
       strncasecmp(tleName, "RS40S", 5) == 0)) return true;
  return false;
}

const TleRecord *findTle(const char *profileName) {
  for (size_t i = 0; i < tleCount; ++i) {
    if (nameMatches(profileName, tleRecords[i].name)) return &tleRecords[i];
  }
  return nullptr;
}

bool parseTleFeed(const String &body) {
  static TleRecord parsed[24];
  memset(parsed, 0, sizeof(parsed));
  size_t count = 0;
  int cursor = 0;
  String lines[3];
  size_t part = 0;
  while (cursor < body.length()) {
    const int end = body.indexOf('\n', cursor);
    String line = body.substring(cursor, end < 0 ? body.length() : end);
    line.trim();
    cursor = end < 0 ? body.length() : end + 1;
    if (line.length() == 0) continue;
    if (line.startsWith("1 ")) {
      lines[1] = line;
      part = 1;
    } else if (line.startsWith("2 ") && part == 1) {
      lines[2] = line;
      bool supported = false;
      for (size_t i = 0; i < satelliteProfileCount(); ++i) {
        if (nameMatches(satelliteProfileAt(i)->name, lines[0].c_str())) {
          supported = true;
          break;
        }
      }
      if (supported && lines[1].length() >= 60 &&
          lines[2].length() >= 60 &&
          lines[1].substring(2, 7) == lines[2].substring(2, 7) &&
          count < 24) {
        lines[0].toCharArray(parsed[count].name, sizeof(parsed[count].name));
        lines[1].toCharArray(parsed[count].line1, sizeof(parsed[count].line1));
        lines[2].toCharArray(parsed[count].line2, sizeof(parsed[count].line2));
        ++count;
      }
      part = 0;
    } else if (!line.startsWith("2 ")) {
      lines[0] = line;
      part = 0;
    }
  }
  if (count == 0) return false;
  memcpy(tleRecords, parsed, sizeof(parsed));
  tleCount = count;
  activeLine1[0] = '\0';
  return true;
}

void loadCache() {
  Preferences preferences;
  preferences.begin("hamsat-tle", true);
  const size_t bytes = preferences.getBytesLength("records");
  if (bytes == sizeof(tleRecords)) {
    preferences.getBytes("records", tleRecords, bytes);
    for (size_t i = 0; i < 24 && tleRecords[i].name[0]; ++i) {
      if (tleRecords[i].line1[0] != '1' || tleRecords[i].line2[0] != '2') break;
      ++tleCount;
    }
  }
  lastFetch = preferences.getULong("fetched", 0);
  preferences.end();
}

void saveCache() {
  Preferences preferences;
  preferences.begin("hamsat-tle", false);
  preferences.putBytes("records", tleRecords, sizeof(tleRecords));
  preferences.putULong("fetched", lastFetch);
  preferences.end();
}

void refreshTle(uint32_t now) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, TLE_URL)) return;
  const int status = http.GET();
  if (status == HTTP_CODE_OK) {
    const String body = http.getString();
    if (body.length() > 1000 && body.length() < 100000 && parseTleFeed(body)) {
      lastFetch = now;
      saveCache();
      Serial.printf("Loaded %u TLEs from AMSAT.\n", unsigned(tleCount));
    }
  } else {
    Serial.printf("AMSAT TLE download failed: %d\n", status);
  }
  http.end();
}

bool gridToCoordinates(const char *grid, double &lat, double &lon) {
  const size_t length = strlen(grid);
  if (length != 4 && length != 6) return false;
  if (grid[0] < 'A' || grid[0] > 'R' || grid[1] < 'A' || grid[1] > 'R' ||
      grid[2] < '0' || grid[2] > '9' || grid[3] < '0' || grid[3] > '9') return false;
  lon = -180.0 + (grid[0] - 'A') * 20.0 + (grid[2] - '0') * 2.0;
  lat = -90.0 + (grid[1] - 'A') * 10.0 + (grid[3] - '0');
  if (length == 6) {
    if (grid[4] < 'A' || grid[4] > 'X' || grid[5] < 'A' || grid[5] > 'X') return false;
    lon += (grid[4] - 'A' + 0.5) / 12.0;
    lat += (grid[5] - 'A' + 0.5) / 24.0;
  } else {
    lon += 1.0;
    lat += 0.5;
  }
  return true;
}

bool activateOrbit(const TleRecord &tle, const char *grid) {
  if (strcmp(activeLine1, tle.line1) == 0 && strcmp(activeGrid, grid) == 0) return true;
  double lat, lon;
  if (!gridToCoordinates(grid, lat, lon)) return false;
  char line1[130]{};
  char line2[130]{};
  strlcpy(line1, tle.line1, sizeof(line1));
  strlcpy(line2, tle.line2, sizeof(line2));
  if (!orbit.init(tle.name, line1, line2) && strcmp(orbit.line1, tle.line1) != 0)
    return false;
  orbit.site(lat, lon, 0.0);
  strlcpy(activeLine1, tle.line1, sizeof(activeLine1));
  strlcpy(activeGrid, grid, sizeof(activeGrid));
  return true;
}
}  // namespace

void trackingBegin() {
  loadCache();
  WiFiManager manager;
  manager.setConnectTimeout(10);
  manager.setConfigPortalTimeout(120);
  displayNetworkSetup();
  const bool connected = manager.autoConnect("HamSatTune-CYD");
  if (!connected) Serial.println("WiFi unavailable; showing nominal frequencies.");
  if (WiFi.status() == WL_CONNECTED) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    const uint32_t started = millis();
    while (time(nullptr) < 1700000000 && millis() - started < 12000) delay(100);
    const time_t now = time(nullptr);
    if (now >= 1700000000 && (lastFetch == 0 ||
        now - lastFetch >= FETCH_INTERVAL_SECONDS)) refreshTle(uint32_t(now));
  }
}

bool trackingFreeTuneSupported(const SatelliteProfile &profile) {
  return profile.transponderType == TransponderType::Reverse &&
         profile.uplinkHz != 0 &&
         (strcmp(profile.name, "RS-44") == 0 ||
          strcmp(profile.name, "FO-29") == 0) &&
         (strcmp(profile.comment, "SSB") == 0 ||
          strcmp(profile.comment, "CW") == 0);
}

bool trackingFreeTuneAllowed(const SatelliteProfile &profile,
                            int32_t tuneOffsetHz) {
  if (!trackingFreeTuneSupported(profile)) return tuneOffsetHz == 0;
  const bool cw = strcmp(profile.downlinkMode, "CW") == 0;
  const double downlink = double(profile.downlinkHz) +
                          profile.downlinkOffsetHz + tuneOffsetHz;
  const double uplink = double(profile.uplinkHz) + profile.uplinkOffsetHz -
                        tuneOffsetHz + (cw ? 1000.0 : 0.0);
  const bool rs44 = strcmp(profile.name, "RS-44") == 0;
  return downlink >= (rs44 ? 435610000.0 : 435800000.0) &&
         downlink <= (rs44 ? 435670000.0 : 435900000.0) &&
         uplink >= (rs44 ? 145935000.0 : 145900000.0) &&
         uplink <= (rs44 ? 145995000.0 : 146000000.0);
}

TrackingResult trackingCalculate(const SatelliteProfile &profile,
                                 const char *gridLocator,
                                 int32_t tuneOffsetHz) {
  if (!trackingFreeTuneAllowed(profile, tuneOffsetHz)) tuneOffsetHz = 0;
  const bool cw = strcmp(profile.downlinkMode, "CW") == 0;
  const double nominalRx = double(profile.downlinkHz) + profile.downlinkOffsetHz -
                           (cw ? 1000.0 : 0.0) + tuneOffsetHz;
  const double nominalTx = profile.uplinkHz == 0 ? 0.0 :
                           double(profile.uplinkHz) + profile.uplinkOffsetHz -
                           tuneOffsetHz;
  TrackingResult result{nominalRx, nominalTx, "--:--:-- UTC", "NO UTC",
                        tuneOffsetHz, 0.0, 0.0};

  timeval tv{};
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) return result;
  tm utc{};
  gmtime_r(&tv.tv_sec, &utc);
  snprintf(result.utc, sizeof(result.utc), "%02d:%02d:%02dZ",
           utc.tm_hour, utc.tm_min, utc.tm_sec);

  static uint32_t lastRefreshAttempt = 0;
  if (WiFi.status() == WL_CONNECTED &&
      (lastFetch == 0 || uint32_t(tv.tv_sec) - lastFetch >= FETCH_INTERVAL_SECONDS) &&
      millis() - lastRefreshAttempt >= 60000) {
    lastRefreshAttempt = millis();
    refreshTle(uint32_t(tv.tv_sec));
  }

  double lat, lon;
  if (!gridToCoordinates(gridLocator, lat, lon)) {
    result.status = "SET GRID";
    return result;
  }
  const TleRecord *tle = findTle(profile.name);
  if (!tle) {
    result.status = "NO TLE";
    return result;
  }
  if (!activateOrbit(*tle, gridLocator)) {
    result.status = "TLE ERROR";
    return result;
  }
  const double jd = getJulianFromUnix(double(tv.tv_sec) + tv.tv_usec / 1e6);
  if (fabs(jd - orbit.satrec.jdsatepoch) > MAX_TLE_AGE_DAYS) {
    result.status = "TLE OLD";
    return result;
  }
  orbit.findsat(jd);
  if (orbit.satrec.error != 0 || !isfinite(orbit.satRangeRateKmPerSec)) {
    result.status = "ORBIT ERR";
    return result;
  }
  result.azimuthDeg = orbit.satAz;
  result.elevationDeg = orbit.satEl;
  const double receiveShift = -orbit.satRangeRateKmPerSec * 1000.0 /
                              hamsat::SPEED_OF_LIGHT * nominalRx;
  result.receiveHz += int32_t(receiveShift);
  if (nominalTx > 0.0) {
    double txRangeRate = orbit.satRangeRateKmPerSec;
    if (strcmp(profile.comment, "V/U FT4") == 0) {
      orbit.findsat(jd + 3.75 / 86400.0);
      txRangeRate = orbit.satRangeRateKmPerSec;
    }
    const double transmitShift = -txRangeRate * 1000.0 /
                                 hamsat::SPEED_OF_LIGHT * nominalTx;
    result.transmitHz -= int32_t(transmitShift);
    if (profile.transponderType == TransponderType::Reverse && cw)
      result.transmitHz += 1000.0;
  }
  result.status = "TRACKING";
  return result;
}

bool trackingStationCoordinates(const char *gridLocator, float &latitudeDeg,
                                float &longitudeDeg) {
  double latitude, longitude;
  if (!gridToCoordinates(gridLocator, latitude, longitude)) return false;
  latitudeDeg = static_cast<float>(latitude);
  longitudeDeg = static_cast<float>(longitude);
  return true;
}

bool trackingMapSnapshot(const SatelliteProfile &profile,
                         const char *gridLocator, MapSnapshot &snapshot) {
  if (!trackingStationCoordinates(gridLocator, snapshot.stationLatDeg,
                                  snapshot.stationLonDeg)) return false;
  const TleRecord *tle = findTle(profile.name);
  if (!tle || !activateOrbit(*tle, gridLocator)) return false;
  timeval tv{};
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) return false;
  const double now = getJulianFromUnix(double(tv.tv_sec) + tv.tv_usec / 1e6);
  if (fabs(now - orbit.satrec.jdsatepoch) > MAX_TLE_AGE_DAYS) return false;

  // The map samples a 90-minute ground track. Restore the current orbit at
  // the end so subsequent Doppler calculations see the present position.
  for (size_t i = 0; i < MAP_PATH_POINTS; ++i) {
    const double offsetMinutes = static_cast<double>(i) - 45.0;
    orbit.findsat(now + offsetMinutes / 1440.0);
    if (orbit.satrec.error != 0 || !isfinite(orbit.satLat) ||
        !isfinite(orbit.satLon)) {
      orbit.findsat(now);
      return false;
    }
    snapshot.path[i] = {static_cast<float>(orbit.satLat),
                        static_cast<float>(orbit.satLon)};
  }
  orbit.findsat(now);
  if (orbit.satrec.error != 0 || !isfinite(orbit.satAlt)) return false;
  snapshot.satelliteLatDeg = static_cast<float>(orbit.satLat);
  snapshot.satelliteLonDeg = static_cast<float>(orbit.satLon);
  snapshot.altitudeKm = static_cast<float>(orbit.satAlt);
  return true;
}

namespace {
constexpr uint32_t PASS_STEP_SECONDS = 30;
constexpr uint32_t PASS_LOOKBACK_SECONDS = 3 * 3600;
constexpr uint32_t PASS_HORIZON_SECONDS = 24 * 3600;
Sgp4 passOrbit;
char passGrid[7] = "";
PassPrediction passRows[20]{};
PassPrediction pendingPass{};
size_t passCount = 0;
size_t passProfileIndex = 0;
uint32_t passNow = 0;
uint32_t passStart = 0;
uint32_t passEnd = 0;
uint32_t passTime = 0;
double previousElevation = 0;
bool passPrepared = false;
bool passInside = false;
bool passDone = true;
size_t passCandidates = 0;
const char *passStatus = "NO UTC";

bool duplicatePassSatellite(size_t profileIndex) {
  const char *name = satelliteProfileAt(profileIndex)->name;
  for (size_t i = 0; i < profileIndex; ++i)
    if (strcasecmp(name, satelliteProfileAt(i)->name) == 0) return true;
  return false;
}

void finishPassScan() {
  // Active passes first, then upcoming passes in AOS order.
  for (size_t i = 1; i < passCount; ++i) {
    const PassPrediction value = passRows[i];
    size_t j = i;
    const bool valueActive = value.aosUnix <= passNow;
    while (j > 0) {
      const PassPrediction &previous = passRows[j - 1];
      const bool previousActive = previous.aosUnix <= passNow;
      if (previousActive && !valueActive) break;
      if (previousActive == valueActive &&
          (previousActive ? previous.losUnix : previous.aosUnix) <=
              (valueActive ? value.losUnix : value.aosUnix)) break;
      passRows[j] = previous;
      --j;
    }
    passRows[j] = value;
  }
  passDone = true;
  passStatus = passCount ? "READY" :
      passCandidates ? "NO PASS IN 24H" : "NO CURRENT TLE";
}

bool preparePassSatellite() {
  while (passProfileIndex < satelliteProfileCount()) {
    const size_t index = passProfileIndex;
    const SatelliteProfile *profile = satelliteProfileAt(index);
    if (duplicatePassSatellite(index)) {
      ++passProfileIndex;
      continue;
    }
    const TleRecord *tle = findTle(profile->name);
    if (!tle) {
      ++passProfileIndex;
      continue;
    }
    char line1[130]{};
    char line2[130]{};
    strlcpy(line1, tle->line1, sizeof(line1));
    strlcpy(line2, tle->line2, sizeof(line2));
    if (!passOrbit.init(tle->name, line1, line2) &&
        strcmp(passOrbit.line1, tle->line1) != 0) {
      ++passProfileIndex;
      continue;
    }
    if (fabs(getJulianFromUnix(passNow) - passOrbit.satrec.jdsatepoch) >
        MAX_TLE_AGE_DAYS) {
      ++passProfileIndex;
      continue;
    }
    double latitude, longitude;
    if (!gridToCoordinates(passGrid, latitude, longitude)) {
      ++passProfileIndex;
      continue;
    }
    passOrbit.site(latitude, longitude, 0.0);
    passOrbit.findsat(getJulianFromUnix(passStart));
    if (passOrbit.satrec.error != 0 || !isfinite(passOrbit.satEl)) {
      ++passProfileIndex;
      continue;
    }
    ++passCandidates;
    passTime = passStart;
    previousElevation = passOrbit.satEl;
    passInside = previousElevation >= 0;
    pendingPass = {profile->name, passStart, 0,
                   static_cast<float>(fmax(0.0, previousElevation)),
                   static_cast<float>(passOrbit.satAz), 0};
    passPrepared = true;
    return true;
  }
  finishPassScan();
  return false;
}

uint32_t refinePassCrossing(uint32_t lower, uint32_t upper,
                            bool rising, float &azimuthDeg) {
  while (upper - lower > 1) {
    const uint32_t middle = lower + (upper - lower) / 2;
    passOrbit.findsat(getJulianFromUnix(middle));
    if ((passOrbit.satEl >= 0) == rising) upper = middle;
    else lower = middle;
  }
  passOrbit.findsat(getJulianFromUnix(upper));
  azimuthDeg = static_cast<float>(passOrbit.satAz);
  return upper;
}
}  // namespace

void trackingPassBegin(const char *gridLocator, uint32_t nowUnix) {
  passCount = 0;
  passProfileIndex = 0;
  passCandidates = 0;
  passPrepared = false;
  passDone = true;
  if (nowUnix < 1700000000) {
    passStatus = "NO UTC";
    return;
  }
  double latitude, longitude;
  if (!gridToCoordinates(gridLocator, latitude, longitude)) {
    passStatus = "SET GRID";
    return;
  }
  strlcpy(passGrid, gridLocator, sizeof(passGrid));
  passNow = nowUnix;
  passStart = nowUnix - PASS_LOOKBACK_SECONDS;
  passEnd = nowUnix + PASS_HORIZON_SECONDS;
  passDone = false;
  passStatus = "CALCULATING";
}

bool trackingPassTick(uint8_t sampleBudget) {
  if (passDone) return false;
  while (sampleBudget > 0 && !passDone) {
    if (!passPrepared && !preparePassSatellite()) return true;
    const uint32_t nextTime = passTime + PASS_STEP_SECONDS < passEnd
        ? passTime + PASS_STEP_SECONDS : passEnd;
    passOrbit.findsat(getJulianFromUnix(nextTime));
    if (passOrbit.satrec.error != 0 || !isfinite(passOrbit.satEl)) {
      passPrepared = false;
      ++passProfileIndex;
      continue;
    }
    const double elevation = passOrbit.satEl;
    if (!passInside && previousElevation < 0 && elevation >= 0) {
      pendingPass.aosUnix = refinePassCrossing(passTime, nextTime, true,
                                               pendingPass.aosAzimuthDeg);
      pendingPass.maxElevationDeg = static_cast<float>(elevation);
      passInside = true;
    }
    if (passInside && elevation > pendingPass.maxElevationDeg)
      pendingPass.maxElevationDeg = static_cast<float>(elevation);
    if (passInside && previousElevation >= 0 && elevation < 0) {
      pendingPass.losUnix = refinePassCrossing(passTime, nextTime, false,
                                               pendingPass.losAzimuthDeg);
      if (pendingPass.losUnix > passNow) {
        if (pendingPass.aosUnix <= passEnd &&
            passCount < sizeof(passRows) / sizeof(passRows[0]))
          passRows[passCount++] = pendingPass;
        passPrepared = false;
        ++passProfileIndex;
      } else {
        passInside = false;
        passTime = nextTime;
        previousElevation = elevation;
      }
    } else if (nextTime >= passEnd) {
      passPrepared = false;
      ++passProfileIndex;
    } else {
      passTime = nextTime;
      previousElevation = elevation;
    }
    --sampleBudget;
  }
  return false;
}

bool trackingPassComplete() { return passDone; }
const char *trackingPassStatus() { return passStatus; }
size_t trackingPassCount() { return passCount; }
const PassPrediction *trackingPassAt(size_t index) {
  return index < passCount ? &passRows[index] : nullptr;
}
