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

// Two stacked cards, same shape/spacing as Codex/z.ai's two-window layout
// (top=50/138, 82px tall each). Per live feedback ("ok let's bring back 2
// bars... remove the line that says Model and replace it w/ model
// names.. we will show pro and flash % and re[set] time") this is now two
// REAL metrics, not one number shown twice: this account's real model
// configs (confirmed live via `agy models`) split into a Gemini Pro
// family (gemini-3.1-pro-*) and a Gemini Flash family
// (gemini-3.5/3.6-flash-*) -- see clawdmeter_daemon.py's poll_antigravity()
// for the per-family "pick tightest variant" logic. Non-Gemini models this
// account also has (Claude Sonnet/Opus, GPT-OSS) aren't shown here, they
// already have their own dedicated pages on this device. Identical shape
// to drawZaiMeter()/drawCodexMeter() -- label top-left (the daemon's
// "<version> <family>" string, e.g. "3.6 Flash", replacing the generic
// "Model"/"Quota" captions two earlier designs used here), big value
// top-right, bar, countdown row.
//
// The value is fixed at size4, not the size5 every other quota page's
// card uses -- a deliberate, page-scoped exception (tried size3 first,
// per live feedback ("can we use size 4")). A naive size4 with the old
// 3-digit "%3d%%" format would NOT have fit the full label (only ~8
// chars of room, "3.6 Flash" needs 9) -- what actually makes size4 work
// is capping the DISPLAYED value at 2 digits (see the `dispPct` comment
// below, per live feedback "the percentage is 2 number max"), which
// gives back the exact width size3 had. Card x/w/h/top positions are
// UNCHANGED from every other quota page (top=50/138, h=82) throughout
// every revision of this card -- only the value's own font size and
// digit-width vary.
static void drawAntigravityMeter(Arduino_GFX* gfx, int top, const char* label,
                                  bool has, int pct, bool hasReset, int resetMins,
                                  bool full, bool growRight) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  // Displayed value is capped at 2 digits (99% max), NOT the real `pct`
  // used for the bar fill/color below -- per explicit live feedback
  // ("the percentage is 2 number max"). This is a deliberate display-only
  // clamp (a common "don't print 100% until truly done" UI convention),
  // not a data-accuracy compromise: the bar itself, and pctColor()'s red
  // threshold, still use the real unclamped `pct`, so a genuinely
  // exhausted quota still reads as a full red bar even though the number
  // reads "99%" instead of "100%". This is what makes size4 (not size3)
  // fit the full label: a fixed 3-char "NN%"/" --" is 72px at size4 --
  // identical width to a fixed 4-char "%3d%%" at size3 -- so this trades
  // one digit of display precision at the true-100% edge case for a
  // visibly bigger value everywhere else.
  int dispPct = has ? (pct > 99 ? 99 : (pct < 0 ? 0 : pct)) : 0;
  char pc[8];
  if (has) snprintf(pc, sizeof(pc), "%2d%%", dispPct);
  else     strlcpy(pc, " --", sizeof(pc));
  uint8_t sz = gfxFitSize(pc, 150, 4);
  int pcw = gfxTextW(pc, sz);
  gfx->setTextSize(sz);
  // Red ONLY at the true-100% edge case ("99%" would otherwise be
  // indistinguishable from a real 99), white everywhere else -- per
  // explicit request, this page only. Not a severity threshold (that's
  // what the bar's pctColor() already is) -- this is a correctness flag:
  // red means "the number shown is capped, the real value is higher."
  gfx->setTextColor((has && pct >= 100) ? C_RED : C_WHITE, C_PANEL);
  gfx->setCursor(x + w - pcw - 14, top + 10);
  gfx->print(pc);

  // Label drawn UNCONDITIONALLY (not gated on `full`), opaque, fixed-width
  // padded -- unlike z.ai's "5h"/"MCP" (static, safe to gate on `full`
  // since they never change), this label is the daemon-reported string
  // and CAN change poll to poll (a Flash generation swap, or an old
  // daemon that never sends labelPro/labelFlash at all) -- gating on
  // `full` would leave a stale/blank label on a data-only redraw, the
  // exact single-card-looking bug already found and fixed once on the
  // Codex page (`drawCodexPage()` calling a card function conditionally).
  // Fixed at exactly 10 chars (truncated, then padded) -- NOT simply
  // padded to whatever length the label happens to be. At size4 with the
  // 2-digit value cap above, the value's cursor is x=146
  // (x+w-pcw-14, pcw=72 for a fixed 3-char "NN%"), and this label row
  // starts at x=22 -- 124px/10.3 chars of headroom before the two rows'
  // bounding boxes (which DO overlap vertically: label y=12..28, value
  // y=10..42) start to collide. Real labels seen live are 9 chars
  // ("3.6 Flash"); 10 leaves a small margin for a future double-digit
  // minor version ("3.10 Flash"). truncateDots caps anything longer
  // still.
  char lblRaw[12];
  truncateDots((label && label[0]) ? label : "--", lblRaw, sizeof(lblRaw), 10);
  char lbl[11];
  snprintf(lbl, sizeof(lbl), "%-10s", lblRaw);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 12);
  gfx->print(lbl);

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
  // windows. Card 1: Gemini Pro family. Card 2: Gemini Flash family. Both
  // always drawn unconditionally (not gated on has*) -- same
  // both-cards-always-drawn fix already applied to CodexMode.cpp.
  drawAntigravityMeter(gfx, 50,  a.hasLabelPro ? a.labelPro : "Pro",
                        a.hasPctPro, a.pctPro, a.hasRPro, a.rPro, full, growRight);
  drawAntigravityMeter(gfx, 138, a.hasLabelFlash ? a.labelFlash : "Flash",
                        a.hasPctFlash, a.pctFlash, a.hasRFlash, a.rFlash, full, growRight);
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
