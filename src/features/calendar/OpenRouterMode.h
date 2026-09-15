// OpenRouterMode.h -- OpenRouter API spend display, pushed by
// clawdmeter-daemon's --openrouter flag. Same lifecycle shape as
// AntigravityMode: single page, dirty-tracked off the last successful
// push's lastOkMs, no device-side network call of its own.
#pragma once
#include "Mode.h"
#include "config.h"

class OpenRouterMode : public DisplayMode {
 public:
  const char* id() const override { return "openrouter"; }
  uint8_t     modeConst() const override { return MODE_OPENROUTER; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;   // structural redraw (header+card) vs. data-only
  uint32_t openrouterRenderedOk_ = 0xFFFFFFFF;   // last OpenRouterData.lastOkMs drawn
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;
};

extern OpenRouterMode g_openrouterMode;
