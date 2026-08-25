#include "OpenRouterMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "OpenRouterIcon.h"

OpenRouterMode g_openrouterMode;

// Same palette subset AntigravityMode.cpp uses -- not shared/exported
// anywhere yet, each mode defines what it needs.
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_UGREEN  0x7C6B   // sage green -- zero/near-zero daily spend
#define C_PURPLE  0x79DD   // OpenRouter brand violet, approximate (~#7C3AED) --
                            // not pixel-sampled, the source PNG was a temp
                            // upload that had already expired by the time the
                            // icon color was picked
#ifdef C_YELLOW
#undef C_YELLOW
#endif
#define C_YELLOW  0xFE01   // caution yellow ~#FFC107 -- same value CalendarMode.cpp's
                            // C_STORM uses, already proven distinguishable from C_ACCENT
                            // on this screen
#define C_ACCENT  0xDBAA   // terra-cotta -- kept with sibling palette copy
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use
#define C_BARBG   0x2945   // unfilled bar track, unused here by design
// C_RED comes from Gfx.h (shared across modes already, see UsageMode.cpp)

static void fmtUsd(bool has, double v, char* out, size_t n) {
  if (!has) { strlcpy(out, "--", n); return; }
  if (v < 1.0) snprintf(out, n, "$%.3f", v);
  else         snprintf(out, n, "$%.2f", v);
}

// Two stacked cards, same top=50/138 (x=8, w=224, h=82) geometry every
// sibling quota page (Codex/z.ai/Antigravity) uses for its two-window
// layout -- per explicit feedback ("we just keep Today and Week and make it
// two cards like other quota pages"), dropping Total and the single-card
// three-row layout entirely. No bar/reset row here (unlike the siblings):
// OpenRouter gives dollars, not a percentage against a real limit, so
// there's nothing to fill a bar with -- see the page's own design note
// history. Value at size5, the same "every other quota page's card" size
// AntigravityMode.cpp's own comment references (that page uses size4
// instead only because its bar+reset row eats the vertical room this page
// doesn't spend).
static void drawOpenRouterCard(Arduino_GFX* gfx, int top, const char* label,
                                bool has, double value, uint16_t valueColor,
                                bool full) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 12);
  gfx->print(label);

  char val[18];
  fmtUsd(has, value, val, sizeof(val));
  uint8_t sz = gfxFitSize(val, w - 28, 5);
  int valW = gfxTextW(val, sz);
  int vx = x + w - valW - 14;
  if (vx < x + 14) vx = x + 14;

  gfx->fillRect(x + 14, top + 34, w - 28, 40, C_PANEL);   // clear the value row every redraw
  gfx->setTextSize(sz);
  gfx->setTextColor(has ? valueColor : C_DIM, C_PANEL);
  gfx->setCursor(vx, top + 34);
  gfx->print(val);
}

static void drawOpenRouterPage(Arduino_GFX* gfx, const OpenRouterData& o, bool full) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // y=12, matching the other quota pages' header icon margin -- see
    // AntigravityMode.cpp's drawAntigravityPage() comment for why 12, not 4/8.
    gfx->drawBitmap(6, 12, kOpenRouterIcon, OPENROUTER_ICON_SIZE, OPENROUTER_ICON_SIZE, C_PURPLE);
    // size2, not every sibling page's size3 -- "OpenRouter" is long enough at
    // size3 (180px) that it runs into the clock overlay's x=173..235 region
    // and gets its last ~4 letters overpainted the moment NTP syncs (codex
    // sol pre-flash audit finding, 2026-08-25). At size2 (120px from x=44)
    // it clears the clock with room to spare.
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(44, 20);
    gfx->print("openrouter");   // brand is all-lowercase, not "OpenRouter"
  }

  uint16_t dailyColor = (o.hasUsdDaily && o.usdDaily <= 0.001) ? C_UGREEN : C_RED;
  drawOpenRouterCard(gfx, 50,  "Today", o.hasUsdDaily,  o.usdDaily,  dailyColor, full);
  drawOpenRouterCard(gfx, 138, "Week",  o.hasUsdWeekly, o.usdWeekly, C_WHITE,    full);
}

// Same flip-clock overlay as UsageMode.cpp/CodexMode.cpp/AntigravityMode.cpp
// -- per the same "any quota-style page gets the clock" reasoning. Kept as a
// near-identical copy rather than a shared helper: each mode owns its own
// render/dirty state per Mode.h's design.
static void drawOpenRouterClockOverlay(uint32_t& nextRedrawMs, int& lastMinute) {
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

void OpenRouterMode::begin(const Settings& s) {
  // calendarInit(s) is deliberately NOT called here -- same reasoning as
  // ZaiMode::begin()/CodexMode::begin()/AntigravityMode::begin(): it would
  // re-clear shared calendar/weather quota data, and another calendar mode's
  // begin() already calls it once. g_openrouter is a file-scope static,
  // zero-initialized at boot.
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  openrouterRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void OpenRouterMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  openrouterRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void OpenRouterMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void OpenRouterMode::service(const Settings& s) {
  (void)s;
  const OpenRouterData& o = openrouterGet();
  if (o.lastOkMs != openrouterRenderedOk_) { openrouterRenderedOk_ = o.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawOpenRouterPage(gfx, o, needFullRender_);
    needRender_ = false;
    needFullRender_ = false;
  }
  drawOpenRouterClockOverlay(clockNextRedrawMs_, clockLastMinute_);
}
