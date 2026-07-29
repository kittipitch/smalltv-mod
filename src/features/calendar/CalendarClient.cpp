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
  ev["summary"] = true; ev["start"] = true; ev["allDay"] = true; ev["color"] = true;
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
  f["weatherCode"] = true; f["pm25"] = true; f["aqi"] = true; f["city"] = true;
}

bool weatherApply(const String& body) {
  JsonDocument filter; weatherFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool gotTemp = doc["tempC"].is<float>() || doc["tempC"].is<int>();
  bool gotPrecip = doc["precipPct"].is<int>();
  bool gotCode = doc["weatherCode"].is<int>();
  bool gotPm = doc["pm25"].is<float>() || doc["pm25"].is<int>();
  bool gotAqi = doc["aqi"].is<int>();
  bool gotCity = doc["city"].is<const char*>();
  if (!gotTemp && !gotPrecip && !gotCode && !gotPm && !gotAqi && !gotCity) return false;

  if (gotTemp)   { g_weather.tempC = doc["tempC"].as<float>(); g_weather.hasTemp = true; }
  if (gotPrecip) { g_weather.precipPct = doc["precipPct"].as<int>(); g_weather.hasPrecip = true; }
  if (gotCode)   { g_weather.weatherCode = doc["weatherCode"].as<int>(); g_weather.hasWeatherCode = true; }
  if (gotPm)     { g_weather.pm25 = doc["pm25"].as<float>(); g_weather.hasPm25 = true; }
  if (gotAqi)    { g_weather.aqi = doc["aqi"].as<int>(); g_weather.hasAqi = true; }
  if (gotCity)   { stripNonAscii(doc["city"].as<const char*>(), g_weather.city, sizeof(g_weather.city)); g_weather.hasCity = true; }
  g_weather.forecastError = !(gotTemp || gotPrecip || gotCode);
  g_weather.aqError = !(gotPm || gotAqi);
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
  f["pctResetCreditUsed"] = true;
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
  if (doc["pctResetCreditUsed"].is<int>()) {
    g_codex.pctResetCreditUsed = doc["pctResetCreditUsed"].as<int>();
    g_codex.hasPctResetCreditUsed = true;
  }
  g_codex.valid = true;
  g_codex.lastOkMs = millis();
  return true;
}

// ---- Antigravity quota: pushed payload -------------------------------------
// { "ok":true, "pctModel":2, "rModel":291 }
// Antigravity CLI (`agy`) quota -- unlike Codex, each daemon poll fires a
// real, costed prompt (see clawdmeter_daemon.py's poll_antigravity()), so
// this is a single-card shape, not two windows.
static void antigravityFilter(JsonDocument& f) {
  f["ok"] = true; f["pctModel"] = true; f["label"] = true; f["rModel"] = true;
}

bool antigravityApply(const String& body) {
  JsonDocument filter; antigravityFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool gotPct = doc["pctModel"].is<int>();
  if (!gotPct) return false;

  g_antigravity.pctModel = doc["pctModel"].as<int>(); g_antigravity.hasPctModel = true;
  if (doc["label"].is<const char*>()) {
    strlcpy(g_antigravity.label, doc["label"].as<const char*>(), sizeof(g_antigravity.label));
    g_antigravity.hasLabel = true;
  }
  if (doc["rModel"].is<int>()) { g_antigravity.rModel = doc["rModel"].as<int>(); g_antigravity.hasRModel = true; }
  g_antigravity.valid = true;
  g_antigravity.lastOkMs = millis();
  return true;
}
