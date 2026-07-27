// CalendarClient.h — two independent data sources for MODE_CALENDAR, both
// PUSHED by clawdmeter-daemon (not fetched by the device):
//   - Calendar: POST /api/calendar. The device never does Google OAuth
//     itself; calendarApply() just parses the payload.
//   - Weather + air quality: POST /api/weather. Originally fetched directly
//     by the device from Open-Meteo, but that path proved hard to debug
//     on the ESP8266 (no serial access in this project's workflow, opaque
//     TLS/HTTP failures) -- moved to the daemon, which has real logging and
//     can be iterated on quickly. weatherApply() just parses the payload.
#pragma once
#include "Settings.h"
#include "CalendarData.h"

void calendarInit(const Settings& s);

const CalendarEvent& calendarGet();
const WeatherData&   weatherGet();
const ZaiData&        zaiGet();

// Apply payloads PUSHED to the device.
bool calendarApply(const String& body);   // {ok,events:[{summary,start,allDay}, ...]}
bool weatherApply(const String& body);    // {ok,tempC?,precipPct?,weatherCode?,pm25?,aqi?}
bool zaiApply(const String& body);        // {ok,pct5h?,pctTokens?}
