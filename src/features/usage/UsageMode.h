// UsageMode.h — Claude usage meter feature.
//
// Shows 5h/7d usage bars + a small mascot when data is flowing, and an animated
// pixel-art mascot when the daemon goes quiet. Owns its fetch (UsageClient), its
// mascot animation (Mascot) and its render/dirty state.
#pragma once
#include "Mode.h"
#include "config.h"

class UsageMode : public DisplayMode {
 public:
  const char* id() const override { return "usage"; }
  uint8_t     modeConst() const override { return MODE_USAGE; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;   // defined in the .cpp: it must reach
                                          // file-static mascot state (s_mascotPrimed)

 private:
  void drawClockOverlay();   // top-right "HH:MM", this page only (see UsageMode.cpp)
  uint32_t usageSampled_ = 0;              // lastOkMs already fed to the mascot tracker
  uint32_t usageRenderedOk_ = 0xFFFFFFFF;
  bool     showingMascot_ = false;
  bool     needRender_ = true;
  // Structural changes (wake/invalidate/mascot-exit/warmth-saturation change)
  // need a full screen clear + header/mascot redraw; a plain new data push
  // doesn't — see drawUsage()'s `full` param and its self-clearing meters.
  bool     needFullRender_ = true;
  uint32_t clockNextRedrawMs_ = 0;   // top-right "HH:MM" overlay, this page only
  int      clockLastMinute_ = -1;    // forces an immediate redraw on minute change,
                                      // not just every 30s -- avoids showing a stale
                                      // digit for up to 30s after the real change
};

extern UsageMode g_usageMode;
