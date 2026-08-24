// CalendarMode.h — 3 independent modes: next calendar event (agenda),
// agenda's 2nd page (events 4-6), and combined weather + air quality.
//
// Originally one auto-rotating "calendar" mode with 3 internal timed
// sub-pages (agenda/weather/AQI); split into independent DisplayModes (own
// carousel checkbox each, own Mode-dropdown entry each) per explicit
// request — "dont subpage it make each page independent" — then weather
// and AQI were lumped back into one combined page ("make agenda separate
// page and lump weather and pm together") since they're closely related
// and the split left too many thin single-purpose pages. Minimalist
// stacked-line layout per-page, plus a small vector weather icon (no
// bitmap assets, no image fetch — see CalendarMode.cpp's drawWeatherIcon).
// See CalendarClient.h for the two independent data paths (calendar
// pushed by the daemon, weather/AQI also pushed by the daemon).
//
// Agenda's 2nd page (CalendarAgendaMode2) follows the same independent-mode
// pattern rather than an internal auto-flip timer within one mode -- tried
// that first (a page_/PAGE_DWELL_MS timer inside CalendarAgendaMode) but it
// needed PAGE_DWELL_MS < the carousel's own per-mode dwell (carouselSec) to
// ever be seen, which isn't guaranteed and wasn't reliable in practice
// ("we only see 3 events now and six.. not six"). A real second carousel
// entry reuses the carousel's own existing dwell/rotation logic for free,
// and gets its own checkbox to show/hide independently -- "we can
// check/uncheck it (to display 3 or 6)". Auto-hidden from the carousel
// when there aren't more than 3 events (carouselHas() in main.cpp), same
// "skip when there's nothing to show" pattern the ticker and z.ai modes
// already use.
#pragma once
#include "Mode.h"
#include "config.h"

class CalendarAgendaMode : public DisplayMode {
 public:
  const char* id() const override { return "agenda"; }
  uint8_t     modeConst() const override { return MODE_CAL_AGENDA; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  bool     needRender_ = true;
  uint32_t calRenderedOk_ = 0xFFFFFFFF;   // last CalendarEvent.lastOkMs drawn
};

class CalendarAgendaMode2 : public DisplayMode {
 public:
  const char* id() const override { return "agenda2"; }
  uint8_t     modeConst() const override { return MODE_CAL_AGENDA2; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  bool     needRender_ = true;
  uint32_t calRenderedOk_ = 0xFFFFFFFF;   // last CalendarEvent.lastOkMs drawn
};

class CalendarWeatherMode : public DisplayMode {
 public:
  const char* id() const override { return "weather"; }
  uint8_t     modeConst() const override { return MODE_CAL_WEATHER; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  bool     needRender_ = true;
  uint32_t wxRenderedOk_ = 0xFFFFFFFF;   // last WeatherData.lastOkMs drawn (covers AQI too)
};

class CalendarForecastMode : public DisplayMode {
 public:
  const char* id() const override { return "forecast"; }
  uint8_t     modeConst() const override { return MODE_CAL_FORECAST; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }

 private:
  bool     needRender_ = true;
  uint32_t wxRenderedOk_ = 0xFFFFFFFF;   // last WeatherData.lastOkMs drawn
};

extern CalendarAgendaMode   g_calendarAgendaMode;
extern CalendarAgendaMode2  g_calendarAgendaMode2;
extern CalendarWeatherMode  g_calendarWeatherMode;
extern CalendarForecastMode g_calendarForecastMode;
