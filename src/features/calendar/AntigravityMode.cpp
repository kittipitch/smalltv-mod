#include "AntigravityMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "AntigravityIcon.h"

AntigravityMode g_antigravityMode;

// Same palette subset CodexMode.cpp/ZaiMode.cpp use -- not shared/exported
// anywhere yet, each mode defines what it needs.
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_UGREEN  0x7C6B   // sage green -- comfortable usage
#define C_YELLOW  0xFE01   // caution yellow ~#FFC107 -- same value CalendarMode.cpp's
                            // C_STORM uses, already proven distinguishable from C_ACCENT
                            // on this screen
#define C_ACCENT  0xDBAA   // terra-cotta -- getting close
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use
#define C_BARBG   0x2945   // unfilled bar track
#define C_SKY     0x5D9C   // muted blue -- same value CalendarMode.cpp uses for weather rain
                            // icon strokes; picked for the logo per live feedback ("bluish")
// C_RED comes from Gfx.h (shared across modes already, see UsageMode.cpp)

// Same threshold philosophy as UsageMode.cpp's barColor()/ZaiMode.cpp's
// pctColor() -- green under 50%, yellow climbing, terra-cotta approaching
// the cap, red once actually tight. Matches the statusline script's own
// green/yellow/orange/red bands.
static uint16_t pctColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_ACCENT;
  if (pct >= 50) return C_YELLOW;
  return C_UGREEN;
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

// Two stacked cards, same shape/spacing as Codex/z.ai's two-window layout
// (top=50/138, 82px tall each) -- per live feedback ("we can have two
// cards") replacing an earlier single-card version that crammed a
// shortened model code onto its own line inside the percentage card.

// Hard-truncate to maxChars, appending ".." when the source is longer
// (this font has no ellipsis glyph). maxChars includes the ".." itself.
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

// Split label into up to 2 lines, each <= maxChars, breaking at the last
// space at-or-before maxChars so real model names ("Gemini 3.6 Flash
// (Medium)") wrap on a word boundary instead of getting hard-truncated
// mid-word. If no space exists within maxChars (or the whole label already
// fits on one line), line 2 is left empty. Line 2 itself is truncateDots-
// capped in case the remainder is still too long for a single line.
static void wrapModelName(const char* label, char* l1, size_t l1cap,
                           char* l2, size_t l2cap, int maxChars) {
  const char* src = (label && label[0]) ? label : "--";
  int len = (int)strlen(src);
  if (len <= maxChars) {
    strlcpy(l1, src, l1cap);
    l2[0] = 0;
    return;
  }
  int splitAt = -1;
  for (int i = maxChars; i >= 0; i--) {
    if (src[i] == ' ') { splitAt = i; break; }
  }
  if (splitAt <= 0) {
    truncateDots(src, l1, l1cap, maxChars);
    l2[0] = 0;
    return;
  }
  int keep = splitAt;
  if (keep >= (int)l1cap) keep = (int)l1cap - 1;
  memcpy(l1, src, keep);
  l1[keep] = 0;
  truncateDots(src + splitAt + 1, l2, l2cap, maxChars);
}

// Card 1: the real model name, shown whole (word-wrapped across 2 lines)
// rather than hard-truncated to 8 chars. Started as the SAME skeleton
// every other quota card on this device uses (label top-left/value
// top-right/bar/countdown row) per live feedback ("make sure the two
// cards layout matches in all screens... similar position of cards and
// text") -- but the bar/countdown row there were always a duplicate of
// card 2's own pctModel/rModel (no second metric exists for card 1 to
// show), and per later live feedback ("in place of the top bar replace
// w/ the model name .. the width shud help showing full") both the
// duplicate bar AND the duplicate 8-char-truncated value in the header
// row are gone -- the name now gets the whole card body (2 lines at
// size2, word-wrapped) instead of appearing twice, truncated two
// different ways. The countdown row is also dropped (redundant with card
// 2's own, and there's no metric here to attach a countdown to) -- left
// as blank space rather than shrinking the card, so card height (h=82,
// shared by every quota page's card geometry) doesn't need to change.
static void drawAntigravityLabelCard(Arduino_GFX* gfx, int top, const char* label,
                                      bool full) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 12);
  gfx->print("Model");

  // Name body: word-wrapped across 2 lines, fixed at size2 -- never
  // auto-shrunk below it (gfxFitSize would floor to an unreadable size1
  // for a long name, already rejected once on the weather page for
  // exactly this readability tradeoff, not repeating it here).
  // Variable-length text on every draw (not just `full`) -- same
  // "GemiGPT" ghosting risk this card was already fixed for once, so
  // both lines are cleared unconditionally before printing, every draw,
  // regardless of whether the new name needs both lines.
  const int nameMaxW = w - 28;              // x+14 .. x+w-14, same margin as every other row
  const int nameMaxChars = nameMaxW / 12;    // size2 glyph width = 6px * 2
  gfx->fillRect(x + 14, top + 44, nameMaxW, 34, C_PANEL);
  char line1[20], line2[20];
  wrapModelName(label, line1, sizeof(line1), line2, sizeof(line2), nameMaxChars);
  gfx->setTextColor(C_WHITE, C_PANEL);
  gfx->setCursor(x + 14, top + 44);
  gfx->print(line1);
  if (line2[0]) {
    gfx->setCursor(x + 14, top + 62);
    gfx->print(line2);
  }

  // Countdown row intentionally left blank (see header comment) -- no
  // draw call here, just unused card space, same h=82 as every other card.
}

// Card 2: big %, fill bar, reset countdown -- same shape as
// CodexMode.cpp's drawCodexMeter().
static void drawAntigravityMeter(Arduino_GFX* gfx, int top,
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
    gfx->print("Quota");
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
  // top+66, not +64 -- see UsageMode.cpp's drawMeter() comment
  gfx->setCursor(x + 14, top + 66);
  gfx->print(line);
}

static void drawAntigravityPage(Arduino_GFX* gfx, const AntigravityData& a, bool full, bool growRight) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: logo + short "agy" label (the CLI's own name, not the full
    // "Antigravity" product name). y=12, not the mascot's own blit y=4 --
    // UsageMode.cpp's header mascot is blitted at y=4 but its idle-pose
    // sprite has 4 blank leading grid rows (cellPx=2, so 8px of real
    // headroom before the ink starts) -- this icon is bbox-cropped, ink
    // starts at row 0, so matching the mascot's *visible* margin means
    // y=4+8=12. Per live feedback ("the claude mascot has some head room
    // from the screen edge... apply that to all logos"), superseding an
    // earlier y=8 chosen for text-baseline centering.
    gfx->drawBitmap(6, 12, kAntigravityIcon, ANTIGRAVITY_ICON_SIZE, ANTIGRAVITY_ICON_SIZE, C_SKY);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 16);  // y=16 centers the text against the y=12 icon
    gfx->print("agy");
  }

  // Two cards, same top=50/138 stacking Codex/z.ai use for their two
  // windows. Card 1: model code. Card 2: percentage/bar/reset.
  drawAntigravityLabelCard(gfx, 50, a.hasLabel ? a.label : "", full);
  drawAntigravityMeter(gfx, 138, a.hasPctModel, a.pctModel, a.hasRModel, a.rModel, full, growRight);
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
