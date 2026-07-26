// CalendarMode.h — next calendar event + weather/air-quality feature.
//
// Minimalist stacked-line layout (chosen over 3 alternative designs — card-
// stack, hero-countdown, split-half — for being the simplest to implement
// correctly on this RAM-tight chip while still meeting the size>=3 text
// floor with no exceptions). No cards, no icons — hierarchy via text size
// and color alone. See CalendarClient.h for the two independent data paths
// (calendar pushed by the daemon, weather/AQI fetched directly).
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
  bool     needRender_ = true;
  uint32_t calRenderedOk_ = 0xFFFFFFFF;   // last CalendarEvent.lastOkMs drawn
  uint32_t wxRenderedOk_  = 0xFFFFFFFF;   // last WeatherData.lastOkMs drawn
};

extern CalendarMode g_calendarMode;
