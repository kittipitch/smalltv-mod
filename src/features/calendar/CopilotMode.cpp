#include "CopilotMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "CopilotLogo.h"

CopilotMode g_copilotMode;

#define C_DIM     0xB574
#define C_UGREEN  0x7C6B
#define C_YELLOW  0xFE01
#define C_ACCENT  0xDBAA
#define C_PANEL   0x18E3
#define C_BARBG   0x2945

static uint16_t pctColor(int pct) {
  if (pct >= 90) return C_RED;
  if (pct >= 75) return C_ACCENT;
  if (pct >= 50) return C_YELLOW;
  return C_UGREEN;
}

static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %2dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

static void drawCopilotMeter(Arduino_GFX* gfx, int top, const char* label,
                              bool has, int pct, bool hasReset, int resetMins,
                              bool full, bool growRight) {
  const int x = 8, w = 224, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  char pc[8];
  int pv = constrain(pct, 0, 100);
  if (has) snprintf(pc, sizeof(pc), "%2d%%", min(pv, 99));
  else     strlcpy(pc, " --", sizeof(pc));
  uint8_t sz = gfxFitSize(pc, 150, 5);
  int pcw = gfxTextW(pc, sz);
  gfx->setTextSize(sz);
  gfx->setTextColor((has && pv >= 100) ? C_RED : C_WHITE, C_PANEL);
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
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(pct));

  char rs[16], line[10 + sizeof(rs) + 1];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  {
    char tmp[10 + sizeof(rs)];
    snprintf(tmp, sizeof(tmp), "Resets in %s", rs);
    snprintf(line, sizeof(line), "%17s", tmp);
  }
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + w - 14 - gfxTextW(line, 2), top + 66);
  gfx->print(line);
}

static void drawCopilotClockOverlay(uint32_t& nextRedrawMs, int& lastMinute) {
  struct tm t;
  if (!clockNow(t)) return;
  int curMinute = t.tm_hour * 60 + t.tm_min;
  if (curMinute == lastMinute && (int32_t)(millis() - nextRedrawMs) < 0) return;
  nextRedrawMs = millis() + 30000;
  lastMinute = curMinute;

  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  const char digits[4] = { buf[0], buf[1], buf[3], buf[4] };
  const int cardW = 13, cardH = 22, gapIn = 1, gapPair = 3;
  const int totalW = cardW * 4 + gapIn * 2 + gapPair;
  const int x0 = 8 + 224 - totalW, y = 14;
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

// The header logo: a pixel-art grid (CopilotLogo.h) scaled to exactly w x h pixels, nearest
// neighbour, so cells are 2 or 3 px. RGB bytes come out of PROGMEM with pgm_read_byte only; an
// all-zero triple is transparent (the screen is black there). Near-copy of AntigravityMode.cpp's
// drawAgyLogo, per the file-by-file convention of these mode sources.
static void drawCopilotLogo(Arduino_GFX* gfx, int x0, int y0, int w, int h) {
  for (int r = 0; r < COPILOT_LOGO_ROWS; r++) {
    for (int c = 0; c < COPILOT_LOGO_COLS; c++) {
      const int i = (r * COPILOT_LOGO_COLS + c) * 3;
      const uint8_t R = pgm_read_byte(&kCOPILOT_LOGO[i]);
      const uint8_t G = pgm_read_byte(&kCOPILOT_LOGO[i + 1]);
      const uint8_t B = pgm_read_byte(&kCOPILOT_LOGO[i + 2]);
      if (!(R | G | B)) continue;
      const int cx = x0 + c * w / COPILOT_LOGO_COLS, cy = y0 + r * h / COPILOT_LOGO_ROWS;
      gfx->fillRect(cx, cy, x0 + (c + 1) * w / COPILOT_LOGO_COLS - cx, y0 + (r + 1) * h / COPILOT_LOGO_ROWS - cy,
                    (uint16_t)(((R & 0xF8) << 8) | ((G & 0xFC) << 3) | (B >> 3)));
    }
  }
}

// Half-width card for the split bottom row (Chat | Code) when the daemon sent a Premium reading:
// same look as the whole card, stacked in a 110 px column with 14 px margins -- label, size-4
// right-aligned value, bar, right-aligned bare reset. Near-copy of AntigravityMode.cpp's
// drawAntigravityHalf, per the file-by-file convention of these mode sources.
static void drawCopilotHalf(Arduino_GFX* gfx, int x, int top, const char* label,
                            bool has, int pct, bool hasReset, int resetMins,
                            bool full, bool growRight) {
  const int w = 110, h = 82;
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  gfx->setCursor(x + 14, top + 6);
  gfx->print(label);

  int dispPct = has ? (pct > 99 ? 99 : (pct < 0 ? 0 : pct)) : 0;
  char pc[8];
  if (has) snprintf(pc, sizeof(pc), "%2d%%", dispPct);
  else     strlcpy(pc, " --", sizeof(pc));
  gfx->setTextSize(4);
  gfx->setTextColor((has && pct >= 100) ? C_RED : C_WHITE, C_PANEL);
  gfx->setCursor(x + w - gfxTextW(pc, 4) - 14, top + 24);
  gfx->print(pc);

  int bx = x + 14, by = top + 56, bw = w - 28, bh = 8;
  gfx->fillRoundRect(bx, by, bw, bh, bh / 2, C_BARBG);
  int fw = has ? (int)(bw * constrain(pct, 0, 100) / 100.0f) : 0;
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, pctColor(pct));

  char rs[16];
  if (hasReset) fmtReset(resetMins, rs, sizeof(rs));
  else          strlcpy(rs, "--", sizeof(rs));
  char rsPad[16];
  snprintf(rsPad, sizeof(rsPad), "%7s", rs);
  gfx->setTextSize(2);
  gfx->setCursor(x + w - 14 - gfxTextW(rsPad, 2), top + 66);
  gfx->print(rsPad);
}

static void drawCopilotPage(Arduino_GFX* gfx, const CopilotData& c,
                            bool full, bool growRight) {
  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: the Copilot helmet logo (owner 2026-10-25, thin-line goggles via the pixel-logo-convert
    // skill: per-TARGET-pixel majority, 25x28) + the wordmark. Aspect-true in the old icons' ink
    // box, top-aligned at (7,12); the wordmark moves right of it and still clears the clock (it
    // ends at x=168, the clock overlay starts at x=173).
    drawCopilotLogo(gfx, 7, 12, 25, 28);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(46, 16);
    gfx->print("Copilot");
  }
  // With a Premium reading (the plan's real paid-request allowance, live 2026-10-25 the only
  // snapshot with usage): Premium whole card on top, Chat | Code halves below -- the agy page's
  // Argon layout. Without it (old daemon), the original two whole cards. A layout change blanks
  // the bottom row first: the whole card's panel would otherwise leave colour in the 4 px gap.
  const bool split = c.hasPctPremium;
  static int8_t s_lastSplit = -1;
  const bool cardsFull = full || (s_lastSplit != (int8_t)split);
  s_lastSplit = (int8_t)split;
  if (split) {
    drawCopilotMeter(gfx, 50, "Premium", c.hasPctPremium, c.pctPremium,
                     c.hasPctPremium && c.hasRPremium, c.rPremium, cardsFull, growRight);
    if (cardsFull) gfx->fillRect(8, 138, 224, 82, C_BLACK);
    drawCopilotHalf(gfx, 8, 138, "Chat", c.hasPctChat, c.pctChat,
                    c.hasPctChat && c.hasRChat, c.rChat, cardsFull, growRight);
    drawCopilotHalf(gfx, 122, 138, "Code", c.hasPctCompletions, c.pctCompletions,
                    c.hasPctCompletions && c.hasRCompletions, c.rCompletions, cardsFull, growRight);
  } else {
    drawCopilotMeter(gfx, 50, "Chat", c.hasPctChat, c.pctChat,
                     c.hasPctChat && c.hasRChat, c.rChat, cardsFull, growRight);
    drawCopilotMeter(gfx, 138, "Code", c.hasPctCompletions, c.pctCompletions,
                     c.hasPctCompletions && c.hasRCompletions, c.rCompletions, cardsFull, growRight);
  }
}

void CopilotMode::begin(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  copilotRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CopilotMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  copilotRenderedOk_ = 0xFFFFFFFF;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CopilotMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  clockNextRedrawMs_ = 0;
  clockLastMinute_ = -1;
}

void CopilotMode::service(const Settings& s) {
  const CopilotData& c = copilotGet();
  if (c.lastOkMs != copilotRenderedOk_) {
    copilotRenderedOk_ = c.lastOkMs;
    needRender_ = true;
  }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawCopilotPage(gfx, c, needFullRender_, s.usage.barGrowRight);
    needRender_ = false;
    needFullRender_ = false;
  }
  drawCopilotClockOverlay(clockNextRedrawMs_, clockLastMinute_);
}
