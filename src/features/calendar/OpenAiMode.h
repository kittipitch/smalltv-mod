// OpenAiMode.h — OpenAI API cheap-ping quota display, pushed by
// clawdmeter-daemon's --openai flag. Same shape as ZaiMode: single page,
// dirty-tracked off the last successful push's lastOkMs, no device-side
// fetch of its own. Tracks OpenAI API *billing* usage (a paid API key), NOT
// a ChatGPT Plus subscription -- no public API exists for that.
#pragma once
#include "Mode.h"
#include "config.h"

class OpenAiMode : public DisplayMode {
 public:
  const char* id() const override { return "openai"; }
  uint8_t     modeConst() const override { return MODE_OPENAI; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;   // structural redraw (header+cards) vs. data-only
  uint32_t openaiRenderedOk_ = 0xFFFFFFFF;   // last OpenAiData.lastOkMs drawn
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;
};

extern OpenAiMode g_openaiMode;
