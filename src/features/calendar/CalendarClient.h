// CalendarClient.h — two independent data sources for MODE_CALENDAR:
//   - Calendar: pushed by clawdmeter-daemon (POST /api/calendar). The device
//     never does Google OAuth itself; calendarApply() just parses the payload.
//   - Weather + air quality: fetched directly by the device from Open-Meteo
//     (two separate HTTPS hosts, no API key). Same shape as the ticker's
//     direct fetches.
#pragma once
#include "Settings.h"
#include "CalendarData.h"

void calendarInit(const Settings& s);
void calendarService(const Settings& s);    // call each loop; fetches weather on schedule
void calendarForceRefresh();                // poll weather again on the next service() call

const CalendarEvent& calendarGet();
const WeatherData&   weatherGet();

// Apply a calendar payload PUSHED to the device (POST /api/calendar).
bool calendarApply(const String& body);     // parse {ok,summary,start,allDay}; true on success
