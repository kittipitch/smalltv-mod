// Settings.h — persisted configuration (LittleFS /config.json)
//
// Layout is segmented per feature: shared device/network fields live at the top
// level, and each feature owns a nested settings slice (ticker / usage / radar).
// config.json mirrors this: { ..shared.., "ticker":{...}, "usage":{...} }.
// The JSON reader also still accepts the old flat layout, so a device upgrading
// from the pre-segmentation firmware keeps its WiFi + symbols; the next save
// rewrites it nested.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

struct SymbolCfg {
  char    symbol[MAX_SYMBOL_LEN];
  char    name[MAX_NAME_LEN];
  uint8_t source;     // SRC_* per ticker (see config.h)
  float   qty;        // position size; 0 = not a position
  float   cost;       // cost basis per unit, in the instrument's currency
};

// A home-area airport marker (radar feature), configured in the web UI.
struct Airport {
  char  icao[MAX_ICAO_LEN];
  float lat, lon;
};

// One saved WiFi station network. The device keeps up to MAX_WIFI_NETS and
// joins the strongest visible one at boot (hidden SSIDs are tried last).
struct WifiCred {
  String ssid;
  String pass;
};

// ---- Ticker (stock/crypto) feature slice ----------------------------------
// The data source is per symbol (SymbolCfg.source); webhookUrl is shared by
// every symbol whose source is SRC_WEBHOOK.
struct TickerSettings {
  String   webhookUrl;    // custom webhook base URL (used by webhook symbols)
  String   range;         // chart timeframe token (e.g. "1d", "5d", "1mo", "1y")
  uint16_t points;        // sparkline points requested
  uint16_t pollSec;       // refresh period
  uint16_t rotateSec;     // per-symbol on-screen time
  bool     colorInverted; // false: up=green/down=red ; true: swapped
  bool     changeOnRange; // true: change/% over the chart timeframe; false: provider's 1-day change

  // What to show
  bool showName;
  bool showPrice;
  bool showChange;
  bool showChart;
  bool showRangeLabel;
  bool showUpdatedAgo;
  bool showPageDots;
  bool showPortfolio;   // P/L line on position tickers + portfolio summary page

  SymbolCfg symbols[MAX_SYMBOLS];
  uint8_t   symbolCount;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);   // applies only the keys present
};

// ---- Claude usage feature slice -------------------------------------------
struct UsageSettings {
  String   usageUrl;      // daemon HTTP endpoint, e.g. http://192.168.1.10:8787/
  uint16_t pollSec;       // refresh period
  bool     barGrowRight;  // false (default): bar fills left->right; true: right->left

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Clock / night mode slice (device-wide) --------------------------------
struct ClockSettings {
  String   tz;            // IANA display name, e.g. "Europe/Rome" (UI round-trip)
  String   tzPosix;       // POSIX TZ rule the device feeds SNTP
  bool     nightEnabled;  // dim/blank on a nightly schedule
  uint16_t nightStartMin; // minutes since local midnight (0..1439)
  uint16_t nightEndMin;
  uint8_t  nightLevel;    // 0..100, 0 = backlight off

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Calendar + weather feature slice --------------------------------------
// Calendar event data itself arrives via push (POST /api/calendar from
// clawdmeter-daemon) — no URL/poll setting needed for it here, same as usage's
// push path. lat/lon + pollSec are for the device-direct weather+AQI fetch.
struct CalendarSettings {
  float    lat;            // weather/AQ location (0,0 = not set yet)
  float    lon;
  uint16_t weatherPollSec; // device-direct Open-Meteo refresh period

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Plane radar feature slice --------------------------------------------
struct RadarSettings {
  float    lat;           // home latitude  (0,0 = not set yet)
  float    lon;           // home longitude
  uint8_t  source;        // RADAR_SRC_DIRECT or RADAR_SRC_WEBHOOK
  String   webhookUrl;    // LAN proxy base URL (when source=webhook)
  uint16_t rangeKm;       // outer ring radius
  uint16_t pollSec;       // refresh period
  bool     unitsMi;       // show distances in miles instead of km

  bool     showLabels;    // callsign + altitude next to each aircraft
  bool     showVectors;   // speed/heading vector line
  bool     showRimDots;   // aircraft beyond the ring as bearing dots on the rim
  uint8_t  uiScale;       // marker/text size: 0 = small, 1 = medium, 2 = large
  uint16_t minAltFt;      // hide aircraft below this altitude (ft); 0 = show all

  Airport airports[MAX_AIRPORTS];
  uint8_t airportCount;

  void setDefaults();
  void toJson(JsonObject o) const;
  void fromJson(JsonObjectConst o);
};

// ---- Top-level settings ----------------------------------------------------
struct Settings {
  // --- WiFi station networks (the device joins one of these) ---
  WifiCred wifi[MAX_WIFI_NETS];
  uint8_t  wifiCount;

  // --- Access point (config / fallback hotspot) ---
  String apSsid;
  String apPass;        // empty => open network
  String hostname;      // mDNS name => http://<hostname>.local

  // --- Write auth: required on state-changing endpoints when non-empty.
  // Empty (default) = no check, matches pre-existing open behavior.
  String secretKey;

  // --- Active feature ---
  uint8_t mode;         // MODE_STOCKS / MODE_USAGE / MODE_RADAR / MODE_CAROUSEL

  // --- Carousel (mode == MODE_CAROUSEL): dwell + which features rotate ---
  uint16_t carouselSec;
  bool carouselTicker, carouselUsage, carouselRadar, carouselAgenda, carouselAgenda2, carouselWeather, carouselZai, carouselOpenAi;
  // Comma-separated mode id()s (e.g. "usage,zai,agenda,weather,ticker"),
  // user-defined rotation order via the web UI's up/down arrows. Empty =
  // use the compiled-in kModes[] order (default, backward compatible).
  // Ids not present in this string, or present but no longer compiled in,
  // are appended in their kModes[] order -- see rebuildCarouselOrder() in
  // main.cpp -- so a firmware update adding/removing a mode never hides it.
  String carouselOrder;

  // --- Shared HTTP / display ---
  uint16_t httpTimeout; // ms
  uint8_t  brightness;        // 0..100 %
  bool     autoBrightness;    // use LDR on A0
  bool     backlightInverted; // active-low backlight
  uint8_t  rotation;          // 0..3 screen orientation

  // --- Device-wide color correction (applied at the display driver level,
  // so it affects every mode — ticker/usage/radar/clock/boot screens alike) ---
  uint8_t  toneR;    // 0..100 red gain,   100 = normal
  uint8_t  toneG;    // 0..100 green gain, 100 = normal
  uint8_t  toneB;    // 0..100 blue gain,  100 = normal (lower = warmer)
  uint8_t  toneSat;  // 0..200 saturation, 100 = normal, >100 = boosted

  // --- Feature slices ---
  TickerSettings   ticker;
  UsageSettings    usage;
  RadarSettings    radar;
  ClockSettings    clock;
  CalendarSettings calendar;

  void setDefaults();
};

// Persistence
bool settingsBegin();                       // mount LittleFS
bool loadSettings(Settings& s);             // false => defaults applied
bool saveSettings(const Settings& s);
void factoryReset(Settings& s);             // wipe file + defaults

// JSON <-> struct. `includeSecrets=false` masks passwords for the web API.
void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);
void settingsApplyJson(Settings& s, JsonObjectConst root); // partial update allowed
