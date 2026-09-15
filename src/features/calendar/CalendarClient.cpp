#include "CalendarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>

static CalendarEvent g_cal;
static WeatherData   g_weather;
static ZaiData        g_zai;
static CodexData      g_codex;
static AntigravityData g_antigravity;
static OpenRouterData g_openrouter;
static bool          g_inited = false;

#if WITH_DEVICE_WEATHER
void weatherNotePush(bool gotFc, bool gotAq);   // defined with the device-direct fetch, below
#endif

void calendarInit(const Settings& s) {
  (void)s;
  g_cal.clear();
  g_weather.clear();
  g_zai.clear();
  g_codex.clear();
  g_antigravity.clear();
  g_openrouter.clear();
  g_inited = true;
}

const CalendarEvent& calendarGet() { return g_cal; }
const WeatherData&   weatherGet()  { return g_weather; }
const ZaiData&        zaiGet()      { return g_zai; }
const CodexData&      codexGet()    { return g_codex; }
const AntigravityData& antigravityGet() { return g_antigravity; }
const OpenRouterData& openrouterGet() { return g_openrouter; }

// Drop non-ASCII bytes (emoji, accented chars) -- this font (CP437 glyph
// table) renders each UTF-8 continuation byte as its own garbage glyph, so an
// emoji in an event title (Google Calendar titles commonly have one) shows as
// a run of nonsense symbols. Printable ASCII only; also collapses the leading
// space a stripped-out leading emoji would otherwise leave behind.
static void stripNonAscii(const char* in, char* out, size_t outCap) {
  size_t o = 0;
  bool sawChar = false;
  for (const char* p = in; *p && o + 1 < outCap; p++) {
    unsigned char ch = (unsigned char)*p;
    if (ch < 0x20 || ch > 0x7E) continue;
    if (ch == ' ' && !sawChar) continue;   // trim leading space left by a stripped char
    out[o++] = (char)ch;
    sawChar = true;
  }
  while (o > 0 && out[o - 1] == ' ') o--;  // trim trailing space too
  out[o] = 0;
}

// ArduinoJson returns a NULL const char* for a key that is absent or of the
// wrong type. strlcpy() and stripNonAscii() both walk that pointer, so a single
// malformed field in a pushed payload would take the device down. The `is<>`
// guards above catch most of it, but not every site had one -- this makes the
// whole class safe by construction. Empty string, not NULL: an empty field
// renders as blank, which is the honest display for "the daemon sent nothing".
static inline const char* jstr(JsonVariantConst v) {
  const char* p = v.as<const char*>();
  return p ? p : "";
}

// ---- calendar: pushed payload ----------------------------------------------
// { "ok":true, "events":[{"summary":"Team sync","start":"2026-07-27T10:00:00+01:00","allDay":false,"color":"7986cb"}, ...] }
// "events" is an empty array (not absent) when there's no upcoming event.
// "color" (hex, no '#') is optional -- the source Google Calendar's own
// backgroundColor, absent if that calendar had none set or on an older
// daemon that predates per-event coloring.
static void calendarFilter(JsonDocument& f) {
  f["ok"] = true;
  JsonObject ev = f["events"].add<JsonObject>();
  // "end" MUST be listed here or ArduinoJson's filter silently drops it
  // before calendarApply() ever sees it -- the field would look absent even
  // though the daemon is sending it correctly. Same trap every other key in
  // this object shares; called out because "end" is newly added.
  ev["summary"] = true; ev["start"] = true; ev["end"] = true; ev["allDay"] = true; ev["color"] = true;
  ev["calendarId"] = true;
}

// Args: 6-char hex string (no '#') -> RGB565. Malformed input (wrong
// length, non-hex chars) is treated as absent by the caller checking
// hasColor before this is invoked, not guarded against here.
static uint16_t hexToRGB565(const char* hex) {
  uint32_t v = (uint32_t)strtoul(hex, nullptr, 16);
  uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

bool calendarApply(const String& body) {
  if (!g_inited) g_cal.clear();
  JsonDocument filter; calendarFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  JsonArrayConst events = doc["events"].as<JsonArrayConst>();
  uint8_t n = 0;
  for (JsonObjectConst ev : events) {
    if (n >= CAL_MAX_EVENTS) break;
    bool hasSummary = ev["summary"].is<const char*>();
    bool hasStart = ev["start"].is<const char*>();
    if (!hasSummary || !hasStart) continue;   // skip malformed entries, don't leave a blank frame
    stripNonAscii(jstr(ev["summary"]), g_cal.items[n].summary, sizeof(g_cal.items[n].summary));
    strlcpy(g_cal.items[n].start, jstr(ev["start"]), sizeof(g_cal.items[n].start));
    // "end" mirrors "start" and is optional (old daemons / single-instant
    // events send none). Stored raw here -- the all-day EXCLUSIVE-end
    // minus-one-day fixup is deliberately NOT applied at parse time; it's a
    // display concern and lives in CalendarMode.cpp, so this buffer always
    // reflects exactly what the daemon sent (easier to reason about than a
    // silently-corrected value stored under the name "end").
    if (ev["end"].is<const char*>()) {
      strlcpy(g_cal.items[n].end, jstr(ev["end"]), sizeof(g_cal.items[n].end));
      g_cal.items[n].hasEnd = true;
    } else {
      g_cal.items[n].end[0] = 0;
      g_cal.items[n].hasEnd = false;
    }
    g_cal.items[n].allDay = ev["allDay"] | false;
    const char* colorStr = ev["color"].is<const char*>() ? ev["color"].as<const char*>() : nullptr;
    if (colorStr && strlen(colorStr) == 6) {
      g_cal.items[n].color = hexToRGB565(colorStr);
      g_cal.items[n].hasColor = true;
    } else {
      g_cal.items[n].color = 0;
      g_cal.items[n].hasColor = false;
    }
    if (ev["calendarId"].is<const char*>()) {
      strlcpy(g_cal.items[n].calId, jstr(ev["calendarId"]), sizeof(g_cal.items[n].calId));
    } else {
      g_cal.items[n].calId[0] = 0;
    }
    n++;
  }
  g_cal.count = n;
  g_cal.valid = true;
  g_cal.lastOkMs = millis();
  return true;
}

// ---- weather + air quality: pushed payload ---------------------------------
// { "ok":true, "tempC":26.0, "precipPct":89, "weatherCode":53, "pm25":4.7, "aqi":38 }
// Any of the data fields may be absent (daemon fetches forecast + AQ from two
// independent Open-Meteo endpoints and only includes what it got). Moved here
// from a device-direct fetch -- see CalendarClient.h for why.
static void weatherFilter(JsonDocument& f) {
  f["ok"] = true; f["tempC"] = true; f["precipPct"] = true;
  f["weatherCode"] = true; f["uvIndex"] = true;
  f["pm25"] = true; f["aqi"] = true; f["aqiNow"] = true; f["city"] = true;
  // Array filter: one representative element describes which fields to
  // keep on EVERY element of "forecast", per ArduinoJson's own filter
  // semantics for arrays (not one filter entry per real array index).
  JsonObject fc = f["forecast"].add<JsonObject>();
  fc["day"] = true; fc["code"] = true; fc["hi"] = true; fc["lo"] = true;
  fc["precip"] = true; fc["aqi"] = true;
}

bool weatherApply(const String& body) {
  JsonDocument filter; weatherFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool gotTemp = doc["tempC"].is<float>() || doc["tempC"].is<int>();
  bool gotPrecip = doc["precipPct"].is<int>();
  bool gotCode = doc["weatherCode"].is<int>();
  bool gotUv = doc["uvIndex"].is<float>() || doc["uvIndex"].is<int>();
  bool gotPm = doc["pm25"].is<float>() || doc["pm25"].is<int>();
  bool gotAqi = doc["aqi"].is<int>();
  bool gotAqiNow = doc["aqiNow"].is<int>();
  bool gotCity = doc["city"].is<const char*>();
  bool gotForecast = doc["forecast"].is<JsonArrayConst>();
  if (!gotTemp && !gotPrecip && !gotCode && !gotUv && !gotPm && !gotAqi && !gotAqiNow && !gotCity && !gotForecast) return false;

  if (gotTemp)   { g_weather.tempC = doc["tempC"].as<float>(); g_weather.hasTemp = true; }
  if (gotPrecip) { g_weather.precipPct = doc["precipPct"].as<int>(); g_weather.hasPrecip = true; }
  if (gotCode)   { g_weather.weatherCode = doc["weatherCode"].as<int>(); g_weather.hasWeatherCode = true; }
  if (gotUv)     { g_weather.uvIndex = doc["uvIndex"].as<float>(); g_weather.hasUvIndex = true; }
  if (gotPm)     { g_weather.pm25 = doc["pm25"].as<float>(); g_weather.hasPm25 = true; }
  if (gotAqi)    { g_weather.aqi = doc["aqi"].as<int>(); g_weather.hasAqi = true; }
  if (gotAqiNow) { g_weather.aqiNow = doc["aqiNow"].as<int>(); g_weather.hasAqiNow = true; }
  if (gotCity)   { stripNonAscii(jstr(doc["city"]), g_weather.city, sizeof(g_weather.city)); g_weather.hasCity = true; }

  // "forecast" is independently optional like every other field here, but
  // unlike a stale single value (a slightly-old temp/AQI number still
  // reads as roughly right), stale forecast days show wrong DATES once
  // the real date has moved on -- worse than showing nothing. So a push
  // that omits "forecast" entirely explicitly clears it rather than
  // leaving the last-parsed days in place (State's keep-last-good only
  // preserves the previous payload on a whole-poll failure; a poll that
  // succeeds overall via a different field, e.g. AQI, while its own
  // forecast fetch specifically failed, is a real and reachable case --
  // confirmed live this session that partial poll failures happen).
  if (gotForecast) {
    JsonArrayConst forecast = doc["forecast"].as<JsonArrayConst>();
    uint8_t n = 0;
    for (JsonObjectConst day : forecast) {
      if (n >= WX_FC_DAYS) break;
      bool hasDay = day["day"].is<const char*>();
      bool hasCode = day["code"].is<int>();
      bool hasHi = day["hi"].is<int>();
      bool hasLo = day["lo"].is<int>();
      if (!hasDay || !hasCode || !hasHi || !hasLo) continue;   // skip malformed entries
      strlcpy(g_weather.fc[n].day, jstr(day["day"]), sizeof(g_weather.fc[n].day));
      g_weather.fc[n].code = day["code"].as<int>();
      g_weather.fc[n].hi = day["hi"].as<int>();
      g_weather.fc[n].lo = day["lo"].as<int>();
      g_weather.fc[n].precip = day["precip"].is<int>() ? day["precip"].as<int>() : -1;
      if (day["aqi"].is<int>()) {
        g_weather.fc[n].aqi = day["aqi"].as<int>();
        g_weather.fc[n].hasAqi = true;
      } else {
        g_weather.fc[n].hasAqi = false;
      }
      n++;
    }
    g_weather.fcCount = n;
    g_weather.hasForecast = n > 0;
  } else if (gotTemp || gotPrecip || gotCode || gotUv) {
    // Clear the days ONLY when this push carried the forecast half and omitted
    // them -- that means the sender has current conditions and no days, so the
    // old days are genuinely gone. An AQ-ONLY push says nothing about the
    // forecast, and wiping fc[] on it destroyed whatever the device had just
    // fetched for itself: with a daemon whose forecast half fails while AQ
    // succeeds (a confirmed live case), every AQ push erased the device's own
    // forecast and the page sat on "No forecast yet".
    g_weather.fcCount = 0;
    g_weather.hasForecast = false;
  }

  // The forecast half is owned by EITHER current conditions or a days array. A
  // push carrying only "forecast" replaces fc[] -- so it must stamp the same
  // clock and satisfy the same mid-cycle abort, or wxStepAqForecast() decorates
  // those pushed rows as though this cycle had built them.
  bool gotFcHalf = gotTemp || gotPrecip || gotCode || gotUv || gotForecast;
  g_weather.forecastError = !(gotTemp || gotPrecip || gotCode || gotUv);
  g_weather.aqError = !(gotPm || gotAqi || gotAqiNow);
  g_weather.valid = true;
  g_weather.lastOkMs = millis();
  g_weather.renderGen++;
#if WITH_DEVICE_WEATHER
  // Pass which halves this payload actually carried -- same booleans the error
  // flags above are derived from, so the clocks and the flags can never disagree.
  weatherNotePush(gotFcHalf,
                  gotPm || gotAqi || gotAqiNow);
#endif
  return true;
}

// ---- z.ai quota: pushed payload --------------------------------------------
// { "ok":true, "pct5h":12, "pctTokens":3 }
// Mirrors weatherApply()'s shape -- each field independently optional so a
// partial/changed upstream response doesn't drop the whole push.
static void zaiFilter(JsonDocument& f) {
  f["ok"] = true; f["pct5h"] = true; f["r5h"] = true;
  f["pctMcp"] = true; f["rMcp"] = true;
}

bool zaiApply(const String& body) {
  JsonDocument filter; zaiFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool got5h = doc["pct5h"].is<int>();
  bool gotMcp = doc["pctMcp"].is<int>();
  if (!got5h && !gotMcp) return false;

  if (got5h)  { g_zai.pct5h = doc["pct5h"].as<int>(); g_zai.hasPct5h = true; }
  if (gotMcp) { g_zai.pctMcp = doc["pctMcp"].as<int>(); g_zai.hasPctMcp = true; }
  if (doc["r5h"].is<int>())  { g_zai.r5h = doc["r5h"].as<int>(); g_zai.hasR5h = true; }
  if (doc["rMcp"].is<int>()) { g_zai.rMcp = doc["rMcp"].as<int>(); g_zai.hasRMcp = true; }
  g_zai.valid = true;
  g_zai.lastOkMs = millis();
  return true;
}

// ---- Codex quota: pushed payload -------------------------------------------
// { "ok":true, "pct5h":12, "r5h":3, "pctWeek":5, "rWeek":10075 }
// Codex CLI's real ChatGPT-plan rate limit, read from its own session log
// after a real (free, plan-included) ping -- NOT an OpenAI API billing key.
// Same optional-field shape as zaiApply() (a window not populated on this
// account/plan tier is simply absent, not a fake 0).
static void codexFilter(JsonDocument& f) {
  f["ok"] = true; f["pct5h"] = true; f["r5h"] = true;
  f["pctWeek"] = true; f["rWeek"] = true;
  f["resetCredits"] = true; f["resetCreditExpireMins"] = true;
}

bool codexApply(const String& body) {
  JsonDocument filter; codexFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool got5h = doc["pct5h"].is<int>();
  bool gotWeek = doc["pctWeek"].is<int>();
  if (!got5h && !gotWeek) return false;

  if (got5h)   { g_codex.pct5h = doc["pct5h"].as<int>(); g_codex.hasPct5h = true; }
  if (gotWeek) { g_codex.pctWeek = doc["pctWeek"].as<int>(); g_codex.hasPctWeek = true; }
  if (doc["r5h"].is<int>())   { g_codex.r5h = doc["r5h"].as<int>(); g_codex.hasR5h = true; }
  if (doc["rWeek"].is<int>()) { g_codex.rWeek = doc["rWeek"].as<int>(); g_codex.hasRWeek = true; }
  if (doc["resetCredits"].is<int>()) {
    g_codex.resetCredits = doc["resetCredits"].as<int>();
    g_codex.hasResetCredits = true;
  }
  if (doc["resetCreditExpireMins"].is<int>()) {
    g_codex.resetCreditExpireMins = doc["resetCreditExpireMins"].as<int>();
    g_codex.hasResetCreditExpireMins = true;
  }
  g_codex.valid = true;
  g_codex.lastOkMs = millis();
  return true;
}

// ---- Antigravity quota: pushed payload -------------------------------------
// { "ok":true, "pctModel":2, "rModel":291 }
// Antigravity CLI (`agy`) quota -- unlike Codex, each daemon poll fires a
// real, costed prompt (see clawdmeter_daemon.py's poll_antigravity()), so
// this fires once per interval, same cost as the old single-card shape --
// the two-family split (Pro/Flash) below is free, it's just how the
// already-returned model list gets parsed on the daemon side.
static void antigravityFilter(JsonDocument& f) {
  f["ok"] = true;
  f["pctPro"] = true; f["labelPro"] = true; f["rPro"] = true;
  f["pctFlash"] = true; f["labelFlash"] = true; f["rFlash"] = true;
}

bool antigravityApply(const String& body) {
  JsonDocument filter; antigravityFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool gotAny = doc["pctPro"].is<int>() || doc["pctFlash"].is<int>();
  if (!gotAny) return false;

  if (doc["pctPro"].is<int>()) { g_antigravity.pctPro = doc["pctPro"].as<int>(); g_antigravity.hasPctPro = true; }
  if (doc["labelPro"].is<const char*>()) {
    strlcpy(g_antigravity.labelPro, jstr(doc["labelPro"]), sizeof(g_antigravity.labelPro));
    g_antigravity.hasLabelPro = true;
  }
  if (doc["rPro"].is<int>()) { g_antigravity.rPro = doc["rPro"].as<int>(); g_antigravity.hasRPro = true; }

  if (doc["pctFlash"].is<int>()) { g_antigravity.pctFlash = doc["pctFlash"].as<int>(); g_antigravity.hasPctFlash = true; }
  if (doc["labelFlash"].is<const char*>()) {
    strlcpy(g_antigravity.labelFlash, jstr(doc["labelFlash"]), sizeof(g_antigravity.labelFlash));
    g_antigravity.hasLabelFlash = true;
  }
  if (doc["rFlash"].is<int>()) { g_antigravity.rFlash = doc["rFlash"].as<int>(); g_antigravity.hasRFlash = true; }

  g_antigravity.valid = true;
  g_antigravity.lastOkMs = millis();
  return true;
}

// ---- OpenRouter quota: pushed payload -------------------------------------
// { "ok":true, "usd_daily":0.035, "usd_weekly":0.12, "usd_total":4.56, "free_tier":true }
// OpenRouter API spend is daemon-pushed, same device-side shape as the sibling
// quota pages: parse defensively, keep optional fields independent, and only
// accept a push when at least one real field landed.
static void openrouterFilter(JsonDocument& f) {
  f["ok"] = true;
  f["usd_daily"] = true; f["usd_weekly"] = true; f["usd_total"] = true; f["free_tier"] = true;
}

bool openrouterApply(const String& body) {
  JsonDocument filter; openrouterFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool gotDaily = doc["usd_daily"].is<float>() || doc["usd_daily"].is<int>();
  bool gotWeekly = doc["usd_weekly"].is<float>() || doc["usd_weekly"].is<int>();
  bool gotTotal = doc["usd_total"].is<float>() || doc["usd_total"].is<int>();
  bool gotFreeTier = doc["free_tier"].is<bool>();
  // free_tier alone is metadata, not a metric -- a payload carrying only it
  // (e.g. an odd API response shape where the usd_* fields didn't parse)
  // must not count as "useful data": it would mark the page valid and put
  // it in the carousel showing three "--" rows with a confident FREE/PAID
  // subtitle above them. Require at least one real spend figure, same as
  // every sibling quota parser (codex sol pre-flash audit, 2026-08-25).
  if (!gotDaily && !gotWeekly && !gotTotal) return false;

  if (gotDaily) { g_openrouter.usdDaily = doc["usd_daily"].as<double>(); g_openrouter.hasUsdDaily = true; }
  if (gotWeekly) { g_openrouter.usdWeekly = doc["usd_weekly"].as<double>(); g_openrouter.hasUsdWeekly = true; }
  if (gotTotal) { g_openrouter.usdTotal = doc["usd_total"].as<double>(); g_openrouter.hasUsdTotal = true; }
  if (gotFreeTier) { g_openrouter.freeTier = doc["free_tier"].as<bool>(); g_openrouter.hasFreeTier = true; }

  g_openrouter.valid = true;
  g_openrouter.lastOkMs = millis();
  return true;
}

// ===========================================================================
// Device-direct weather + AQI fetch (WITH_DEVICE_WEATHER)
//
// Restores the pre-daemon behaviour as a FALLBACK, not a replacement: a push
// to /api/weather always wins while one keeps arriving, so a daemon-fed device
// behaves exactly as before and keeps the daemon's better logging. Only a
// device with nothing pushing to it fetches for itself.
//
// Deliberately plain HTTP. Open-Meteo serves both endpoints over http://, so
// this costs no BearSSL heap -- the ~16 KB TLS buffer is what made the old
// device-direct path fragile on a board with ~28 KB free.
//
// The daemon does two things this cannot: reverse-geocode a city name (a third
// API) and label days from real dates. City is simply left to whatever a push
// last set. Day labels are derived from the device clock instead.
// ===========================================================================
#if WITH_DEVICE_WEATHER
#include <math.h>
#include "Net.h"
#include "Clock.h"

void weatherNotePush(bool gotFc, bool gotAq);   // fwd
static int g_wxHttpCode = 0;   // set by wxGetJson for the caller to record
// Seeded, not 0: a daemon-fed device would otherwise fetch once on the very
// first loop tick, before the daemon's first push has had a chance to arrive.
// 45 s lets a live daemon claim the device; one with no daemon waits 45 s once.
static uint32_t g_wxNextMs   = 45000;
static uint32_t g_wxFcOkMs   = 0;   // last GOOD forecast (per-half staleness)
// Last device-direct fetch outcome, surfaced on /api/status. This board has no
// usable serial, and a silent failure is exactly what hid a truncated URL
// during development -- so the result has to be visible over HTTP.
// PUBLISHED codes: the last COMPLETED cycle's verdict. Only wxStepFinish() and
// weatherNotePush() write these, and linkState() reads only these.
static int      g_wxLastFc   = 0;   // HTTP code, or -1 parse fail, 0 = not tried
static int      g_wxLastAq   = 0;
static int      g_wxLastAqF  = 0;   // per-day AQI forecast, same encoding
static uint32_t g_wxTriedMs  = 0;
// IN-FLIGHT codes: what the current cycle's steps have recorded so far. Split
// out because zeroing the published codes when a cycle OPENS destroyed the
// still-current offline evidence: linkState() reads fc == 0 as LINK_OK, so
// every retry during one continuous outage flashed the normal page (clearing
// g_linkDownSince and g_offlineShown) until both halves had failed again, then
// restarted the offline grace from zero -- the notice never showed the true
// outage duration. Steps write these; only a COMPLETED cycle publishes them.
static int      g_wxCurFc    = 0;
static int      g_wxCurAq    = 0;
static int      g_wxCurAqF   = 0;
static uint32_t g_wxAqOkMs   = 0;   // last GOOD air quality
// Aggregate push clock. Kept for diagnostics only -- suppression is decided by
// the PER-HALF clocks below, so a daemon feeding one half cannot starve the other.
static uint32_t g_wxPushOkMs = 0;
                                    // ANY push, used for fetch suppression and
                                    // as proof of link -- not for retiring data.
// Per-half push clocks. The single clock above cannot say WHICH half a push
// actually refreshed, and weatherApply() treats a payload carrying only one
// half as perfectly normal (the daemon fetches forecast and air quality from
// two independent endpoints and sends what it got -- clawdmeter_daemon.py only
// sets "aqi" when us_aqi came back). With one shared clock an AQ-less push kept
// rolling it forever, so the AQ retire gate never fired and stale pushed AQ was
// pinned on screen indefinitely; symmetric for a forecast-less push. Found by
// codex AND zask independently in the ac302d9 re-audit.
static uint32_t g_wxFcPushOkMs = 0;   // last push that carried forecast/current
static uint32_t g_wxAqPushOkMs = 0;   // last push that carried air quality

// US AQI from a raw PM2.5 reading, classic (pre-2024) EPA breakpoints -- the
// same table python-aqi uses daemon-side, so aqiNow means the same thing
// whichever source filled it.
static int wxAqiFromPm25(float pm) {
  static const float cLo[] = {  0.0f, 12.1f, 35.5f,  55.5f, 150.5f, 250.5f };
  static const float cHi[] = { 12.0f, 35.4f, 55.4f, 150.4f, 250.4f, 500.4f };
  static const int   iLo[] = {     0,    51,   101,     151,    201,    301 };
  static const int   iHi[] = {    50,   100,   150,     200,    300,    500 };
  if (pm < 0) return -1;
  for (uint8_t i = 0; i < 6; i++)
    if (pm <= cHi[i])
      return (int)lroundf((iHi[i] - iLo[i]) / (cHi[i] - cLo[i]) * (pm - cLo[i]) + iLo[i]);
  return 500;   // above the table; treat as the ceiling rather than failing
}

// "Tmr" for tomorrow, then weekday abbreviations. Derived from the device's
// own clock rather than parsing Open-Meteo's ISO dates: the response is always
// today-first, so an offset is enough and avoids date arithmetic.
static const char* const kWxWd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// Weekday straight from the response's own ISO date ("2026-09-12"), so the
// label matches the data regardless of what the device clock thinks. The clock
// path below assumed the device timezone equals the location's timezone=auto
// one; after a move whose browser timezone lookup failed (it is best-effort and
// keeps the old zone on failure), the values came from the new location's local
// days while the labels came from the old zone -- off by one near a boundary.
// Sakamoto's algorithm: integer-only, no time_t, valid across the range here.
static bool wxDayLabelFromIso(const char* iso, int offset, char* out, size_t n) {
  if (!iso) return false;
  int y = 0, m = 0, d = 0;
  if (sscanf(iso, "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
  if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1970) return false;
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  int yy = y - (m < 3 ? 1 : 0);
  int wd = (yy + yy / 4 - yy / 100 + yy / 400 + t[m - 1] + d) % 7;
  if (wd < 0 || wd > 6) return false;
  if (offset == 1) { strlcpy(out, "Tmr", n); return true; }
  strlcpy(out, kWxWd[wd], n);
  return true;
}

static void wxDayLabel(int offset, char* out, size_t n) {
  struct tm now;
  if (offset == 1) { strlcpy(out, "Tmr", n); return; }
  if (!clockNow(now)) { snprintf(out, n, "+%dd", offset); return; }
  strlcpy(out, kWxWd[(now.tm_wday + offset) % 7], n);
}

// Enforces a WHOLE-REQUEST deadline on the body read. ArduinoJson pulls bytes
// through the Stream interface, so once the deadline passes this reports "no
// more data" and deserializeJson() ends with IncompleteInput -- recorded as a
// parse failure (-100), which is honest: we reached the server and did not get
// a usable body. Without it a slow-drip peer holds loop() indefinitely.
class WxDeadlineStream : public Stream {
 public:
  WxDeadlineStream(Stream& src, uint32_t deadlineMs) : src_(src), dl_(deadlineMs) {
    // INHERIT the source's inter-byte timeout. Stream::_timeout defaults to 1000 ms
    // (Stream.h), but HTTPClient::connect() sets 3000 ms on the WiFiClient, and the
    // parent commit passed that client to ArduinoJson directly. Wrapping it without
    // this line silently cut the per-byte patience to a third: ArduinoJson reads via
    // Stream::readBytes -> timedRead, which gives up after _timeout, so an ordinary
    // lwIP retransmit gap (RTO starts at 3 s) mid-body would abort the parse as -100.
    // On a lossy link both halves fail, the cycle achieves nothing, and the backoff
    // climbs to 30 min -- hours with no weather on exactly the marginal Wi-Fi this
    // fallback exists to serve.
    //
    // THE TRADE, made deliberately: timedRead() gives up _timeout after the
    // deadline passes, so a larger value lets one request overrun its deadline by
    // that much (8 s deadline + 3 s = ~11 s worst case in a single loop() pass,
    // versus ~9 s at 1000). That is acceptable -- timedRead() calls yield(), so
    // the software WDT is fed throughout, and each endpoint is its own loop()
    // pass. Bounding the overrun tighter is not worth converting ordinary packet
    // loss into a failed cycle, because the deadline is a safety net against a
    // slow-drip peer, not a precision timer.
    setTimeout(src.getTimeout());
  }
  bool expired() const { return (int32_t)(millis() - dl_) >= 0; }
  int available() override { return expired() ? 0 : src_.available(); }
  int read() override      { return expired() ? -1 : src_.read(); }
  int peek() override      { return expired() ? -1 : src_.peek(); }
  size_t write(uint8_t b) override { return src_.write(b); }
 private:
  Stream&  src_;
  uint32_t dl_;
};

static bool wxGetJson(const Settings& s, const String& url,
                      JsonDocument& doc, JsonDocument& filter) {
  std::unique_ptr<NetClient> client(new WiFiClient());   // plain HTTP, no TLS heap
  HTTPClient http;
  // Bounds DNS, connect and header-wait SEPARATELY, so the default 8000 would
  // stall loop() for ~24 s per endpoint. Weather is not worth freezing the UI for.
  http.setTimeout(3000);
  http.setReuse(false);
  // Open-Meteo answers HTTP/1.1 with Transfer-Encoding: chunked, and getStream()
  // is the RAW socket -- the ESP8266 core only de-chunks in writeToStream()/
  // getString(). Parsing straight off the stream would hand ArduinoJson
  // "14f\r\n{..." and fail on the first byte. HTTP/1.0 forbids chunked, so the
  // body arrives plain. Same fix and same reason as OtaUpdate.cpp.
  http.useHTTP10(true);
  g_wxHttpCode = 0;            // no stale value if begin()/GET never set one
  const uint32_t deadline = millis() + WX_REQ_DEADLINE_MS;
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  // The phases before this each had their own 3 s bound; together they can
  // still exceed the whole-request budget, so stop here rather than start a
  // body read that would run past it.
  if ((int32_t)(millis() - deadline) >= 0) {
    Serial.println("[wx] deadline hit before body");
    g_wxHttpCode = -100;
    http.end();
    return false;
  }
  g_wxHttpCode = code;
  if (code != HTTP_CODE_OK) {
    Serial.printf("[wx] GET failed: HTTP %d\n", code);   // silence is what hid a truncated URL
    http.end(); return false;
  }
  WxDeadlineStream body(http.getStream(), deadline);
  DeserializationError err = deserializeJson(doc, body,
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    // A 200 whose body will not parse must NOT be reported as 200, or the
    // diagnostic points at the wrong layer entirely.
    Serial.printf("[wx] parse failed: %s\n", err.c_str());
    g_wxHttpCode = -100;
    return false;
  }
  return true;
}
#endif  // WITH_DEVICE_WEATHER

#if WITH_DEVICE_WEATHER
// True when both success clocks are older than staleMs, so the data they vouch
// for can be retired. Either clock may be 0 ("this source never succeeded"),
// which must neither vouch for the data nor block the retire.
//
// Both halves of that rule were live defects, found by codex's b6780be
// pre-flash audit:
//   - Requiring the FETCH clock (g_wxFcOkMs != 0) meant a daemon-fed unit that
//     rebooted could never retire: its self-fetch has never succeeded, so the
//     clock is 0, and pushed values -- including day labels that go wrong after
//     midnight -- would sit on screen forever once pushes stopped.
//   - The AQ half consulted only its own fetch clock, so an OLD self-fetch
//     could erase air-quality data a daemon had pushed moments earlier.
static bool wxAllSourcesStale(uint32_t fetchOkMs, uint32_t pushOkMs, uint32_t staleMs) {
  if (!fetchOkMs && !pushOkMs) return false;   // nothing ever arrived; nothing to retire
  if (fetchOkMs && (millis() - fetchOkMs) <= staleMs) return false;
  if (pushOkMs  && (millis() - pushOkMs)  <= staleMs) return false;
  return true;
}

// Two independent endpoints, exactly as the daemon does it: a temp-only result
// with no AQI (or the reverse) is a normal state the display already handles,
// so each half sets its own error flag and neither aborts the other.
// ---- fetch cycle: ONE endpoint per loop() pass ----------------------------
// Was a single function doing all three GETs back to back. Each HTTPClient call
// bounds DNS, connect, header-silence and per-byte body gaps SEPARATELY, so a
// dead endpoint costs ~9 s and a full cycle ~27 s -- and handleClient() runs
// only before weatherService() (main.cpp), so the web server, the ONLY OTA
// recovery path this board has, was frozen for the whole stall.
//
// Measured cost on a healthy link is ~96 ms for the cycle, so this is a
// failure-mode fix, not a performance one: the point is that a unit whose
// internet is broken must still answer its web UI.
enum : uint8_t { WX_IDLE = 0, WX_STEP_FC, WX_STEP_AQ, WX_STEP_AQF, WX_STEP_DONE };
static uint8_t  g_wxStep       = WX_IDLE;
static bool     g_wxCycFcFresh = false;
// Local day-of-year when the forecast step built fc[]. The AQ-forecast step
// runs in a LATER loop() pass and reduces hours from the location's local
// midnight, so if local midnight falls between the two, fc[] still holds the
// old tomorrow/+2/+3 horizon while the hourly series has already rolled --
// putting each day's AQI on the wrong row until the next cycle.
static int      g_wxCycFcYday  = -1;
static bool     g_wxCycGotAny  = false;
static bool     g_wxCycWantFc  = false;   // this cycle's per-half eligibility
static bool     g_wxCycWantAq  = false;
static uint8_t  g_wxFailStreak = 0;
static uint32_t g_wxIntervalMs = 60000;   // interval actually in force (backoff included)

// How long a recorded failure still counts as CURRENT evidence. linkState()
// needs this rather than period*2: once backoff stretches the gap between
// attempts, a fixed multiple of the poll period expires mid-outage and the
// DEVICE OFFLINE verdict silently un-sticks while the outage continues.
uint8_t weatherFailStreak() { return g_wxFailStreak; }

uint32_t weatherAttemptWindowMs() {
  uint32_t w = g_wxIntervalMs * 2;
  return w < 120000UL ? 120000UL : w;
}

static void wxStepForecast(const Settings& s) {
  if (!g_wxCycWantFc) return;      // a daemon is feeding this half; leave it alone
  char url[320];   // the forecast request is 261 chars rendered; 220 truncated it
  // --- forecast: current conditions + the next 3 days ---
  snprintf(url, sizeof(url),
           "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,precipitation_probability,weather_code,uv_index"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&forecast_days=4&timezone=auto",
           s.calendar.lat, s.calendar.lon);
  {
    JsonDocument filter;
    JsonObject cur = filter["current"].to<JsonObject>();
    cur["temperature_2m"] = true; cur["precipitation_probability"] = true;
    cur["weather_code"] = true;   cur["uv_index"] = true;
    JsonObject day = filter["daily"].to<JsonObject>();
    // 4 short date strings (~44 B). The HOURLY series is still excluded for the
    // heap reason documented below -- that one is 96 timestamps.
    day["time"] = true;
    day["weather_code"] = true; day["temperature_2m_max"] = true;
    day["temperature_2m_min"] = true; day["precipitation_probability_max"] = true;
    JsonDocument doc;
    if (wxGetJson(s, url, doc, filter)) {
      JsonObject c = doc["current"];
      // Did THIS response carry anything? The has* flags persist from earlier
      // cycles, so they cannot answer that -- and without a local witness an
      // empty but well-formed body ("{}") recorded a 200 and stamped freshness
      // on data nobody sent.
      bool gotCur = false;
      if (!c.isNull()) {
        if (c["temperature_2m"].is<float>())            { g_weather.tempC = c["temperature_2m"]; g_weather.hasTemp = true; gotCur = true; }
        if (c["precipitation_probability"].is<float>() || c["precipitation_probability"].is<int>())
                                                        { g_weather.precipPct = c["precipitation_probability"]; g_weather.hasPrecip = true; gotCur = true; }
        if (c["weather_code"].is<float>() || c["weather_code"].is<int>())
                                                        { g_weather.weatherCode = c["weather_code"]; g_weather.hasWeatherCode = true; gotCur = true; }
        if (c["uv_index"].is<float>())                  { g_weather.uvIndex = c["uv_index"]; g_weather.hasUvIndex = true; gotCur = true; }
      }
      JsonArray codes = doc["daily"]["weather_code"];
      JsonArray hi    = doc["daily"]["temperature_2m_max"];
      JsonArray lo    = doc["daily"]["temperature_2m_min"];
      JsonArray pp    = doc["daily"]["precipitation_probability_max"];
      uint8_t n = 0;
      // Bound the loop by EACH array's own size, not just weather_code's: a
      // truncated response that shortens hi/lo below codes must not read past
      // them (they would degrade to 0 via the unbound variant -- a fabricated
      // reading, and the exact class this fetch was re-audited for).
      size_t fcLim = codes.size();
      if (hi.size() < fcLim) fcLim = hi.size();
      if (lo.size() < fcLim) fcLim = lo.size();
      // index 0 is today, already shown on the main page -- start at tomorrow
      for (uint8_t i = 1; i < 4 && n < WX_FC_DAYS && i < fcLim; i++) {
        // Require real numbers. operator| coerces a null or a string to 0, so a
        // partial upstream response became a fabricated clear 0/0 degree day --
        // and then marked the forecast FRESH. The pushed parser already rejects
        // malformed entries; this half must match it.
        if (!(codes[i].is<int>() || codes[i].is<float>())) continue;
        if (!(hi[i].is<int>()    || hi[i].is<float>()))    continue;
        if (!(lo[i].is<int>()    || lo[i].is<float>()))    continue;
        ForecastDay& f = g_weather.fc[n];
        JsonArray dts = doc["daily"]["time"];
        const char* iso = (i < dts.size()) ? dts[i].as<const char*>() : nullptr;
        if (!wxDayLabelFromIso(iso, i, f.day, sizeof(f.day)))
          wxDayLabel(i, f.day, sizeof(f.day));   // no/!parseable date -> clock
        f.code   = codes[i] | 0;
        f.hi     = (int)lroundf(hi[i] | 0.0f);
        f.lo     = (int)lroundf(lo[i] | 0.0f);
        // as<float>(), not "| 0": operator| only matches integer-typed values,
        // so a float-typed 55.0 would silently read as 0. Same rule as pm2_5
        // below. -1 = no data, as weatherApply() stores.
        f.precip = (i < pp.size() && (pp[i].is<float>() || pp[i].is<int>()))
                       ? (int)pp[i].as<float>() : -1;
        f.aqi    = 0; f.hasAqi = false;   // no per-day AQI without the extra call
        n++;
      }
      // Publish the parsed days ONLY on the success branch. Assigning fcCount here
      // unconditionally erased fc[] on the -105 path (reachable but empty) without
      // touching renderGen, so the forecast page kept painting the old days with no
      // dirty signal to correct it -- the same mutate-without-signalling class the
      // retirement fix addressed.
      if (!gotCur && n == 0) {
        // Reachable, well-formed, and empty. That is a server-side problem, not
        // fresh data: recording 200 here would keep the retire logic asleep and
        // leave yesterday's readings on screen looking current. -105 still
        // proves we reached the internet, so linkState() correctly reads it as
        // evidence FOR the link (it is absent from noResponse()).
        g_weather.forecastError = true;
        g_wxCurFc = -105;
      } else {
      g_weather.fcCount = n;
      g_weather.hasForecast = (n > 0);
      g_weather.forecastError = false;
      g_wxFcOkMs = millis(); if (!g_wxFcOkMs) g_wxFcOkMs = 1;
      g_wxCycGotAny = true;
      g_wxCycFcFresh = true;
      { time_t now = time(nullptr); struct tm* lt = localtime(&now);
        g_wxCycFcYday = lt ? lt->tm_yday : -1; }
      g_wxCurFc = 200;
      }
    } else {
      g_weather.forecastError = true;
      g_wxCurFc = g_wxHttpCode ? g_wxHttpCode : -1;
    }
  }

}

static void wxStepAq(const Settings& s) {
  if (!g_wxCycWantAq) return;      // daemon-fed half
  char url[320];
  // --- air quality: this hour's PM2.5, plus Open-Meteo's own rolling US AQI ---
  //
  // This runs UNCONDITIONALLY, including right after the forecast GET failed at
  // transport level. An earlier version skipped it in that case and synthesized
  // `g_wxCurAq = -104` in place of a real result, to save ~6 s of loop() stall
  // during an outage. That was circular and it cost a real bug (codex, ac302d9
  // re-audit): linkState() declares LINK_NO_INTERNET only when BOTH halves
  // failed without a response, and a synthesized code made ONE host failure look
  // like TWO independent probes. Because this is a DIFFERENT hostname from the
  // forecast host, a forecast-host-only outage then seized the whole screen with
  // DEVICE OFFLINE on a device that was demonstrably online.
  //
  // The stall is the cheaper price. A wrong DEVICE OFFLINE hides every working
  // page on the unit and does not self-correct while that host stays down; the
  // stall only slows the web portal, and this fetch runs at all only when no
  // daemon is pushing. This GET is now the sole INDEPENDENT evidence linkState()
  // has, which is exactly why it must actually be attempted.
  //
  // MUST write a failure code here, never leave the previous cycle's 200:
  // a sticky 200 would make the offline verdict unreachable -- trading a false
  // positive for a false negative on a screen whose entire job is to say which
  // side of the link actually broke.
snprintf(url, sizeof(url),
         "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
         "&current=pm2_5,us_aqi",
         s.calendar.lat, s.calendar.lon);
{
  JsonDocument filter;
  JsonObject cur = filter["current"].to<JsonObject>();
  cur["pm2_5"] = true; cur["us_aqi"] = true;
  JsonDocument doc;
  if (wxGetJson(s, url, doc, filter)) {
    JsonObject c = doc["current"];
    bool gotAqVals = false;   // see gotCur above: has* flags persist, this does not
    if (!c.isNull()) {
      if (c["pm2_5"].is<float>()) {
        g_weather.pm25 = c["pm2_5"]; g_weather.hasPm25 = true; gotAqVals = true;
        int a = wxAqiFromPm25(g_weather.pm25);
        if (a >= 0) { g_weather.aqiNow = a; g_weather.hasAqiNow = true; }
      }
      if (c["us_aqi"].is<float>() || c["us_aqi"].is<int>()) { g_weather.aqi = c["us_aqi"]; g_weather.hasAqi = true; gotAqVals = true; }
    }
    if (!gotAqVals) {
      g_weather.aqError = true;
      g_wxCurAq = -105;      // reached, parsed, carried nothing -- see forecast
    } else {
    g_weather.aqError = false;
    g_wxAqOkMs = millis(); if (!g_wxAqOkMs) g_wxAqOkMs = 1;
    g_wxCycGotAny = true;
    g_wxCurAq = 200;
    }
  } else {
    g_weather.aqError = true;
    g_wxCurAq = g_wxHttpCode ? g_wxHttpCode : -1;
  }
}

}

static void wxStepAqForecast(const Settings& s) {
  if (!g_wxCycWantAq) return;
  char url[320];
  // --- air-quality forecast: per-day AQI for the 3-day page ---
  // The air-quality API has no "daily" block at all -- asking for one is a hard
  // error ("Cannot initialize ForecastVariableDaily from invalid String value
  // us_aqi_max"), unlike the weather API which hands us daily maxima ready-made.
  // So AQI per day means pulling the hourly series and reducing it here.
  //
  // timezone=auto makes the series start at LOCAL midnight, so hour i belongs to
  // day i/24 with no date parsing -- which is why "time" is deliberately NOT in
  // the filter below: dropping it avoids allocating 96 timestamp strings on a
  // board with ~27 KB of free heap. forecast_days=4 yields exactly 96 hours, and
  // fc[n] is day offset n+1 (index 0 is today), so fc[n] reduces the hours
  // [(n+1)*24, (n+1)*24+24).
  //
  // Gated on fcFresh, NOT on hasForecast/fcCount: those persist from earlier
  // cycles (a failed forecast only sets forecastError; the retire below needs
  // period*3 to clear them). Decorating a stale fc[] with fresh AQI silently
  // misaligns the days -- after local midnight fc[0] is physically today but
  // would receive tomorrow's AQI -- so only a fc[] rebuilt this cycle is
  // eligible. Same reason a daemon-pushed fc[] is left alone once pushes stop.
  //
  // Skipped too when the current-AQ GET just failed at the transport level
  // (negative HTTPC_ERROR_*): it is the same host, so a third blocking attempt
  // only adds ~9 s to the loop() stall for a connection we already know is down.
  if (!g_wxCycFcFresh) {
    g_wxCurAqF = 0;              // no attempt this cycle; don't leave a stale 200
  } else if (g_wxCurAq < 0) {
    g_wxCurAqF = -103;           // skipped: same host unreachable this cycle
  } else if (g_weather.fcCount == 0) {
    g_wxCurAqF = -102;           // skipped: the forecast fetch built no days,
                                  // so there is nothing to decorate
  } else if ([]{ time_t now = time(nullptr); struct tm* lt = localtime(&now);
                 return !lt || lt->tm_yday != g_wxCycFcYday; }()) {
    // Local midnight arrived between the forecast step and this one. fc[] holds
    // yesterday's horizon; the hourly series starts at the NEW local midnight,
    // so decorating now shifts every AQI onto the wrong day. Skip -- the next
    // cycle rebuilds both consistently.
    g_wxCurAqF = -106;
  } else {
    snprintf(url, sizeof(url),
             "http://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
             "&hourly=pm2_5&forecast_days=4&timezone=auto",
             s.calendar.lat, s.calendar.lon);
    JsonDocument filter;
    filter["hourly"]["pm2_5"] = true;
    JsonDocument doc;
    if (wxGetJson(s, url, doc, filter)) {
      JsonArray v = doc["hourly"]["pm2_5"];
      const int sz = (int)v.size();   // O(n) in ArduinoJson v7 -- hoist it
      int filled = 0;
      for (uint8_t n = 0; n < g_weather.fcCount && n < WX_FC_DAYS; n++) {
        const int base = (int)(n + 1) * 24;
        int mx = -1;
        for (int i = base; i < base + 24 && i < sz; i++) {
          // Open-Meteo returns null for hours it has no model output for. A day
          // that is entirely null must stay "no data", not read as 0 -- which
          // the renderer would colour as pristine air.
          // isNull() alone misses a string or bool, which as<float>() turns
          // into 0.0 -- a fabricated AQI 0, rendered as pristine air.
          if (!(v[i].is<float>() || v[i].is<int>())) continue;
          // as<float>(), not "| 0": pm2_5 is fractional and operator| only
          // matches integer-typed values, so 60.5 would silently read as 0.
          int a = wxAqiFromPm25(v[i].as<float>());
          if (a > mx) mx = a;
        }
        if (mx >= 0) { g_weather.fc[n].aqi = mx; g_weather.fc[n].hasAqi = true; filled++; }
      }
      g_wxCurAqF = filled ? 200 : -101;  // -101 = parsed fine, but every hour was null
      g_wxCycGotAny = true;
    } else {
      g_wxCurAqF = g_wxHttpCode ? g_wxHttpCode : -1;
    }
  }

}

static void wxStepFinish(const Settings& s, uint32_t period) {
  // PUBLISH this cycle's endpoint codes as one atomic snapshot. linkState()
  // only ever sees a complete, self-consistent verdict: three codes that all
  // came from the same cycle, stamped with the age of that cycle. A cycle in
  // progress never moves the published verdict, in either direction.
  g_wxLastFc  = g_wxCurFc;
  g_wxLastAq  = g_wxCurAq;
  g_wxLastAqF = g_wxCurAqF;
  g_wxTriedMs = millis(); if (!g_wxTriedMs) g_wxTriedMs = 1;   // 0 = "never attempted"

  // Retire a half that has been failing long enough that showing it would be a
  // lie. Without this, one dead endpoint leaves its last values on screen
  // indefinitely with nothing marking them stale -- and after midnight the old
  // forecast even carries the wrong weekday labels. weatherApply() already
  // clears the forecast on a bad push; this mirrors that policy for the fetch.
  // Deliberately per-half: a working forecast must not be dropped because air
  // quality is down, which is the whole reason these are two endpoints.
  const uint32_t staleMs = period * 3;
  // Retire only when EVERY source that could have supplied this half has been
  // quiet for staleMs. Both clocks are consulted symmetrically -- see
  // wxAllSourcesStale() for why requiring the fetch clock specifically was a
  // blind spot on a daemon-fed unit, and why the AQ half needed the push clock
  // it did not have.
  bool retired = false;
  if (g_weather.forecastError && wxAllSourcesStale(g_wxFcOkMs, g_wxFcPushOkMs, staleMs)) {
    if (g_weather.hasForecast || g_weather.hasTemp || g_weather.hasPrecip ||
        g_weather.hasWeatherCode || g_weather.hasUvIndex) retired = true;
    g_weather.fcCount = 0; g_weather.hasForecast = false;
    g_weather.hasTemp = g_weather.hasPrecip = false;
    g_weather.hasWeatherCode = g_weather.hasUvIndex = false;
  }
  if (g_weather.aqError && wxAllSourcesStale(g_wxAqOkMs, g_wxAqPushOkMs, staleMs)) {
    if (g_weather.hasPm25 || g_weather.hasAqi || g_weather.hasAqiNow) retired = true;
    g_weather.hasPm25 = g_weather.hasAqi = g_weather.hasAqiNow = false;
  }

  if (g_wxCycGotAny) { g_weather.valid = true; g_weather.lastOkMs = millis(); }
  // Retirement CHANGES what should be on screen without being a success, so it
  // must move the render signal too. Both weather modes key their repaint off
  // renderGen; keyed off lastOkMs (which only a success moves), a cycle that
  // retired both halves left the old readings painted on screen indefinitely --
  // and with reachable failures (HTTP 500, parse errors) linkState() stays
  // LINK_OK, so no offline notice covers them either.
  if (g_wxCycGotAny || retired) g_weather.renderGen++;

  // Back off on a cycle that achieved nothing. A device with no internet
  // otherwise pays the full stall every period forever; capped so it always
  // recovers on its own once the link returns.
  if (g_wxCycGotAny) {
    g_wxFailStreak = 0;
    g_wxIntervalMs = period;
  } else {
    if (g_wxFailStreak < 5) g_wxFailStreak++;
    uint32_t iv = period << g_wxFailStreak;
    if (iv > WX_BACKOFF_MAX_MS || iv < period) iv = WX_BACKOFF_MAX_MS;   // <period = overflow
    // The cap must never turn backoff into a SPEED-UP. weatherPollSec is a
    // uint16_t, so a legal 65535 gives period = 65,535,000 ms (18.2 h) -- every
    // backed-off interval then exceeded the 30-minute ceiling and got clamped
    // DOWN to it, making a failing 18-hour poll retry every half hour.
    if (iv < period) iv = period;
    g_wxIntervalMs = iv;
  }
  // Schedule from COMPLETION, not from the start of the cycle. Stamped up
  // front, a cycle that itself lasted >= period left the next one already due,
  // so the web server got one handleClient() pass between multi-second stalls.
  g_wxNextMs = millis() + g_wxIntervalMs;
}

void weatherCadenceChanged() {
  // Data stays -- only the timing is wrong. Drop the backoff INTERVAL, since it
  // was derived from the previous period and would otherwise keep the old,
  // longer gap in force (and with it weatherAttemptWindowMs()).
  //
  // Deliberately NOT g_wxFailStreak. The streak is evidence about the LINK, and
  // editing a cadence field says nothing about whether the internet works; the
  // parent commit cleared it here, which let someone saving that field mid-outage
  // un-stick a correct DEVICE OFFLINE verdict for two more cycles. The immediate
  // re-probe below closes the window this leaves: a fresh attempt runs on the very
  // next loop() pass and republishes a current verdict within seconds.
  g_wxIntervalMs = 60000;
  g_wxNextMs = millis();
}

void weatherLocationChanged() {
  // The city came from a push describing the OLD coordinates; leaving it up
  // would caption new readings with the previous location's name.
  g_weather.city[0] = 0; g_weather.hasCity = false;
  // Everything else is location-bound too. Drop the values rather than let the
  // retire timer surface them next to a new place name.
  g_weather.hasTemp = g_weather.hasPrecip = false;
  g_weather.hasWeatherCode = g_weather.hasUvIndex = false;
  g_weather.hasPm25 = g_weather.hasAqi = g_weather.hasAqiNow = false;
  g_weather.fcCount = 0; g_weather.hasForecast = false;
  // This function MUTATES g_weather, so it owes the render signal -- the same
  // contract the retirement fix established. Without it, a location change made
  // while the device is offline clears every has* flag, wxStepFinish() then
  // computes retired == false (the flags are already down) and g_wxCycGotAny is
  // false, so renderGen never moves and the paint cache keeps showing the OLD
  // location's readings and caption indefinitely. Currently masked because the
  // only caller is handlePostConfig(), which forces a full repaint anyway; this
  // makes it true by construction rather than by luck.
  g_weather.renderGen++;
  g_wxFcOkMs = g_wxAqOkMs = 0;
  // The push clocks describe the OLD coordinates. Left set, both halves look
  // daemon-fed for up to WX_PUSH_GRACE_MS and the "fetch the new location now"
  // contract silently becomes "in three minutes", with blank pages meanwhile.
  g_wxFcPushOkMs = g_wxAqPushOkMs = g_wxPushOkMs = 0;
  g_wxFailStreak = 0;
  g_wxIntervalMs = 60000;
  g_wxStep   = WX_IDLE;      // abandon any half-finished cycle for the old spot
  g_wxNextMs = millis();     // fetch the new location now, not a period from now
}

void weatherService(const Settings& s) {
  if (s.calendar.lat == 0.0f && s.calendar.lon == 0.0f) return;   // location not set
  if (!netConnected()) {
    // Abandon any cycle in flight rather than pausing it. g_wxCycWantFc/WantAq
    // were latched when it opened; across an outage longer than
    // WX_PUSH_GRACE_MS those latches go stale, and a resumed cycle would skip a
    // half that is now the device's job -- leaving it unfetched until the next
    // deadline, up to 18.2 h at the maximum poll period.
    // One exception: a cycle already at WX_STEP_DONE has run all three GETs and
    // has mutated g_weather. Dropping it here would leave the new readings in
    // memory and the OLD pixels on screen (renderGen never moves) until some
    // later success or a wake(), and would skip the retire/backoff bookkeeping.
    // Finish it, then go idle.
    if (g_wxStep == WX_STEP_DONE) {
      uint32_t period = (uint32_t)s.calendar.weatherPollSec * 1000UL;
      if (period < 60000UL) period = 60000UL;
      wxStepFinish(s, period);
    }
    g_wxStep = WX_IDLE;
    g_wxCycFcFresh = false;
    return;
  }

  uint32_t period = (uint32_t)s.calendar.weatherPollSec * 1000UL;
  // Clamp here rather than trusting the settings loader: weatherPollSec is a
  // uint16_t and Settings.cpp only applies a LOWER bound, so a stored 65536
  // truncates to 0 -- which without this would fetch twice per loop() forever
  // and starve the web server, killing the only recovery path this unit has.
  if (period < 60000UL) period = 60000UL;

  // Mid-cycle: do exactly ONE endpoint, then hand loop() back so netLoop() and
  // webPortalLoop() run between endpoints.
  if (g_wxStep != WX_IDLE) {
    // Advance BEFORE doing the work, never after. weatherNotePush() aborts an
    // in-flight cycle by setting WX_IDLE (see A3); if the step were assigned
    // after the call, a push arriving during the GET would be overwritten by
    // the very next statement and the cycle it aborted would resume -- exactly
    // the case the abort exists for. Assigning first makes whoever writes
    // WX_IDLE last the winner, with no reliance on core re-entrancy behaviour.
    switch (g_wxStep) {
      case WX_STEP_FC:  g_wxStep = WX_STEP_AQ;   wxStepForecast(s);   return;
      case WX_STEP_AQ:  g_wxStep = WX_STEP_AQF;  wxStepAq(s);         return;
      case WX_STEP_AQF: g_wxStep = WX_STEP_DONE; wxStepAqForecast(s); return;
      default: break;
    }
    wxStepFinish(s, period);
    g_wxStep = WX_IDLE;
    return;
  }

  // Idle: decide whether to open a cycle. Suppression is now PER HALF -- one
  // shared clock meant a daemon pushing only a city name, or only weather with
  // no AQI, suppressed all three fetches and starved the half it never fed.
  bool fcPushed = g_wxFcPushOkMs && (millis() - g_wxFcPushOkMs) < WX_PUSH_GRACE_MS;
  bool aqPushed = g_wxAqPushOkMs && (millis() - g_wxAqPushOkMs) < WX_PUSH_GRACE_MS;
  if (fcPushed && aqPushed) {
    // Fully daemon-fed. Keep the deadline rolling so the first fallback fetch
    // lands promptly when pushes stop, instead of a whole period later (and so
    // the compare cannot go negative after 24.8 days of uptime).
    g_wxNextMs = millis();
    return;
  }
  if ((int32_t)(millis() - g_wxNextMs) < 0) return;
  // Plain HTTP needs far less than the TLS paths (Radar 18k, Usage 20k), but
  // bail anyway so a fetch never starts while an OTA upload holds the heap.
  if (ESP.getFreeHeap() < 12000) {
    // Record it. Left silent, fc stayed 0 forever and linkState() read that as
    // "never attempted" -> LINK_OK, so a device wedged here looked healthy.
    // -8 is HTTPClient's too-little-RAM code: local, so it is correctly NOT
    // evidence of an internet outage.
    // Publish DIRECTLY, not via g_wxCur*. This path returns without ever
    // reaching wxStepFinish(), so an in-flight code would never be published
    // and /api/status would report 0 -- "never attempted" -- which is the exact
    // silent wedge this diagnostic was added to expose. -8 is a terminal
    // verdict about this attempt, not a cycle result.
    g_wxLastFc = -8;
    g_wxTriedMs = millis(); if (!g_wxTriedMs) g_wxTriedMs = 1;
    g_wxNextMs = millis() + 5000;
    return;
  }
  g_wxCycWantFc  = !fcPushed;
  g_wxCycWantAq  = !aqPushed;
  g_wxCycFcFresh = false;
  g_wxCycFcYday  = -1;
  g_wxCycGotAny  = false;
  // Open the new cycle on a blank IN-FLIGHT slate, and leave the PUBLISHED
  // verdict from the last completed cycle standing until this one finishes.
  // That keeps both properties we need at once: a cycle can never pair its own
  // forecast failure with the PREVIOUS cycle's AQ failure (the codes it reads
  // are its own), and an established DEVICE OFFLINE verdict survives every
  // retry of a continuous outage instead of flapping to LINK_OK at each open.
  // g_wxTriedMs is likewise stamped at FINISH, so the age linkState() checks
  // always describes the evidence it is actually looking at.
  g_wxCurFc = g_wxCurAq = g_wxCurAqF = 0;
  g_wxStep       = WX_STEP_FC;
}
#else
void weatherService(const Settings&) {}
#endif  // WITH_DEVICE_WEATHER

#if WITH_DEVICE_WEATHER
// Called by weatherApply() on every accepted push. Kept separate from
// g_weather.lastOkMs because that field is also written by our own fetch.
void weatherNotePush(bool gotFc, bool gotAq) {
  uint32_t now = millis(); if (!now) now = 1;   // 0 is the "never" sentinel
  // A push WINS -- including over a cycle already in flight. Since the fetch
  // became one step per loop() pass, webPortalLoop() runs between steps, so a
  // push can land mid-cycle. Left running, a later step would overwrite the
  // half just pushed; worse, a push can replace fc[] while g_wxCycFcFresh is
  // still true from an earlier pass, and wxStepAqForecast() would then decorate
  // SOMEONE ELSE'S forecast as if this cycle had built it -- across a date or
  // location boundary that attaches AQI to the wrong day rows.
  //
  // Abort ONLY when the push actually covers a half this cycle set out to
  // fetch. Aborting on every push starves the other half: a daemon feeding just
  // air quality every 30 s would cancel the forecast cycle again and again, and
  // an aborted cycle never reaches wxStepFinish(), so backoff never engages and
  // the retire timer never runs.
  if (g_wxStep != WX_IDLE &&
      ((gotFc && g_wxCycWantFc) || (gotAq && g_wxCycWantAq))) {
    g_wxStep = WX_IDLE;
    g_wxCycFcFresh = false;
    g_wxCycGotAny  = false;
  }
  g_wxPushOkMs = now;
  // Only stamp the half this payload actually carried. A push that brought no
  // air quality must NOT vouch for the freshness of the AQ already on screen.
  if (gotFc) g_wxFcPushOkMs = now;
  if (gotAq) g_wxAqPushOkMs = now;
  // A push proves the daemon reached us, which retires the fetch diagnostic as
  // evidence about the link. Without this, linkState() could keep reading a
  // failure recorded BEFORE the push -- and because weatherService() then
  // suppresses a fresh probe for 2 * weatherPollSec, that stale verdict could
  // stand for 20 minutes (131 at UINT16_MAX) and flap the screen to DEVICE
  // OFFLINE on a device demonstrably online. 0 is the "never attempted" value
  // linkState() already treats as unknown, which is exactly the truth here.
  g_wxLastFc = g_wxLastAq = g_wxLastAqF = 0;
  g_wxTriedMs = 0;
  // A push is proof the link works, so it must also clear the BACKOFF STATE, not
  // just the codes. Otherwise: a night-long outage drives the streak to 5 and the
  // interval to 30 min; in the morning the laptop wakes and its pushes suppress
  // our own probes, so no successful cycle ever runs to reset the streak. The
  // laptop then sleeps, one transient failure follows, and streak 5 >= 2 declares
  // DEVICE OFFLINE instantly -- for 30 minutes, on a unit that was provably online
  // four minutes earlier. That is a longer version of the exact false verdict this
  // commit set out to remove.
  g_wxFailStreak = 0;
  g_wxIntervalMs = 60000;
}
void weatherFetchDiag(int& fc, int& aq, int& aqf, uint32_t& agoMs) {
  fc = g_wxLastFc; aq = g_wxLastAq; aqf = g_wxLastAqF;
  agoMs = g_wxTriedMs ? (millis() - g_wxTriedMs) : 0;
}
#endif
