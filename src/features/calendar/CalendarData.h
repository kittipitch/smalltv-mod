// CalendarData.h — runtime (volatile) next-event + weather/AQ snapshot.
#pragma once
#include <Arduino.h>

#define CAL_TITLE_LEN 48   // truncated for display well before this; room for the raw push
#define CAL_START_LEN 24   // ISO8601 datetime ("2026-07-27T10:00:00+01:00") or date-only

// Pushed by clawdmeter-daemon's --calendar feature (POST /api/calendar). The
// device never talks to Google directly — see clawdmeter-daemon's own README
// for why (OAuth secrets stay server-side).
struct CalendarEvent {
  char     summary[CAL_TITLE_LEN];
  char     start[CAL_START_LEN];
  bool     allDay;
  bool     hasEvent;   // false = daemon confirmed no upcoming event (not an error)
  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    summary[0] = 0;
    start[0] = 0;
    allDay = false;
    hasEvent = false;
    valid = false;
    lastOkMs = 0;
  }
};

// Fetched directly by the device from Open-Meteo (two independent endpoints —
// see config.h). Each field is independently optional: a temp-only response
// with no AQI (or vice versa) is a normal, expected state, not an error.
struct WeatherData {
  float    tempC;
  int      precipPct;
  bool     hasTemp;
  bool     hasPrecip;

  int      weatherCode;  // WMO weather code (Open-Meteo "current.weather_code")
  bool     hasWeatherCode;

  float    pm25;
  int      aqi;         // US AQI
  bool     hasPm25;
  bool     hasAqi;

  bool     valid;        // at least one field populated at least once
  bool     forecastError; // last forecast (temp/precip) fetch failed
  bool     aqError;       // last air-quality fetch failed
  uint32_t lastOkMs;

  void clear() {
    tempC = 0; precipPct = 0; hasTemp = hasPrecip = false;
    weatherCode = 0; hasWeatherCode = false;
    pm25 = 0; aqi = 0; hasPm25 = hasAqi = false;
    valid = false; forecastError = false; aqError = false;
    lastOkMs = 0;
  }
};
