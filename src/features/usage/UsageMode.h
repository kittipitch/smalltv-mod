// UsageMode.h — Claude usage meter feature.
//
// Shows 5h/7d usage bars + a small mascot when data is flowing, an animated
// pixel-art mascot while waiting for the first reading of the boot, and a
// dimmed-header stale page once a feed that was working goes quiet. Owns its
// fetch (UsageClient), its mascot animation (Mascot) and its render/dirty state.
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
  // Tracks the last-rendered hasWeekly so a flip forces a FULL redraw: the
  // percentage is right-aligned, so "N/A" and " 12%" start at different x
  // and the opaque-glyph overwrite alone would leave stale pixels behind.
  int8_t   weeklyKnownRendered_ = -1;   // -1 = nothing rendered yet
  bool     showingMascot_ = false;
  // The mascot now means ONE thing: warming up (no reading has landed yet this
  // boot). A feed that landed and then went quiet keeps its last values,
  // notice instead -- the two used to be the same animation, which is why a
  // 54-minute outage on 2026-09-08 looked identical to a normal cold start.
  bool     showingDaemonOff_ = false;
  uint32_t daemonOffNextMs_ = 0;   // next elapsed-line repaint on that notice
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
