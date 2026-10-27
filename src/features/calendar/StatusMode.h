#pragma once
#include "Mode.h"
#include "config.h"
#include "CalendarData.h"

class Arduino_GFX;

void drawStatusPage(Arduino_GFX* gfx, const UptimeData& u, uint32_t effAgeS, bool nodata);
// One 224x52 slot (row 0..3); every slot clears its own row, so a slot repaints alone.
void drawStatusSlot(Arduino_GFX* gfx, const UptimeData& u, int i, uint32_t effAgeS, bool nodata);

class StatusMode : public DisplayMode {
 public:
  const char* id() const override { return "status"; }
  uint8_t     modeConst() const override { return MODE_STATUS; }

  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;

 private:
  bool     needRender_ = true;
  uint32_t slotSig_[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};   // per-row content signature
};

extern StatusMode g_statusMode;
