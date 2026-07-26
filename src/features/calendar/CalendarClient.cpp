#include "CalendarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>

static CalendarEvent g_cal;
static WeatherData   g_weather;
static bool          g_inited = false;

void calendarInit(const Settings& s) {
  (void)s;
  g_cal.clear();
  g_weather.clear();
  g_inited = true;
}

const CalendarEvent& calendarGet() { return g_cal; }
const WeatherData&   weatherGet()  { return g_weather; }

// ---- calendar: pushed payload ----------------------------------------------
// { "ok":true, "summary":"Team sync", "start":"2026-07-27T10:00:00+01:00", "allDay":false }
// summary/start are JSON null (not just absent) when there's no upcoming event.
static void calendarFilter(JsonDocument& f) {
  f["ok"] = true; f["summary"] = true; f["start"] = true; f["allDay"] = true;
}

bool calendarApply(const String& body) {
  if (!g_inited) g_cal.clear();
  JsonDocument filter; calendarFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;

  bool hasSummary = doc["summary"].is<const char*>();
  strlcpy(g_cal.summary, hasSummary ? doc["summary"].as<const char*>() : "", sizeof(g_cal.summary));
  bool hasStart = doc["start"].is<const char*>();
  strlcpy(g_cal.start, hasStart ? doc["start"].as<const char*>() : "", sizeof(g_cal.start));
  g_cal.allDay = doc["allDay"] | false;
  g_cal.hasEvent = hasSummary && hasStart;
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
  f["weatherCode"] = true; f["pm25"] = true; f["aqi"] = true;
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
  if (!gotTemp && !gotPrecip && !gotCode && !gotPm && !gotAqi) return false;

  if (gotTemp)   { g_weather.tempC = doc["tempC"].as<float>(); g_weather.hasTemp = true; }
  if (gotPrecip) { g_weather.precipPct = doc["precipPct"].as<int>(); g_weather.hasPrecip = true; }
  if (gotCode)   { g_weather.weatherCode = doc["weatherCode"].as<int>(); g_weather.hasWeatherCode = true; }
  if (gotPm)     { g_weather.pm25 = doc["pm25"].as<float>(); g_weather.hasPm25 = true; }
  if (gotAqi)    { g_weather.aqi = doc["aqi"].as<int>(); g_weather.hasAqi = true; }
  g_weather.forecastError = !(gotTemp || gotPrecip || gotCode);
  g_weather.aqError = !(gotPm || gotAqi);
  g_weather.valid = true;
  g_weather.lastOkMs = millis();
  return true;
}
