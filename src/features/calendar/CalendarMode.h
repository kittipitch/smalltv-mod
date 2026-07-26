// CalendarMode.h — next calendar event + weather/air-quality feature.
//
// Minimalist stacked-line layout per-page (chosen over 3 alternative designs
// — card-stack, hero-countdown, split-half — for being the simplest to
// implement correctly on this RAM-tight chip while still meeting the size>=3
// text floor with no exceptions). Split into 3 timed sub-pages (agenda /
// weather / air quality) rather than one stacked screen — hierarchy via text
// size and color, plus a small vector weather icon (no bitmap assets, no
// image fetch — see CalendarMode.cpp's drawWeatherIcon). See CalendarClient.h
// for the two independent data paths (calendar pushed by the daemon,
// weather/AQI fetched directly).
#pragma once
#include "Mode.h"
#include "config.h"

class CalendarMode : public DisplayMode {
 public:
  const char* id() const override { return "calendar"; }
  uint8_t     modeConst() const override { return MODE_CALENDAR; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override { needRender_ = true; }   // repaint only

 private:
  enum Page : uint8_t { PAGE_AGENDA = 0, PAGE_WEATHER = 1, PAGE_AQI = 2, PAGE_COUNT = 3 };

  bool     needRender_ = true;
  uint32_t calRenderedOk_ = 0xFFFFFFFF;   // last CalendarEvent.lastOkMs drawn
  uint32_t wxRenderedOk_  = 0xFFFFFFFF;   // last WeatherData.lastOkMs drawn
  Page     page_ = PAGE_AGENDA;
  uint32_t nextPageMs_ = 0;
};

extern CalendarMode g_calendarMode;
