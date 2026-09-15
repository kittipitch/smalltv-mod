// CalendarClient.h — two independent data sources for MODE_CALENDAR:
//   - Calendar: PUSH ONLY (POST /api/calendar). The device never does Google
//     OAuth itself; calendarApply() just parses the payload. When pushes stop,
//     the last received events stay on screen -- they are not retired by age.
//   - Weather + air quality: push (POST /api/weather) OR the device's own fetch
//     (weatherService(), WITH_DEVICE_WEATHER), per half. A push wins while one
//     keeps arriving; WX_PUSH_GRACE_MS after the last one this device takes
//     over. Both are first-class: a daemon that lives on a laptop is off much
//     of the time, and this is what keeps the screen current meanwhile. Plain
//     HTTP -- no BearSSL heap -- which is what makes it viable where the
//     original device-direct path was fragile.
#pragma once
#include "Settings.h"
#include "CalendarData.h"

void calendarInit(const Settings& s);

// Device-direct weather/AQI fetch (WITH_DEVICE_WEATHER, default on). A push to
// /api/weather wins while one keeps arriving -- checked PER HALF, so a daemon
// feeding only weather does not starve air quality. Call each loop(): it does at
// most ONE HTTP GET per call, so a dead endpoint cannot freeze the web server.
void weatherService(const Settings& s);

// Last device-direct fetch outcome, for /api/status: HTTP code per endpoint
// (200 ok, -100 parsed-badly, negative HTTPC_ERROR_* connect fail, 0 never
// tried) and how long ago it ran. Skip codes: -101 parsed but every hour null,
// -102 no forecast days to decorate, -103 same host already known down.
//
// Being negative is NOT enough to count as evidence of an outage, and an
// earlier version of this comment wrongly said it was (codex, ac302d9
// re-audit). The skip codes above are excluded precisely because a skipped
// fetch proves nothing about the link; -104 (current-AQ skipped when the
// forecast host was unreachable) was removed outright for that reason, and the
// air-quality GET now always runs so aq always carries its own evidence.
//
// THE CURRENT RULE, restated in full because this comment is what the next
// person will judge a new skip code against, and it had drifted twice:
//   counts as "no response":  -1 only. A connect() that never completed.
//   NOT evidence of an outage: -4/-5/-11 (all emitted from writeToPrint() or
//     handleHeaderResponse(), i.e. AFTER the TCP handshake succeeded, so they
//     prove the opposite), -100/-105 (reached the server, body unusable or
//     empty), -101/-102/-103/-106 (skips), -8 (local: no location set), 0
//     (never tried), and any 2xx/4xx/5xx.
//   AND a single failing cycle is never enough: linkState() additionally
//     requires weatherFailStreak() >= 2, because both halves share one DNS
//     resolver and one provider, so one cycle failing both is a router hiccup.
// The codes reported here belong to the last COMPLETED cycle; a cycle in
// flight never moves them (see g_wxCur* in the .cpp).
void weatherFetchDiag(int& fc, int& aq, int& aqf, uint32_t& agoMs);
// How long a recorded fetch failure still counts as CURRENT evidence, in ms.
// Tracks the interval actually in force, so backoff cannot expire a DEVICE
// OFFLINE verdict in the middle of the outage that produced it.
uint32_t weatherAttemptWindowMs();
// Consecutive fetch cycles that achieved nothing. linkState() requires >= 2
// before declaring the internet down: both endpoints share one DNS resolver and
// one provider, so a single cycle failing both is a routine hiccup, not proof.
uint8_t  weatherFailStreak();
// Call when the configured lat/lon changes. Drops location-bound data (a
// pushed city name belongs to the OLD coordinates) and makes the next fetch
// due immediately, instead of leaving the previous deadline to expire.
void weatherLocationChanged();
// Call when weatherPollSec changes. Re-arms the schedule so a cadence cut takes
// effect now instead of at a deadline computed from the OLD interval (up to
// 18.2 h away), without discarding data that is still valid for this location.
void weatherCadenceChanged();

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
