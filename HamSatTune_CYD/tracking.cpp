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

TrackingResult trackingCalculate(const SatelliteProfile &profile,
                                 const char *gridLocator) {
  const bool cw = strcmp(profile.downlinkMode, "CW") == 0;
  const double nominalRx = double(profile.downlinkHz) + profile.downlinkOffsetHz -
                           (cw ? 1000.0 : 0.0);
  const double nominalTx = profile.uplinkHz == 0 ? 0.0 :
                           double(profile.uplinkHz) + profile.uplinkOffsetHz;
  TrackingResult result{nominalRx, nominalTx, "--:--:-- UTC", "NO UTC"};

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
