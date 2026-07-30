#include "CodexMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "CodexIcon.h"

CodexMode g_codexMode;

// Same palette subset ZaiMode.cpp uses -- not shared/exported anywhere yet,
// each mode defines what it needs.
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_UGREEN  0x7C6B   // sage green -- comfortable usage
#define C_YELLOW  0xFE01   // caution yellow ~#FFC107 -- same value CalendarMode.cpp's
                            // C_STORM uses, already proven distinguishable from C_ACCENT
                            // on this screen
#define C_ACCENT  0xDBAA   // terra-cotta -- getting close
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use
#define C_BARBG   0x2945   // unfilled bar track
// C_RED comes from Gfx.h (shared across modes already, see UsageMode.cpp)

// Same threshold philosophy as UsageMode.cpp's barColor()/ZaiMode.cpp's
// pctColor() -- green under 50%, yellow climbing, terra-cotta approaching
// the cap, red once actually tight. Matches the statusline script's own
// green/yellow/orange/red bands.
static uint16_t pctColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 70) return C_ACCENT;
  if (pct >= 50) return C_YELLOW;
  return C_UGREEN;
}

// Identical to UsageMode.cpp's/ZaiMode.cpp's fmtReset() -- kept as a
// near-copy rather than a shared helper, same reasoning as
// drawCodexClockOverlay below.
static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %2dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

// Same card shape as UsageMode.cpp's drawMeter()/ZaiMode.cpp's
// drawZaiMeter() -- big %, label, fill bar, reset countdown. `full` gates
// the label + panel background exactly like those (an Opus review caught an
// unconditional-fill bug in the original UsageMode version; same reasoning
// applies here).
static void drawCodexMeter(Arduino_GFX* gfx, int top, const char* label,
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

  // Row is always drawn, same as UsageMode.cpp's/ZaiMode.cpp's meter --
  // when hasReset is false (a window that isn't populated on this
  // account/plan tier, e.g. secondary was null on the account this was
  // tested against), it prints "--" rather than a fake "now", but still
  // occupies the row so nothing else needs to clear it.
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

// Card 2: free rate-limit reset credits -- found live in the app-server
// RPC's `rateLimitResetCredits` field (undocumented, not part of the
// primary/secondary rate-limit windows drawCodexMeter shows). The point of
// this card is to use a credit before it lapses unused, so the bottom row
// is a countdown, not just a static count. Structurally IDENTICAL to
// drawCodexMeter -- same label/value/bar/countdown positions -- per live
// feedback ("the cards are ugly make sure they use the same layout as
// claude and z.ai page").
//
// Bar semantics, per explicit live feedback ("the bottom bar for the
// reset it shud be devided in 7 days and then the count down time shud
// reflect the remaining time... green is good (7 days or more), red is
// almost expire"): fill = remaining time / a fixed 7-day window, capped
// at 100% when 7+ days remain -- FULL bar = safe, EMPTY bar = about to
// expire. This replaced an earlier version whose bar filled based on %
// of the credit's *actual* granted-to-expiry lifespan (which can be
// ~30 days on this account, per the real RPC data -- see poll_codex()),
// an unintuitive scale that didn't match "7 days" the way the user
// described it. Computed entirely device-side from resetCreditExpireMins
// (already pushed) -- no daemon involvement needed for this metric,
// unlike the elapsed-fraction version it replaced.
#define CODEX_RESET_WINDOW_MINS (7 * 24 * 60)
static void drawCodexResetCard(Arduino_GFX* gfx, int top, bool hasCredits, int credits,
                                bool hasExpire, int expireMins, bool full, bool growRight) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  // Label drawn UNCONDITIONALLY (not gated on `full`), unlike
  // drawCodexMeter's static "Week" label -- "Reset" (5 chars) at x+14
  // overlaps the value's cursor position (x=128 when the value is
  // "%3d", 3 chars, sized up to size5 by gfxFitSize) at x=130..142.
  // Drawing the value first (opaque, C_PANEL bg) then the label
  // (opaque, same bg) after it, every time, means the label always wins
  // the overlap instead of getting silently clipped by the next
  // data-only value redraw -- same fix AntigravityMode.cpp's label card
  // uses for the same reason.
  //
  // "Reset" -- shortened from longer wording the same row's width can't
  // fit alongside the big value (tried "Full reset", the ChatGPT/Codex
  // UI's own real label per a live screenshot of its "Usage" panel; then
  // "Available reset" per direct feedback; both run past the value's
  // leftmost possible cursor position at any digit count -- 224px card
  // width doesn't have room for either next to a size5 number). User's
  // own call once shown the pixel math: "say just 'reset'".
  char cv[8];
  if (hasCredits) snprintf(cv, sizeof(cv), "%3d", constrain(credits, 0, 999));
  else            strlcpy(cv, " --", sizeof(cv));
  uint8_t sz = gfxFitSize(cv, 150, 5);
  int cvw = gfxTextW(cv, sz);
  gfx->setTextSize(sz);
  gfx->setTextColor(C_WHITE, C_PANEL);
  gfx->setCursor(x + w - cvw - 14, top + 10);
  gfx->print(cv);

  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 12);
  gfx->print("Reset");

  int bx = x + 14, by = top + 52, bw = w - 28, bh = 12;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  bool hasUrgency = hasCredits && credits > 0 && hasExpire;
  float remainFrac = hasUrgency
    ? constrain((float)expireMins / CODEX_RESET_WINDOW_MINS, 0.0f, 1.0f)
    : 0.0f;
  int fw = hasUrgency ? (int)(bw * remainFrac) : 0;
  int fx = growRight ? (bx + bw - fw) : bx;
  // pctColor() expects "% used/urgent", not "% remaining" -- invert so a
  // credit close to expiring (small remainFrac) still lands in the same
  // red/amber/green bands every other quota bar on the device uses.
  int urgencyPct = hasUrgency ? (int)((1.0f - remainFrac) * 100.0f + 0.5f) : 0;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(urgencyPct));

  // "Expires", not "Resets in" -- per live feedback ("the expiry date
  // of free reset it shud say expire or sth") -- this is a one-shot
  // credit lapsing unused, not a recurring window resetting, so the
  // wording needs to say which. Dropped the "in" (vs. a first attempt,
  // "Expires in") so this row's cursor can stay at the same x+14 as
  // drawCodexMeter's "Resets in" row -- "Expires in %-7s" was 18 chars,
  // one more than "Resets in"'s 17, which ran 6px past the card's right
  // edge at x+14; shifting the cursor to x+8 to compensate fixed the
  // overflow but broke left-edge alignment between the two cards'
  // bottom rows, caught live ("why the word resets in / expires dont
  // line up"). Same "always draw, print -- when absent" convention as
  // drawCodexMeter's row -- also covers credits==0 (a real "none
  // available" state, distinct from "daemon hasn't sent this field
  // yet").
  char rs[16], line[8 + sizeof(rs) + 1];
  if (hasCredits && credits > 0 && hasExpire) fmtReset(expireMins, rs, sizeof(rs));
  else                                         strlcpy(rs, "--", sizeof(rs));
  snprintf(line, sizeof(line), "Expires %-7s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  // top+66, not +64 -- see UsageMode.cpp's drawMeter() comment
  gfx->setCursor(x + 14, top + 66);
  gfx->print(line);
}

static void drawCodexPage(Arduino_GFX* gfx, const CodexData& c, bool full, bool growRight) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: real logo (user-supplied Codex CLI app icon, see
    // CodexIcon.h for the rasterization), same 32x32-at-y=12 position
    // AntigravityMode.cpp's icon uses, drawn in C_CODEX_PURPLE (sampled
    // from the source image's own fill) per live feedback ("we can use
    // that purple color but make it mono"). x=56 text start leaves room
    // for the icon, same as "agy"'s header.
    // y=12, not the mascot's own blit y=4 -- UsageMode.cpp's header
    // mascot is blitted at y=4 but its idle-pose sprite has 4 blank
    // leading grid rows (cellPx=2, so 8px of real headroom before the
    // ink starts) -- this icon is bbox-cropped, ink starts at row 0, so
    // matching the mascot's *visible* margin means y=4+8=12. Per live
    // feedback ("the claude mascot has some head room from the screen
    // edge... apply that to all logos"). Every header icon on every
    // quota page now shares this exact visible margin.
    gfx->drawBitmap(6, 12, kCodexIcon, CODEX_ICON_SIZE, CODEX_ICON_SIZE, C_CODEX_PURPLE);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 16);  // y=16 centers the text against the y=12 icon
    gfx->print("Codex");
  }

  // Two fixed cards, per live feedback ("top is weekly quota and reset;
  // bottom is remaining reset and expiration") -- card 1 the weekly
  // quota/reset meter, card 2 the free-reset-credit meter, replacing an
  // earlier layout that reserved this space for a "5h" window (this
  // account's `secondary` has been null every single poll this whole
  // session -- confirmed live in CalendarData.h's CodexData comment) plus
  // a cramped bottom-margin text line for the credit info. `pct5h`/`r5h`
  // are still received and stored on CodexData (see CalendarClient.cpp)
  // in case a real 5h-window account is found later, they're just not
  // drawn -- both fixed card slots are now spoken for.
  // "7d", not "Week" -- per live feedback ("we shud say that to be
  // consistent w/ claude page"), matching UsageMode.cpp's "5h"/"7d"
  // card-label convention for the same real window length (10080
  // minutes = 7 days).
  //
  // Both cards drawn UNCONDITIONALLY now (was `if (c.hasPctWeek)
  // drawCodexMeter(...)`), matching UsageMode.cpp/ZaiMode.cpp's pattern
  // where every card always draws and internally falls back to "--" when
  // its own `has*` flag is false. The old conditional-call pattern had a
  // real bug: when hasPctWeek flipped false->true on a DATA-ONLY render
  // (the common case -- a mode switch renders once before the daemon's
  // push arrives, then the push lands as a separate render tick with
  // needFullRender_ already spent), card 1's very first appearance would
  // skip drawCodexMeter's own panel-fill and label (both gated on
  // `full`) forever, until the next genuine structural redraw happened
  // to come along -- confirmed live: a fresh mode switch showed only
  // card 2, card 1 entirely missing its panel and "7d" label. Per live
  // feedback ("we always draw both cards too here") -- z.ai never had
  // this bug because it always called drawZaiMeter unconditionally.
  drawCodexMeter(gfx, 50, "7d", c.hasPctWeek, c.pctWeek, c.hasRWeek, c.rWeek, full, growRight);
  drawCodexResetCard(gfx, 138, c.hasResetCredits, c.resetCredits,
                      c.hasResetCreditExpireMins, c.resetCreditExpireMins, full, growRight);
}

// Same flip-clock overlay as UsageMode.cpp/ZaiMode.cpp -- per the same
// "any quota-style page gets the clock" reasoning. Kept as a near-identical
// copy rather than a shared helper: each mode owns its own render/dirty
// state per Mode.h's design.
static void drawCodexClockOverlay(uint32_t& nextRedrawMs, int& lastMinute) {
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

void CodexMode::begin(const Settings& s) {
  // calendarInit(s) is deliberately NOT called here -- same reasoning as
  // ZaiMode::begin(): it would re-clear g_cal/g_weather too (shared across
  // all CalendarClient consumers), and another mode's begin() already calls
  // it once. g_codex is a file-scope static, zero-initialized at boot.
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  codexRenderedOk_ = 0xFFFFFFFF;
  // Force the clock to repaint on the very next service() tick -- otherwise
  // a structural redraw (fillScreen) wipes it and it stays blank for up to
  // 30s, since its own timer/lastMinute state doesn't know a wipe happened.
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CodexMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  codexRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CodexMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CodexMode::service(const Settings& s) {
  const CodexData& c = codexGet();
  if (c.lastOkMs != codexRenderedOk_) { codexRenderedOk_ = c.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawCodexPage(gfx, c, needFullRender_, s.usage.barGrowRight);
    needRender_ = false;
    needFullRender_ = false;
  }
  drawCodexClockOverlay(clockNextRedrawMs_, clockLastMinute_);
}
