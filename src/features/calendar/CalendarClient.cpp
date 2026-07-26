#include "CalendarClient.h"
#include "Platform.h"
#include <ArduinoJson.h>

static CalendarEvent g_cal;
static WeatherData   g_weather;
static uint32_t      g_nextWeatherPollMs = 0;
static bool           g_inited = false;
static bool           g_fetchAqNext = false;   // alternates forecast/AQ across ticks

// TLS receive-buffer size per host, probed once (see RadarClient's identical
// approach for adsb.fi) — MFLN lets BearSSL use a much smaller buffer than the
// default 4 KB when the server supports it, a real heap win on the ESP8266.
static uint16_t g_tlsRxForecast = 0;
static uint16_t g_tlsRxAq       = 0;

void calendarInit(const Settings& s) {
  (void)s;
  g_cal.clear();
  g_weather.clear();
  g_nextWeatherPollMs = millis();
  g_inited = true;
}

void calendarForceRefresh() { g_nextWeatherPollMs = millis(); }

const CalendarEvent& calendarGet() { return g_cal; }
const WeatherData&   weatherGet()  { return g_weather; }

// ---- calendar: pushed payload ----------------------------------------------
// { "ok":true, "summary":"Team sync", "start":"2026-07-27T10:00:00+01:00", "allDay":false }
// summary/start are JSON null (not just absent) when there's no upcoming event.
static void calendarFilter(JsonDocument& f) {
  f["ok"] = true; f["summary"] = true; f["start"] = true; f["allDay"] = true;
}

bool calendarApply(const String& body) {
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

// ---- weather + air quality: direct fetch -----------------------------------
static void probeTlsFor(const char* host, uint16_t& cache) {
#if defined(SMALLTV_ESP8266)
  if (cache) return;
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 512))       cache = 512;
  else if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(host, 443, 1024)) cache = 1024;
  else                                                                         cache = 4096;
#else
  (void)host; (void)cache;
#endif
}

static bool fetchJson(const char* host, const String& url, uint16_t& tlsCache, JsonDocument& doc,
                      JsonDocument& filter) {
  if (ESP.getFreeHeap() < 18000) return false;   // TLS needs a big contiguous chunk; skip, don't crash
  probeTlsFor(host, tlsCache);

  std::unique_ptr<NetClient> client(platformMakeSecureClient(tlsCache));   // public read-only API, no cert pinning
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");
  http.setUserAgent(F(OPEN_METEO_USER_AGENT));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  bool ok = !deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  return ok;
}

static String buildForecastUrl(const Settings& s) {
  String u = F("https://");
  u += F(OPEN_METEO_FORECAST_HOST);
  u += F(OPEN_METEO_FORECAST_PATH);
  u += F("?latitude=");
  u += String(s.calendar.lat, 4);
  u += F("&longitude=");
  u += String(s.calendar.lon, 4);
  u += F("&current=temperature_2m,precipitation_probability,weather_code");
  return u;
}

static String buildAqUrl(const Settings& s) {
  String u = F("https://");
  u += F(OPEN_METEO_AQ_HOST);
  u += F(OPEN_METEO_AQ_PATH);
  u += F("?latitude=");
  u += String(s.calendar.lat, 4);
  u += F("&longitude=");
  u += String(s.calendar.lon, 4);
  u += F("&current=pm2_5,us_aqi");
  return u;
}

static void fetchForecast(const Settings& s) {
  JsonDocument filter;
  JsonObject cur = filter["current"].to<JsonObject>();
  cur["temperature_2m"] = true;
  cur["precipitation_probability"] = true;
  cur["weather_code"] = true;

  JsonDocument doc;
  if (!fetchJson(OPEN_METEO_FORECAST_HOST, buildForecastUrl(s), g_tlsRxForecast, doc, filter)) {
    g_weather.forecastError = true;
    return;
  }
  JsonObjectConst cu = doc["current"].as<JsonObjectConst>();
  bool gotTemp = cu["temperature_2m"].is<float>() || cu["temperature_2m"].is<int>();
  bool gotPrecip = cu["precipitation_probability"].is<int>();
  bool gotCode = cu["weather_code"].is<int>();
  if (!gotTemp && !gotPrecip && !gotCode) { g_weather.forecastError = true; return; }

  if (gotTemp)   { g_weather.tempC = cu["temperature_2m"].as<float>(); g_weather.hasTemp = true; }
  if (gotPrecip) { g_weather.precipPct = cu["precipitation_probability"].as<int>(); g_weather.hasPrecip = true; }
  if (gotCode)   { g_weather.weatherCode = cu["weather_code"].as<int>(); g_weather.hasWeatherCode = true; }
  g_weather.forecastError = false;
  g_weather.valid = true;
  g_weather.lastOkMs = millis();
}

static void fetchAirQuality(const Settings& s) {
  JsonDocument filter;
  JsonObject cur = filter["current"].to<JsonObject>();
  cur["pm2_5"] = true;
  cur["us_aqi"] = true;

  JsonDocument doc;
  if (!fetchJson(OPEN_METEO_AQ_HOST, buildAqUrl(s), g_tlsRxAq, doc, filter)) {
    g_weather.aqError = true;
    return;
  }
  JsonObjectConst cu = doc["current"].as<JsonObjectConst>();
  bool gotPm = cu["pm2_5"].is<float>() || cu["pm2_5"].is<int>();
  bool gotAqi = cu["us_aqi"].is<int>();
  if (!gotPm && !gotAqi) { g_weather.aqError = true; return; }

  if (gotPm)  { g_weather.pm25 = cu["pm2_5"].as<float>(); g_weather.hasPm25 = true; }
  if (gotAqi) { g_weather.aqi = cu["us_aqi"].as<int>(); g_weather.hasAqi = true; }
  g_weather.aqError = false;
  g_weather.valid = true;
  g_weather.lastOkMs = millis();
}

// ---------------------------------------------------------------------------
void calendarService(const Settings& s) {
  if (!g_inited) calendarInit(s);

  // No location set -> nothing to fetch (the mode shows a prompt instead),
  // same convention as radar's lat/lon==0 sentinel.
  if (s.calendar.lat == 0.0f && s.calendar.lon == 0.0f) return;
  if ((int32_t)(millis() - g_nextWeatherPollMs) < 0) return;
  // Alternate forecast/AQ across ticks (each its own poll period) rather than
  // both in one tick — two back-to-back TLS handshakes on the ESP8266 is the
  // exact heap-pressure pattern this codebase avoids elsewhere (StockClient).
  g_nextWeatherPollMs = millis() + (uint32_t)s.calendar.weatherPollSec * 1000UL / 2;
  if (g_fetchAqNext) fetchAirQuality(s); else fetchForecast(s);
  g_fetchAqNext = !g_fetchAqNext;
}
