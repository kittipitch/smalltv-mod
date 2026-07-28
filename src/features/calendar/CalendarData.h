// CalendarData.h — runtime (volatile) next-event + weather/AQ snapshot.
#pragma once
#include <Arduino.h>

#define CAL_TITLE_LEN 48   // truncated for display well before this; room for the raw push
#define CAL_START_LEN 24   // ISO8601 datetime ("2026-07-27T10:00:00+01:00") or date-only
#define CAL_MAX_EVENTS 6   // agenda page shows 3 at a time, cycling through 2 pages when >3

struct CalendarEventItem {
  char summary[CAL_TITLE_LEN];
  char start[CAL_START_LEN];
  bool allDay;
  uint16_t color;      // RGB565, source calendar's real Google color
  bool     hasColor;   // false = daemon sent none (old daemon, or the source
                        // calendar has no color set) -- render with the
                        // existing default accent color, not a bogus black
};

// Pushed by clawdmeter-daemon's --calendar feature (POST /api/calendar). The
// device never talks to Google directly — see clawdmeter-daemon's own README
// for why (OAuth secrets stay server-side).
struct CalendarEvent {
  CalendarEventItem items[CAL_MAX_EVENTS];
  uint8_t  count;      // 0 = daemon confirmed no upcoming events (not an error)
  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    for (uint8_t i = 0; i < CAL_MAX_EVENTS; i++) {
      items[i].summary[0] = 0;
      items[i].start[0] = 0;
      items[i].allDay = false;
      items[i].color = 0;
      items[i].hasColor = false;
    }
    count = 0;
    valid = false;
    lastOkMs = 0;
  }
};

// Fetched directly by the device from Open-Meteo (two independent endpoints —
// see config.h). Each field is independently optional: a temp-only response
// with no AQI (or vice versa) is a normal, expected state, not an error.
#define WX_CITY_LEN 24   // truncated for display well before this

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

  char     city[WX_CITY_LEN];  // reverse-geocoded by the daemon, cached there
  bool     hasCity;

  bool     valid;        // at least one field populated at least once
  bool     forecastError; // last forecast (temp/precip) fetch failed
  bool     aqError;       // last air-quality fetch failed
  uint32_t lastOkMs;

  void clear() {
    tempC = 0; precipPct = 0; hasTemp = hasPrecip = false;
    weatherCode = 0; hasWeatherCode = false;
    pm25 = 0; aqi = 0; hasPm25 = hasAqi = false;
    city[0] = 0; hasCity = false;
    valid = false; forecastError = false; aqError = false;
    lastOkMs = 0;
  }
};

// Pushed by clawdmeter-daemon's --zai feature (POST /api/zai). z.ai's own
// quota endpoint (api.z.ai/api/monitor/usage/quota/limit) is undocumented
// and could change shape without notice -- kept to the two percentages
// this device actually needs, everything else parsed defensively.
// pct5h/r5h come from z.ai's "TOKENS_LIMIT" entry (the real rolling
// 5-hour cycle) and pctMcp/rMcp from "TIME_LIMIT" (the real monthly
// MCP-tools quota -- search-prime/web-reader/zread) -- deliberately NOT a
// literal 1:1 mapping of z.ai's own field names, which are backwards from
// what they sound like (TIME_LIMIT's own "usage" cap of 100 matches z.ai
// Lite's published 100/month MCP allowance, not its ~400/week prompt
// allowance). See the daemon's poll_zai() comment for the full evidence
// trail, including the first WRONG mapping this superseded.
struct ZaiData {
  int  pct5h;
  bool hasPct5h;
  int  r5h;           // minutes until the 5h quota resets, same shape as Claude's sr/wr
  bool hasR5h;
  int  pctMcp;
  bool hasPctMcp;
  int  rMcp;           // minutes until the monthly MCP-tools quota resets
  bool hasRMcp;

  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    pct5h = 0; hasPct5h = false;
    r5h = 0; hasR5h = false;
    pctMcp = 0; hasPctMcp = false;
    rMcp = 0; hasRMcp = false;
    valid = false;
    lastOkMs = 0;
  }
};

// Pushed by clawdmeter-daemon's --openai feature (POST /api/openai). This is
// a cheap ping against OpenAI's *paid API* (x-ratelimit-* response headers),
// NOT a ChatGPT Plus subscription check -- no public API exists for that
// (see CLAUDE.md's research notes). pctReq/rReq track the request-rate
// limit, pctTok/rTok the token-rate limit, both from the same ping.
struct OpenAiData {
  int  pctReq;
  bool hasPctReq;
  int  rReq;           // minutes until the request-rate limit resets
  bool hasRReq;
  int  pctTok;
  bool hasPctTok;
  int  rTok;           // minutes until the token-rate limit resets
  bool hasRTok;

  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    pctReq = 0; hasPctReq = false;
    rReq = 0; hasRReq = false;
    pctTok = 0; hasPctTok = false;
    rTok = 0; hasRTok = false;
    valid = false;
    lastOkMs = 0;
  }
};
