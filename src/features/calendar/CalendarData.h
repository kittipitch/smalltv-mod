// CalendarData.h — runtime (volatile) next-event + weather/AQ snapshot.
#pragma once
#include <Arduino.h>

#define CAL_TITLE_LEN 48   // truncated for display well before this; room for the raw push
#define CAL_START_LEN 24   // ISO8601 datetime ("2026-07-27T10:00:00+01:00") or date-only
#define CAL_MAX_EVENTS 6   // agenda page shows 3 at a time, cycling through 2 pages when >3

struct CalendarEventItem {
  char summary[CAL_TITLE_LEN];
  char start[CAL_START_LEN];
  char end[CAL_START_LEN];  // mirrors "start": full ISO8601 for timed events,
                            // "YYYY-MM-DD" for all-day. Stored RAW -- the
                            // all-day EXCLUSIVE-end minus-one-day adjustment
                            // is a display concern done at render time in
                            // CalendarMode.cpp, so this field always holds
                            // exactly what the daemon sent.
  bool hasEnd;              // false = daemon sent no "end" (old daemon, or a
                            // single-instant event) -- guarded the same way as
                            // hasColor, render as a plain same-day event
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
      items[i].end[0] = 0;
      items[i].hasEnd = false;
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

#define WX_FC_DAYS 3   // tomorrow, +2, +3 -- today's own conditions are
                        // already shown on the main weather page

struct ForecastDay {
  char day[6];    // "Tmr"/"Mon"/"Tue"/etc, daemon-formatted (fixed English
                   // 3-4 char labels, no date math or i18n done here)
  int  code;      // WMO weather code, same table as WeatherData.weatherCode
  int  hi;        // daily max temp, whole degrees C (daemon-rounded)
  int  lo;        // daily min temp, whole degrees C (daemon-rounded)
  int  precip;    // max precipitation probability that day, %
  int  aqi;       // daily AQI: python-aqi/EPA on that day's max hourly PM2.5
                   // (daemon-side, same algorithm as WeatherData.aqiNow) --
                   // independently optional, see hasAqi below
  bool hasAqi;
};

struct WeatherData {
  float    tempC;
  int      precipPct;
  bool     hasTemp;
  bool     hasPrecip;

  int      weatherCode;  // WMO weather code (Open-Meteo "current.weather_code")
  bool     hasWeatherCode;

  float    uvIndex;      // Open-Meteo "current.uv_index", same forecast call as tempC
  bool     hasUvIndex;

  float    pm25;
  int      aqi;         // US AQI, Open-Meteo's own 24h-rolling-average-derived value
  int      aqiNow;       // US AQI computed daemon-side from THIS hour's pm25 alone
                          // (python-aqi, classic pre-2024 EPA breakpoints) -- a fast-
                          // reacting complement to aqi, not a replacement for it.
  bool     hasPm25;
  bool     hasAqi;
  bool     hasAqiNow;

  char     city[WX_CITY_LEN];  // reverse-geocoded by the daemon, cached there
  bool     hasCity;

  ForecastDay fc[WX_FC_DAYS];
  uint8_t     fcCount;    // 0..WX_FC_DAYS, how many of fc[] are populated
                           // (gates access, same pattern as CalendarEvent.count)
  bool        hasForecast;

  bool     valid;        // at least one field populated at least once
  bool     forecastError; // last forecast (temp/precip) fetch failed
  bool     aqError;       // last air-quality fetch failed
  uint32_t lastOkMs;

  void clear() {
    tempC = 0; precipPct = 0; hasTemp = hasPrecip = false;
    weatherCode = 0; hasWeatherCode = false;
    uvIndex = 0; hasUvIndex = false;
    pm25 = 0; aqi = 0; aqiNow = 0; hasPm25 = hasAqi = hasAqiNow = false;
    city[0] = 0; hasCity = false;
    fcCount = 0; hasForecast = false;
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

// Pushed by clawdmeter-daemon's --codex feature (POST /api/codex). This is
// Codex CLI's real ChatGPT-plan rate-limit quota, read from its own session
// log after a real (free, plan-included) `codex exec` ping -- NOT an OpenAI
// API billing key (an earlier design that used x-ratelimit-* response
// headers was scrapped before ever flashing -- see CLAUDE.md for why: those
// headers reflect per-minute rate-limit headroom, not real usage/spend).
// pct5h/r5h track the shorter window (labeled "5h", when present -- was
// null on the account this was tested against), pctWeek/rWeek the longer
// weekly window (confirmed live: window_minutes=10080).
//
// resetCredits/resetCreditExpireMins: Codex accounts get occasional
// free "full rate-limit reset" credits (found live in the app-server
// RPC's `rateLimitResetCredits` field, alongside the rate limits
// themselves -- not documented anywhere, discovered by reading the raw
// response). resetCredits is how many are currently available;
// resetCreditExpireMins is minutes until the soonest-expiring one lapses
// unused -- the whole point of showing this is to use it before it's
// gone, not just know it exists.
struct CodexData {
  int  pct5h;
  bool hasPct5h;
  int  r5h;             // minutes until the shorter window resets
  bool hasR5h;
  int  pctWeek;
  bool hasPctWeek;
  int  rWeek;           // minutes until the weekly window resets
  bool hasRWeek;

  int  resetCredits;              // count of available free rate-limit resets
  bool hasResetCredits;
  int  resetCreditExpireMins;     // minutes until the soonest one expires unused --
  bool hasResetCreditExpireMins;  // device computes its own urgency bar from this
                                   // against a fixed 7-day window, see CodexMode.cpp

  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    pct5h = 0; hasPct5h = false;
    r5h = 0; hasR5h = false;
    pctWeek = 0; hasPctWeek = false;
    rWeek = 0; hasRWeek = false;
    resetCredits = 0; hasResetCredits = false;
    resetCreditExpireMins = 0; hasResetCreditExpireMins = false;
    valid = false;
    lastOkMs = 0;
  }
};

// Antigravity CLI (`agy`) quota -- {ok,pctPro?,labelPro?,rPro?,pctFlash?,
// labelFlash?,rFlash?}. Two-window shape, same as Codex/z.ai: the daemon's
// poll_antigravity() splits this account's real model configs into a
// Gemini Pro family and a Gemini Flash family (non-Gemini models this
// account also has -- Claude Sonnet/Opus, GPT-OSS -- aren't surfaced here,
// they have their own dedicated pages already) and reports each family's
// LATEST version's tightest-constrained variant separately -- version
// wins over quota (a numerically tighter but older-generation entry, e.g.
// 3.5 Flash, is not picked over a newer one, e.g. 3.6 Flash) -- see that
// function's docstring for the full reasoning. `labelPro`/`labelFlash`
// are "<version> <family>" strings (e.g. "3.6 Flash") -- shown on each
// card since which reasoning-tier variant backs the number can change
// poll to poll. Wide enough that AntigravityMode.cpp's two cards use a
// smaller value font (size3, not every other quota card's size5) to
// leave room for it -- see that file's drawAntigravityMeter() comment.
struct AntigravityData {
  int  pctPro;
  bool hasPctPro;
  char labelPro[16];     // e.g. "3.1 Pro"
  bool hasLabelPro;
  int  rPro;             // minutes until the Pro family's quota resets
  bool hasRPro;

  int  pctFlash;
  bool hasPctFlash;
  char labelFlash[16];   // e.g. "3.6 Flash"
  bool hasLabelFlash;
  int  rFlash;           // minutes until the Flash family's quota resets
  bool hasRFlash;

  bool     valid;      // populated at least once by a successful push
  uint32_t lastOkMs;

  void clear() {
    pctPro = 0; hasPctPro = false;
    labelPro[0] = 0; hasLabelPro = false;
    rPro = 0; hasRPro = false;
    pctFlash = 0; hasPctFlash = false;
    labelFlash[0] = 0; hasLabelFlash = false;
    rFlash = 0; hasRFlash = false;
    valid = false;
    lastOkMs = 0;
  }
};

struct OpenRouterData {
  double usdDaily;   bool hasUsdDaily;
  double usdWeekly;  bool hasUsdWeekly;
  double usdTotal;   bool hasUsdTotal;
  bool   freeTier;   bool hasFreeTier;
  bool     valid;
  uint32_t lastOkMs;
  void clear() {
    usdDaily = 0; hasUsdDaily = false;
    usdWeekly = 0; hasUsdWeekly = false;
    usdTotal = 0; hasUsdTotal = false;
    freeTier = false; hasFreeTier = false;
    valid = false; lastOkMs = 0;
  }
};
