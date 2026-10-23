// CopilotMode.h -- GitHub Copilot Chat and Completions monthly quota.
#pragma once
#include "Mode.h"
#include "config.h"

class CopilotMode : public DisplayMode {
 public:
  const char* id() const override { return "copilot"; }
  uint8_t     modeConst() const override { return MODE_COPILOT; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  bool     needFullRender_ = true;
  uint32_t copilotRenderedOk_ = 0xFFFFFFFF;
  uint32_t clockNextRedrawMs_ = 0;
  int      clockLastMinute_ = -1;
};

extern CopilotMode g_copilotMode;
