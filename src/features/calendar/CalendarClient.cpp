#include "CalendarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>

static CalendarEvent g_cal;
static WeatherData   g_weather;
static ZaiData        g_zai;
static CodexData      g_codex;
static AntigravityData g_antigravity;
static bool          g_inited = false;

void calendarInit(const Settings& s) {
  (void)s;
  g_cal.clear();
  g_weather.clear();
  g_zai.clear();
  g_codex.clear();
  g_antigravity.clear();
  g_inited = true;
}

const CalendarEvent& calendarGet() { return g_cal; }
const WeatherData&   weatherGet()  { return g_weather; }
const ZaiData&        zaiGet()      { return g_zai; }
const CodexData&      codexGet()    { return g_codex; }
const AntigravityData& antigravityGet() { return g_antigravity; }

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
    stripNonAscii(ev["summary"].as<const char*>(), g_cal.items[n].summary, sizeof(g_cal.items[n].summary));
    strlcpy(g_cal.items[n].start, ev["start"].as<const char*>(), sizeof(g_cal.items[n].start));
    // "end" mirrors "start" and is optional (old daemons / single-instant
    // events send none). Stored raw here -- the all-day EXCLUSIVE-end
    // minus-one-day fixup is deliberately NOT applied at parse time; it's a
    // display concern and lives in CalendarMode.cpp, so this buffer always
    // reflects exactly what the daemon sent (easier to reason about than a
    // silently-corrected value stored under the name "end").
    if (ev["end"].is<const char*>()) {
      strlcpy(g_cal.items[n].end, ev["end"].as<const char*>(), sizeof(g_cal.items[n].end));
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
  if (gotCity)   { stripNonAscii(doc["city"].as<const char*>(), g_weather.city, sizeof(g_weather.city)); g_weather.hasCity = true; }

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
      strlcpy(g_weather.fc[n].day, day["day"].as<const char*>(), sizeof(g_weather.fc[n].day));
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
  } else {
    g_weather.fcCount = 0;
    g_weather.hasForecast = false;
  }

  g_weather.forecastError = !(gotTemp || gotPrecip || gotCode || gotUv);
  g_weather.aqError = !(gotPm || gotAqi || gotAqiNow);
  g_weather.valid = true;
  g_weather.lastOkMs = millis();
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
    strlcpy(g_antigravity.labelPro, doc["labelPro"].as<const char*>(), sizeof(g_antigravity.labelPro));
    g_antigravity.hasLabelPro = true;
  }
  if (doc["rPro"].is<int>()) { g_antigravity.rPro = doc["rPro"].as<int>(); g_antigravity.hasRPro = true; }

  if (doc["pctFlash"].is<int>()) { g_antigravity.pctFlash = doc["pctFlash"].as<int>(); g_antigravity.hasPctFlash = true; }
  if (doc["labelFlash"].is<const char*>()) {
    strlcpy(g_antigravity.labelFlash, doc["labelFlash"].as<const char*>(), sizeof(g_antigravity.labelFlash));
    g_antigravity.hasLabelFlash = true;
  }
  if (doc["rFlash"].is<int>()) { g_antigravity.rFlash = doc["rFlash"].as<int>(); g_antigravity.hasRFlash = true; }

  g_antigravity.valid = true;
  g_antigravity.lastOkMs = millis();
  return true;
}
