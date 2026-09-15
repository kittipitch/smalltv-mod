// AntigravityMode.h — Antigravity CLI's (`agy`) real model quota display,
// pushed by clawdmeter-daemon's --antigravity flag. Same shape as
// CodexMode/ZaiMode: single page, dirty-tracked off the last successful
// push's lastOkMs, no device-side fetch of its own. UNLIKE Codex, each
// daemon poll fires a real, costed `agy -p` prompt (see poll_antigravity()'s
// docstring in clawdmeter_daemon.py for why no free path exists) -- single
// card, not two windows, since the only clean metric available is one
// model's quotaInfo.remainingFraction.
#pragma once
#include "Mode.h"
#include "config.h"

class AntigravityMode : public DisplayMode {
 public:
  const char* id() const override { return "antigravity"; }
  uint8_t     modeConst() const override { return MODE_ANTIGRAVITY; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;   // structural redraw (header+card) vs. data-only
  uint32_t antigravityRenderedOk_ = 0xFFFFFFFF;   // last AntigravityData.lastOkMs drawn
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;
};

extern AntigravityMode g_antigravityMode;
