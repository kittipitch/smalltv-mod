// CalendarMode.h — 2 independent modes: next calendar event (agenda), and
// combined weather + air quality.
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
  // Re-entering the carousel always shows page 1 first, with a fresh
  // PAGE_DWELL_MS before it flips -- without resetting pageSwitchMs_ here,
  // it'd still hold a timestamp from whenever this mode was last active,
  // so a carousel re-entry after >=PAGE_DWELL_MS away would flip pages on
  // the very first tick (stale page flashes, then an instant flip).
  void wake(const Settings& s) override { needRender_ = true; page_ = 0; pageSwitchMs_ = millis(); }

 private:
  bool     needRender_ = true;
  uint32_t calRenderedOk_ = 0xFFFFFFFF;   // last CalendarEvent.lastOkMs drawn
  uint8_t  page_ = 0;          // which 3-event page is showing (0 or 1)
  uint32_t pageSwitchMs_ = 0;  // millis() of the last page flip
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

extern CalendarAgendaMode  g_calendarAgendaMode;
extern CalendarWeatherMode g_calendarWeatherMode;
