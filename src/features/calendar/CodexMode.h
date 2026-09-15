// CodexMode.h — Codex CLI's real ChatGPT-plan quota display, pushed by
// clawdmeter-daemon's --codex flag. Same shape as ZaiMode: single page,
// dirty-tracked off the last successful push's lastOkMs, no device-side
// fetch of its own. NOT an OpenAI API billing key -- this rides Codex CLI's
// own rate-limit data (real ChatGPT plan usage you already pay for), read
// by the daemon after a real `codex exec` call (see poll_codex()'s
// docstring for the full mechanism and why it superseded an earlier,
// wrong OpenAI-API-header-based design -- see CLAUDE.md).
#pragma once
#include "Mode.h"
#include "config.h"

class CodexMode : public DisplayMode {
 public:
  const char* id() const override { return "codex"; }
  uint8_t     modeConst() const override { return MODE_CODEX; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;   // structural redraw (header+cards) vs. data-only
  uint32_t codexRenderedOk_ = 0xFFFFFFFF;   // last CodexData.lastOkMs drawn
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;
};

extern CodexMode g_codexMode;
