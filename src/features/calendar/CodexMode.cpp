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
  if (pct >= 75) return C_ACCENT;
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

// Free rate-limit reset-credit count/expiry -- found live in the
// app-server RPC's `rateLimitResetCredits` field (undocumented, not part
// of the primary/secondary rate-limit windows drawCodexMeter shows). Used
// to be its own full card; now that this account's `secondary` window is
// populated (a real 5h window alongside the 7d one), the 5h+7d pair takes
// both card slots like every other quota page, so this collapses into a
// small colored line tucked into the 7d card's own dead space -- the gap
// between the "7d" label row (top+12, ends ~top+28) and the bar row
// (by=top+52) has ~24px going spare (per live feedback, "there is a
// space enoght under 7d on the bottom card"). Drawn at top+34, size2
// (16px tall), clearing the gap on both sides with room to spare.
//
// Format: "(N)" when N credits are available but the soonest expiry is
// unknown, "(N - Dd)" when it is -- e.g. "(1 - 23d)" means 1 free reset
// available, the soonest one expiring in ~23 days. Blank (padded, not
// omitted -- see below) when there are none. Days = round(minutes/1440),
// clamped at 0 so a just-expired/stale value never prints negative.
//
// Colored by urgency same idea as UsageMode.cpp's/pctColor()'s bands but
// keyed on days-until-expiry instead of %-used: plenty of runway (>=14d)
// is green, a couple weeks out (3-13d) is yellow, about to lapse (<3d) is
// red. A credit with no known expiry is shown green (available, no
// urgency signal yet) rather than left uncolored.
static uint16_t creditColor(int days) {
  if (days < 3)  return C_RED;
  if (days < 14) return C_YELLOW;
  return C_UGREEN;
}

// Always drawn (not gated on `full`) so a data-only push (the common
// case) still updates it -- same reasoning as drawCodexMeter's value row.
// Fixed-width padded to 12 chars regardless of content so a transition
// from a long "(N - Dd)" down to nothing (or a shorter form) can't leave
// stale pixels behind, matching this file's "Resets in %-7s" convention.
//
// textSize 1, not 2 -- an Opus/Fable QC pass caught a real collision: the
// same card's big "%" value (drawCodexMeter, size5, right-aligned) has
// its cursor at x = 8+224-gfxTextW(pc,5)-14 = 98 (pc is always exactly 4
// chars, "%3d%%"/"  --", so this is constant), with ink extending down to
// roughly this row's y. At size2 a 14-char opaque pad (168px from x=22)
// crosses x=98 and wipes the bottom of the value's digits on every
// render. At size1 (6px/char) a 12-char pad ends at x=22+72=94, clearing
// x=98 with margin -- credits/days are also clamped to 2 digits (not 3)
// so even an unpadded worst case ("(99 - 99d)", 11 chars = 66px) stays
// well inside that margin.
static void drawCodexResetSuffix(Arduino_GFX* gfx, int top, bool hasCredits,
                                  int credits, bool hasExpire, int expireMins) {
  char content[16] = "";
  uint16_t color = C_DIM;
  if (hasCredits && credits > 0) {
    if (hasExpire) {
      int days = (int)((expireMins / 1440.0f) + 0.5f);
      if (days < 0) days = 0;
      snprintf(content, sizeof(content), "(%d - %dd)", constrain(credits, 0, 99), min(days, 99));
      color = creditColor(days);
    } else {
      snprintf(content, sizeof(content), "(%d)", constrain(credits, 0, 99));
      color = C_UGREEN;
    }
  }
  char line[16];
  snprintf(line, sizeof(line), "%-12s", content);
  gfx->setTextSize(1);
  gfx->setTextColor(color, C_PANEL);
  gfx->setCursor(8 + 14, top + 34);
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

  // Two fixed cards, "5h" and "7d" -- matching UsageMode.cpp's/Claude
  // page's own card pair now that this account's `secondary` window is
  // actually populated (it used to be null on every poll, which is why
  // this used to spend the second card slot on the free-reset-credit
  // meter instead -- see git history/CodexMode.cpp comments predating
  // this). The free-reset-credit info didn't get dropped, it moved into
  // a small colored line inside the 7d card's own dead space -- see
  // drawCodexResetSuffix() above.
  //
  // Both cards drawn UNCONDITIONALLY, matching UsageMode.cpp/ZaiMode.cpp's
  // pattern where every card always draws and internally falls back to
  // "--" when its own `has*` flag is false -- a conditional `if (c.has...)`
  // call had a real bug here before: on a DATA-ONLY render (mode switch
  // renders once before the daemon's push arrives, push lands as a
  // separate tick with needFullRender_ already spent), the flag flipping
  // false->true would skip the panel-fill/label (both gated on `full`)
  // forever until the next genuine structural redraw.
  drawCodexMeter(gfx, 50, "5h", c.hasPct5h, c.pct5h, c.hasR5h, c.r5h, full, growRight);
  drawCodexMeter(gfx, 138, "7d", c.hasPctWeek, c.pctWeek, c.hasRWeek, c.rWeek, full, growRight);
  drawCodexResetSuffix(gfx, 138, c.hasResetCredits, c.resetCredits,
                        c.hasResetCreditExpireMins, c.resetCreditExpireMins);
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
