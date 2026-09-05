// CalendarClient.h — two independent data sources for MODE_CALENDAR, both
// PUSHED by clawdmeter-daemon (not fetched by the device):
//   - Calendar: POST /api/calendar. The device never does Google OAuth
//     itself; calendarApply() just parses the payload.
//   - Weather + air quality: POST /api/weather, AND a device-direct fetch as a
//     fallback (weatherService(), WITH_DEVICE_WEATHER). A push always wins while
//     one keeps arriving; the device only fetches for itself once pushes go
//     quiet, so a unit with no daemon still shows weather. The fetch is plain
//     HTTP -- no BearSSL heap -- which is what makes it viable now where the
//     original device-direct path was fragile.
#pragma once
#include "Settings.h"
#include "CalendarData.h"

void calendarInit(const Settings& s);

// Device-direct weather/AQI fetch (WITH_DEVICE_WEATHER, default on). A push to
// /api/weather always wins while one keeps arriving; this only runs once pushes
// have gone quiet, so a device with no daemon still shows weather. Call each loop.
void weatherService(const Settings& s);

// Last device-direct fetch outcome, for /api/status: HTTP code per endpoint
// (200 ok, -100 parsed-badly, negative HTTPC_ERROR_* connect fail, 0 never
// tried) and how long ago it ran.
void weatherFetchDiag(int& fc, int& aq, int& aqf, uint32_t& agoMs);

const CalendarEvent& calendarGet();
const WeatherData&   weatherGet();
const ZaiData&        zaiGet();
const CodexData&      codexGet();
const AntigravityData& antigravityGet();
const OpenRouterData& openrouterGet();

// Apply payloads PUSHED to the device.
bool calendarApply(const String& body);   // {ok,events:[{summary,start,end?,allDay,color?}, ...]}
bool weatherApply(const String& body);    // {ok,tempC?,precipPct?,weatherCode?,pm25?,aqi?}
bool zaiApply(const String& body);        // {ok,pct5h?,pctTokens?}
bool codexApply(const String& body);      // {ok,pct5h?,r5h?,pctWeek?,rWeek?}
bool antigravityApply(const String& body); // {ok,pctPro?,labelPro?,rPro?,pctFlash?,labelFlash?,rFlash?}
bool openrouterApply(const String& body); // {ok,usd_daily?,usd_weekly?,usd_total?,free_tier?}
