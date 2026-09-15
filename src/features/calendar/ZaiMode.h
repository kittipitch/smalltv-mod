// ZaiMode.h — z.ai (GLM) quota display, pushed by clawdmeter-daemon's --zai
// flag. Same shape as CalendarWeatherMode: single page, dirty-tracked off
// the last successful push's lastOkMs, no device-side fetch of its own.
#pragma once
#include "Mode.h"
#include "config.h"

class ZaiMode : public DisplayMode {
 public:
  const char* id() const override { return "zai"; }
  uint8_t     modeConst() const override { return MODE_ZAI; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;   // structural redraw (header+cards) vs. data-only
  uint32_t zaiRenderedOk_ = 0xFFFFFFFF;   // last ZaiData.lastOkMs drawn
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;
};

extern ZaiMode g_zaiMode;
