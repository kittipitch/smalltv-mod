#include "ZaiMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "ZaiIcon.h"
#include "Clock.h"

ZaiMode g_zaiMode;

// Same palette subset CalendarMode.cpp uses -- not shared/exported anywhere
// yet, each mode defines what it needs.
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_UGREEN  0x7C6B   // sage green -- comfortable usage
#define C_ACCENT  0xDBAA   // terra-cotta -- getting close
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use
#define C_BARBG   0x2945   // unfilled bar track
// C_RED comes from Gfx.h (shared across modes already, see UsageMode.cpp)

// Same threshold philosophy as UsageMode.cpp's barColor() -- green under
// 75%, terra-cotta approaching the cap, red once actually tight.
static uint16_t pctColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_ACCENT;
  return C_UGREEN;
}

// Identical to UsageMode.cpp's fmtReset() -- kept as a near-copy rather than
// a shared helper, same reasoning as drawZaiClockOverlay below.
static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %2dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

// Same card shape as UsageMode.cpp's drawMeter() -- big %, label, fill bar,
// reset countdown -- so this page reads as a sibling of the Claude usage
// page, not a different visual language. Both z.ai limits carry a
// nextResetTime (confirmed live against the real endpoint -- an earlier
// session's partial capture wrongly assumed TOKENS_LIMIT had none, see
// CLAUDE.md's z.ai section for the correction), now parsed by the daemon.
// `full` gates the label + panel background exactly like drawMeter() --
// see that function's comment for why (an Opus review caught an
// unconditional-fill bug there; same reasoning applies here).
static void drawZaiMeter(Arduino_GFX* gfx, int top, const char* label,
                          bool has, int pct, bool hasReset, int resetMins,
                          bool full, bool growRight) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  // Both branches must produce the same-length string (see UsageMode.cpp's
  // drawMeter() comment on the "%3d%%" fixed-width invariant) -- a runtime
  // ternary picking the *format string* would also silently disable
  // -Wformat-truncation, since GCC can't check a non-literal format.
  char pc[8];
  if (has) snprintf(pc, sizeof(pc), "%3d%%", constrain(pct, 0, 100));
  else     strlcpy(pc, "  --", sizeof(pc));
  uint8_t sz = gfxFitSize(pc, 150, 5);
  int pcw = gfxTextW(pc, sz);
  gfx->setTextSize(sz);
  gfx->setTextColor(C_WHITE, C_PANEL);
  gfx->setCursor(x + w - pcw - 14, top + 10);
  gfx->print(pc);

  if (full) {
    gfx->setTextSize(2);
    gfx->setTextColor(C_DIM);
    gfx->setCursor(x + 14, top + 12);
    gfx->print(label);
  }

  int bx = x + 14, by = top + 52, bw = w - 28, bh = 12;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  int fw = has ? (int)(bw * constrain(pct, 0, 100) / 100.0f) : 0;
  // growRight anchors the fill to the track's right edge, growing leftward
  // as pct increases -- same UsageMode.cpp drawMeter() convention, kept in
  // sync via the same s.usage.barGrowRight setting so the two sibling
  // quota pages don't disagree when the user toggles it.
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(pct));

  // Row is always drawn, same as UsageMode.cpp's drawMeter() -- when
  // hasReset is false (daemon on an old version, or a future z.ai shape
  // change dropping nextResetTime), it prints "--" rather than a fake
  // "now", but still occupies the row so nothing else needs to clear it.
  char rs[16], line[10 + sizeof(rs) + 1];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  snprintf(line, sizeof(line), "Resets in %-7s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 64);
  gfx->print(line);
}

static void drawZaiPage(Arduino_GFX* gfx, const ZaiData& z, bool full, bool growRight) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: small logo + title, same slot/scale as UsageMode's mascot+CLAUDE.
    gfx->drawBitmap(6, 4, kZaiIcon, ZAI_ICON_SIZE, ZAI_ICON_SIZE, C_WHITE);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 12);
    gfx->print("Z.AI");
  }

  drawZaiMeter(gfx, 50,  "5h",  z.hasPct5h,  z.pct5h,  z.hasR5h,  z.r5h,  full, growRight);
  drawZaiMeter(gfx, 138, "MCP", z.hasPctMcp, z.pctMcp, z.hasRMcp, z.rMcp, full, growRight);
}

// Same flip-clock overlay as UsageMode.cpp -- per explicit request, any
// quota-style page (Claude usage, this one) gets the clock, not just one.
// Kept as a near-identical copy rather than a shared helper: each mode owns
// its own render/dirty state per Mode.h's design (self-contained features),
// and the two card geometries (cardsRight = x=8,w=224) already happen to
// match, so there's nothing real to factor out yet.
static void drawZaiClockOverlay(uint32_t& nextRedrawMs, int& lastMinute) {
  struct tm t;
  if (!clockNow(t)) return;
  int curMinute = t.tm_hour * 60 + t.tm_min;
  bool minuteChanged = curMinute != lastMinute;
  if (!minuteChanged && (int32_t)(millis() - nextRedrawMs) < 0) return;
  nextRedrawMs = millis() + 30000;
  lastMinute = curMinute;

  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  const char digits[4] = { buf[0], buf[1], buf[3], buf[4] };

  const int cardW = 13, cardH = 22, gapIn = 1, gapPair = 3;
  const int totalW = cardW * 4 + gapIn * 2 + gapPair;
  const int cardsRight = 8 + 224;
  const int x0 = cardsRight - totalW, y = 14;
  gfx->fillRect(x0 - 2, y, totalW + 4, cardH, C_BLACK);
  int cx = x0;
  for (int i = 0; i < 4; i++) {
    gfx->fillRoundRect(cx, y, cardW, cardH, 2, C_PANEL);
    gfx->setTextSize(2);
    gfx->setTextColor(C_RED, C_PANEL);
    gfx->setCursor(cx + 1, y + 3);
    gfx->print(digits[i]);
    cx += cardW + (i == 1 ? gapPair : gapIn);
  }
}

void ZaiMode::begin(const Settings& s) {
  // calendarInit(s) is deliberately NOT called here -- it would re-clear
  // g_cal/g_weather too (it's shared across all CalendarClient consumers),
  // and CalendarAgendaMode/CalendarWeatherMode's begin() already calls it
  // once. g_zai is a file-scope static, zero-initialized at boot regardless.
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  zaiRenderedOk_ = 0xFFFFFFFF;
  // Force the clock to repaint on the very next service() tick -- otherwise
  // a structural redraw (fillScreen) wipes it and it stays blank for up to
  // 30s, since its own timer/lastMinute state doesn't know a wipe happened.
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void ZaiMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  zaiRenderedOk_ = 0xFFFFFFFF;
  // Force the clock to repaint on the very next service() tick -- otherwise
  // a structural redraw (fillScreen) wipes it and it stays blank for up to
  // 30s, since its own timer/lastMinute state doesn't know a wipe happened.
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void ZaiMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void ZaiMode::service(const Settings& s) {
  const ZaiData& z = zaiGet();
  if (z.lastOkMs != zaiRenderedOk_) { zaiRenderedOk_ = z.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawZaiPage(gfx, z, needFullRender_, s.usage.barGrowRight);
    needRender_ = false;
    needFullRender_ = false;
  }
  drawZaiClockOverlay(clockNextRedrawMs_, clockLastMinute_);
}
