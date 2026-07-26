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
  void wake(const Settings& s) override { needRender_ = true; }   // repaint only

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

extern CalendarAgendaMode  g_calendarAgendaMode;
extern CalendarWeatherMode g_calendarWeatherMode;
