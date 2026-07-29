#include "AntigravityMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"

AntigravityMode g_antigravityMode;

// Same palette subset CodexMode.cpp/ZaiMode.cpp use -- not shared/exported
// anywhere yet, each mode defines what it needs.
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_UGREEN  0x7C6B   // sage green -- comfortable usage
#define C_ACCENT  0xDBAA   // terra-cotta -- getting close
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use
#define C_BARBG   0x2945   // unfilled bar track
// C_RED comes from Gfx.h (shared across modes already, see UsageMode.cpp)

// Same threshold philosophy as UsageMode.cpp's barColor()/ZaiMode.cpp's
// pctColor() -- green under 75%, terra-cotta approaching the cap, red once
// actually tight.
static uint16_t pctColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_ACCENT;
  return C_UGREEN;
}

// Hard-truncate to maxChars, appending ".." when the source is longer (this
// font has no ellipsis glyph). maxChars includes the ".." itself. Copied
// from CalendarMode.cpp's truncateDots() (not exported/shared) -- needed
// here because, unlike Codex/z.ai's fixed short static labels
// ("5h"/"Week"/"MCP"), this page's model-name row is a real model name
// from the daemon ("Gemini 3.5 Flash (Low)" is 22 chars -- at this row's
// setTextSize(2), 12px/char, that's 264px against a 224px-wide row).
static void truncateDots(const char* in, char* out, size_t outCap, int maxChars) {
  int len = (int)strlen(in);
  if (len <= maxChars) { strlcpy(out, in, outCap); return; }
  if (outCap < 4) { strlcpy(out, "..", outCap); return; }
  int keep = maxChars - 2;
  if (keep < 1) keep = 1;
  if ((size_t)(keep + 3) > outCap) keep = (int)outCap - 3;
  memcpy(out, in, keep);
  out[keep] = '.'; out[keep + 1] = '.'; out[keep + 2] = 0;
}

// Identical to UsageMode.cpp's/CodexMode.cpp's fmtReset() -- kept as a
// near-copy rather than a shared helper, same reasoning as
// drawAntigravityClockOverlay below.
static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %2dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

// Same card shape as CodexMode.cpp's drawCodexMeter() -- big %, label, fill
// bar, reset countdown. Only ONE card on this page (not two like
// Codex/z.ai): the daemon's poll_antigravity() only has one clean metric
// (a single model's quotaInfo.remainingFraction) -- see that function's
// docstring in clawdmeter_daemon.py for why the account-level credit-pool
// fields it tried first were wrong and dropped.
static void drawAntigravityMeter(Arduino_GFX* gfx, int top, const char* label,
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
  // as pct increases -- same UsageMode.cpp/ZaiMode.cpp convention, kept in
  // sync via the same s.usage.barGrowRight setting so sibling quota pages
  // don't disagree when the user toggles it.
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(pct));

  // Row is always drawn, same as UsageMode.cpp's/CodexMode.cpp's meter --
  // when hasReset is false, it prints "--" rather than a fake "now", but
  // still occupies the row so nothing else needs to clear it.
  char rs[16], line[10 + sizeof(rs) + 1];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  snprintf(line, sizeof(line), "Resets in %-7s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 64);
  gfx->print(line);
}

static void drawAntigravityPage(Arduino_GFX* gfx, const AntigravityData& a, bool full, bool growRight) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: plain text title, no logo bitmap (no icon-rasterization
    // pipeline run this session -- see ZaiMode.cpp for the pipeline that
    // would produce one, if this page ever gets one later).
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(8, 12);
    gfx->print("Antigravity");
  }

  // Model-name row, its OWN row (y=60) -- NOT inside the card. The card's
  // internal label slot (x+14, same row as the percentage, sized for a
  // short static string like Codex's "5h"/"Week") is 12 chars wide at
  // most before it collides with the percentage text on the right; a real
  // model name ("Gemini 3.5 Flash (Low)") doesn't fit there. This row
  // spans the full 224px width instead. Drawn UNCONDITIONALLY (not gated
  // on `full`) with fixed-width padding (`%-18s`) and an opaque
  // setTextColor(fg,bg) -- the model backing the percentage can change
  // between polls (that's the whole reason this label exists: see
  // poll_antigravity()'s docstring on why "first isRecommended" wasn't
  // stable), so a shorter new label must fully overwrite a longer old one
  // on every data-only redraw, not just on a structural repaint.
  char trimmed[19];
  truncateDots(a.hasLabel ? a.label : "Model", trimmed, sizeof(trimmed), 18);
  char label[19];
  snprintf(label, sizeof(label), "%-18s", trimmed);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_BLACK);
  gfx->setCursor(8, 60);
  gfx->print(label);

  // Single card, vertically centered between the header and the bottom
  // edge (same 82px card height as Codex/z.ai's cards, just one instead of
  // two stacked at 50/138 -- top=94 splits the remaining space evenly).
  // Its own internal label reverts to a static "Model" -- the real model
  // name is the row above.
  drawAntigravityMeter(gfx, 94, "Model", a.hasPctModel, a.pctModel, a.hasRModel, a.rModel, full, growRight);
}

// Same flip-clock overlay as UsageMode.cpp/CodexMode.cpp -- per the same
// "any quota-style page gets the clock" reasoning. Kept as a near-identical
// copy rather than a shared helper: each mode owns its own render/dirty
// state per Mode.h's design.
static void drawAntigravityClockOverlay(uint32_t& nextRedrawMs, int& lastMinute) {
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

void AntigravityMode::begin(const Settings& s) {
  // calendarInit(s) is deliberately NOT called here -- same reasoning as
  // ZaiMode::begin()/CodexMode::begin(): it would re-clear g_cal/g_weather
  // too (shared across all CalendarClient consumers), and another mode's
  // begin() already calls it once. g_antigravity is a file-scope static,
  // zero-initialized at boot.
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  antigravityRenderedOk_ = 0xFFFFFFFF;
  // Force the clock to repaint on the very next service() tick -- otherwise
  // a structural redraw (fillScreen) wipes it and it stays blank for up to
  // 30s, since its own timer/lastMinute state doesn't know a wipe happened.
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void AntigravityMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  antigravityRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void AntigravityMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void AntigravityMode::service(const Settings& s) {
  const AntigravityData& a = antigravityGet();
  if (a.lastOkMs != antigravityRenderedOk_) { antigravityRenderedOk_ = a.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawAntigravityPage(gfx, a, needFullRender_, s.usage.barGrowRight);
    needRender_ = false;
    needFullRender_ = false;
  }
  drawAntigravityClockOverlay(clockNextRedrawMs_, clockLastMinute_);
}
