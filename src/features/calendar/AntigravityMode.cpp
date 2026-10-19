#include "AntigravityMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "AgyLogo.h"

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
static void drawAntigravityMeter(Arduino_GFX* gfx, int top, const char* label, bool big,
                                  bool has, int pct, bool hasReset, int resetMins,
                                  bool full, bool growRight, bool resetPrefix) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

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
  // `big` = the size-5 value the Claude page uses; otherwise the original size 4. The page
  // decides it once for BOTH cards (see drawAntigravityPage) so a card can never flip size
  // between pushes and strand digits of the other size. This label block now runs BEFORE
  // the value is drawn: at size 5 the value starts at x=128, inside this label's 10-char
  // padding (to x=142), and the value's opaque background must win that overlap. At size 4
  // (x=146) the two never touched, so legacy labels render exactly as before.
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 12);
  gfx->print(lbl);

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
  uint8_t sz = gfxFitSize(pc, 150, big ? 5 : 4);
  int pcw = gfxTextW(pc, sz);
  gfx->setTextSize(sz);
  // Red ONLY at the true-100% edge case ("99%" would otherwise be
  // indistinguishable from a real 99), white everywhere else -- first
  // asked for this page only, since 2026-09-12 the rule on every quota
  // page (see UsageMode.cpp drawMeter()). Not a severity threshold (that's
  // what the bar's pctColor() already is) -- this is a correctness flag:
  // red means "the number shown is capped, the real value is higher."
  gfx->setTextColor((has && pct >= 100) ? C_RED : C_WHITE, C_PANEL);
  gfx->setCursor(x + w - pcw - 14, top + 10);
  gfx->print(pc);

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
  // still occupies the row so nothing else needs to clear it. Owner
  // (2026-10-19): "Resets in" on the TOP card only; every other card on
  // this page shows the bare duration ("5d 18h"). Both forms are padded
  // to a fixed width: the panel is only refilled under `full`, so a
  // shorter text ("now") must overwrite every pixel of a longer one.
  char rs[16], line[10 + sizeof(rs) + 1];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  if (resetPrefix) {
    // "Resets in 4h 57m" right-aligned in a fixed 17-char field (the longest text, "Resets in 29d 23h"),
    // ending 14 px inside the card -- the same right edge as the big value and the bar (owner
    // 2026-10-19: right-align "Resets in" on every card, every page). The padding blanks on the left
    // overwrite a longer previous text.
    char tmp[10 + sizeof(rs)];
    snprintf(tmp, sizeof(tmp), "Resets in %s", rs);
    snprintf(line, sizeof(line), "%17s", tmp);
  } else {
    // Bare form, right-aligned in a fixed 7-char field ending at the SAME right edge, so its last
    // character sits exactly under the last character of the top card's text (the h under the m).
    snprintf(line, sizeof(line), "%7s", rs);
  }
  // top+66, not +64 -- see UsageMode.cpp's drawMeter() comment
  gfx->setCursor(x + w - 14 - gfxTextW(line, 2), top + 66);
  gfx->print(line);
}

// Half-width card for the bottom row when the account also has an Argon group: 7d on the left,
// Argon on the right. Same look as drawAntigravityMeter() (panel, dim label, white value, bar, bare
// reset) stacked in a 110 px column: label row (window left, model tag right), value (size 4, right
// aligned), bar, reset. The label is
// drawn before the value like the full card, both with opaque panel backgrounds, so a changed
// label or value never leaves old pixels behind.
static void drawAntigravityHalf(Arduino_GFX* gfx, int x, int top, const char* label, const char* tag,
                                bool has, int pct, bool hasReset, int resetMins,
                                bool full, bool growRight) {
  const int w = 110, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  char lblRaw[8];
  truncateDots((label && label[0]) ? label : "--", lblRaw, sizeof(lblRaw), 4);
  char lbl[9];
  snprintf(lbl, sizeof(lbl), "%-4s", lblRaw);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 6);
  gfx->print(lbl);
  // Model tag at the right end of the same row ("7d ... Fl"). Fixed 4 chars, right-aligned, so a
  // changed or removed tag fully overwrites the old one. Drawn after the label with its own opaque
  // background; labels are <= 3 chars ("5h", "7d", "30d"), so its blank padding never clips ink.
  char tagRaw[8], tagPad[8];
  truncateDots((tag && tag[0]) ? tag : "", tagRaw, sizeof(tagRaw), 4);
  snprintf(tagPad, sizeof(tagPad), "%4s", tagRaw);
  gfx->setCursor(x + w - gfxTextW(tagPad, 2) - 14, top + 6);
  gfx->print(tagPad);

  // Same display rule as the full card: at most 2 digits ("99%"), red only at a true 100%, a fixed
  // 3-char "NN%" / " --" so a shorter value fully overwrites a longer one, and right-aligned to the
  // card's inner edge like the whole cards' value (owner 2026-10-19: "the number should align right").
  int dispPct = has ? (pct > 99 ? 99 : (pct < 0 ? 0 : pct)) : 0;
  char pc[8];
  if (has) snprintf(pc, sizeof(pc), "%2d%%", dispPct);
  else     strlcpy(pc, " --", sizeof(pc));
  gfx->setTextSize(4);
  gfx->setTextColor((has && pct >= 100) ? C_RED : C_WHITE, C_PANEL);
  gfx->setCursor(x + w - gfxTextW(pc, 4) - 14, top + 24);
  gfx->print(pc);

  int bx = x + 14, by = top + 56, bw = w - 28, bh = 8;  // 14 px margins, like the whole cards
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  int fw = has ? (int)(bw * constrain(pct, 0, 100) / 100.0f) : 0;
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(pct));

  char rs[16];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  char rsPad[16];  // rs is up to 15 chars; sized so the compiler can prove no truncation
  snprintf(rsPad, sizeof(rsPad), "%7s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  // Right-aligned like the whole card's bare row: the same 14 px right margin in BOTH half cards, which
  // puts the right card's last character under the top card's last one (owner 2026-10-19).
  gfx->setCursor(x + w - 14 - gfxTextW(rsPad, 2), top + 66);
  gfx->print(rsPad);
}

// The header logo: a pixel-art grid (AgyLogo.h) scaled to exactly w x h pixels, nearest neighbour, so
// cells are 2 or 3 px (no distortion beyond that). RGB bytes come out of PROGMEM with pgm_read_byte
// only; an all-zero triple is transparent (the screen is black there).
static void drawAgyLogo(Arduino_GFX* gfx, int x0, int y0, int w, int h) {
  for (int r = 0; r < AGY_LOGO_ROWS; r++) {
    for (int c = 0; c < AGY_LOGO_COLS; c++) {
      const int i = (r * AGY_LOGO_COLS + c) * 3;
      const uint8_t R = pgm_read_byte(&kAgyLogo[i]);
      const uint8_t G = pgm_read_byte(&kAgyLogo[i + 1]);
      const uint8_t B = pgm_read_byte(&kAgyLogo[i + 2]);
      if (!(R | G | B)) continue;
      const int cx = x0 + c * w / AGY_LOGO_COLS, cy = y0 + r * h / AGY_LOGO_ROWS;
      gfx->fillRect(cx, cy, x0 + (c + 1) * w / AGY_LOGO_COLS - cx, y0 + (r + 1) * h / AGY_LOGO_ROWS - cy,
                    (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)));
    }
  }
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
    // Owner (2026-10-19): the rainbow pixel-art logo, "the same size as the old one". The old 1-bit
    // icon's ink was 30 x 28 px at x 7..36, y 12..39; the logo's 12 x 13 grid fitted to that height is
    // 26 x 28, centred in the old box (x 9..34, y 12..39): same top margin, same height, same bottom.
    drawAgyLogo(gfx, 9, 12, 26, 28);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 16);  // y=16 centers the text against the y=12 icon
    gfx->print("agy");
  }

  // Two cards, same top=50/138 stacking Codex/z.ai use for their two
  // windows. Card 1: Gemini Pro family. Card 2: Gemini Flash family. Both
  // always drawn unconditionally (not gated on has*) -- same
  // both-cards-always-drawn fix already applied to CodexMode.cpp.
  // Value size, decided once per page from what the daemon sent. "Big" (size 5, the Claude
  // page's) only when it sent labels and every one of them is short ("5h"/"7d"): a daemon
  // that sends none, or the old "3.6 Flash" labels, keeps size 4 exactly as before. A card
  // with no label in big mode reads "--" rather than the default "Flash", which would be
  // five characters of size-4 text. The cards' panels are only refilled under `full`, so a
  // change of mode forces a repaint of both (same trick as CodexMode.cpp's s_lastCard1).
  auto shortOrAbsent = [](bool hasLabel, const char* l) { return !hasLabel || strlen(l) <= 4; };
  const bool big = (a.hasLabelPro || a.hasLabelFlash) &&
                   shortOrAbsent(a.hasLabelPro, a.labelPro) && shortOrAbsent(a.hasLabelFlash, a.labelFlash);
  // The bottom row is split into two half cards (7d | Argon) when the daemon sent an Argon reading,
  // and is one whole card otherwise. A change of either layout fact repaints the cards, and the
  // bottom row is blanked first: the whole card's panel would otherwise leave its colour in the
  // 4 px gap between the halves.
  const bool split = a.hasPctArgon;
  static int8_t s_lastBig = -1, s_lastSplit = -1;
  const bool cardsFull = full || (s_lastBig != (int8_t)big) || (s_lastSplit != (int8_t)split);
  s_lastBig = (int8_t)big;
  s_lastSplit = (int8_t)split;
  drawAntigravityMeter(gfx, 50,  a.hasLabelPro ? a.labelPro : (big ? "--" : "Pro"), big,
                        a.hasPctPro, a.pctPro, a.hasPctPro && a.hasRPro, a.rPro, cardsFull, growRight, true);
  if (cardsFull) gfx->fillRect(8, 138, 224, 82, C_BLACK);
  if (!split) {
    drawAntigravityMeter(gfx, 138, a.hasLabelFlash ? a.labelFlash : (big ? "--" : "Flash"), big,
                          a.hasPctFlash, a.pctFlash, a.hasPctFlash && a.hasRFlash, a.rFlash, cardsFull, growRight, false);
  } else {
    drawAntigravityHalf(gfx, 8, 138, a.hasLabelFlash ? a.labelFlash : "--", a.hasTagFlash ? a.tagFlash : "",
                        a.hasPctFlash, a.pctFlash, a.hasPctFlash && a.hasRFlash, a.rFlash, cardsFull, growRight);
    drawAntigravityHalf(gfx, 122, 138, a.hasLabelArgon ? a.labelArgon : "--", a.hasTagArgon ? a.tagArgon : "",
                        a.hasPctArgon, a.pctArgon, a.hasPctArgon && a.hasRArgon, a.rArgon, cardsFull, growRight);
  }
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
