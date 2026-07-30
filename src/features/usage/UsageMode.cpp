#include "UsageMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "UsageClient.h"
#include "Mascot.h"
#include "Clock.h"

UsageMode g_usageMode;

// Claude-usage palette (Anthropic-inspired dark theme, RGB565 of the originals)
#define C_ACCENT  0xDBAA   // terra-cotta 0xd97757 -- orange band
#define C_YELLOW  0xFE01   // caution yellow ~#FFC107 -- same value CalendarMode.cpp's
                            // C_STORM uses, already proven distinguishable from C_ACCENT
                            // on this screen
#define C_UGREEN  0x7C6B   // green 0x788c5d
#define C_PANEL   0x18E3   // card fill 0x1f1f1e
#define C_BARBG   0x2945   // unfilled bar track 0x2a2a28
#define C_DIM     0xB574   // secondary text 0xb0aea5

// Mascot diff state for the flicker-free full-screen idle animation.
static bool            s_mascotPrimed  = false;
static const uint16_t* s_mascotPalette = nullptr;
static uint8_t         s_prevCells[MASCOT_GRID * MASCOT_GRID];

// Copy a mascot palette into a local RAM array using *byte* reads. pgm_read_byte
// is safe from both RAM and flash; a 16-bit load straight from flash (irom) faults
// on the ESP8266, so this never depends on where the palette actually lives.
static void loadPalette(const uint16_t* palette, uint16_t* out) {
  const uint8_t* p = (const uint8_t*)palette;
  for (int k = 0; k < MASCOT_PALETTE_SIZE; k++)
    out[k] = (uint16_t)(pgm_read_byte(p + 2 * k) | (pgm_read_byte(p + 2 * k + 1) << 8));
}

// Draw a 20x20 mascot frame at (x0,y0), cellPx per cell. Reads PROGMEM frame data.
static void blitMascot(Arduino_GFX* gfx, const uint8_t* cells, const uint16_t* palette,
                       int x0, int y0, int cellPx) {
  uint16_t pal[MASCOT_PALETTE_SIZE];
  loadPalette(palette, pal);
  for (int i = 0; i < MASCOT_GRID * MASCOT_GRID; i++) {
    uint8_t code = pgm_read_byte(&cells[i]);
    uint16_t color = (code < MASCOT_PALETTE_SIZE) ? pal[code] : 0;
    int gx = i % MASCOT_GRID, gy = i / MASCOT_GRID;
    gfx->fillRect(x0 + gx * cellPx, y0 + gy * cellPx, cellPx, cellPx, color);
  }
}

static void fmtReset(int mins, char* out, size_t n) {
  if (mins <= 0) { strlcpy(out, "now", n); return; }
  int d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d > 0)      snprintf(out, n, "%dd %dh", d, h);
  else if (h > 0) snprintf(out, n, "%dh %2dm", h, m);
  else            snprintf(out, n, "%dm", m);
}

// Takes the already-rounded displayed percentage, not the raw float -- a
// raw float and the "%3d%%" display can round to different bands right at
// a threshold (e.g. 49.6 displays "50%" but is still < 50 unrounded),
// showing a number/color pair that visually contradicts each other.
static uint16_t barColor(int pctRounded) {
  if (pctRounded >= 90) return C_RED;
  if (pctRounded >= 75) return C_ACCENT;
  if (pctRounded >= 50) return C_YELLOW;
  return C_UGREEN;
}

// One usage card: big %, a 5h/7d label, a fill bar coloured by load, and the
// reset countdown. `top` is the card's top y; the card is 82px tall.
// `label` ("5h"/"7d") never changes for a given card, so it's only drawn on
// `full` (first entry / wake / mode switch). The % number and reset line DO
// change and are redrawn every update — using a fixed-width format plus
// setTextColor(fg,bg) (opaque background fill per glyph), so the new text
// fully overwrites the old in place with no separate clear/flash needed. The
// card panel and bar always redraw their own full background already, so
// they're naturally self-clearing without any extra work.
static void drawMeter(Arduino_GFX* gfx, int top, const char* label,
                      float pct, int resetMins, bool full, bool growRight) {
  const int x = 8, w = 224, h = 82;
  // Only clear+redraw the card background on structural changes. On a plain
  // data update, everything below is self-overwriting (opaque text, self-
  // clearing bar), so skipping this is what actually removes the flash —
  // this was the bug an Opus review caught: leaving this unconditional wiped
  // the label below every partial render, and also meant every push still
  // repainted ~64% of the screen, defeating the point of `full` entirely.
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  // Label left, percentage right, bar fills from the right edge.
  int pctRounded = (int)lroundf(constrain(pct, 0.0f, 100.0f));
  char pc[8];
  snprintf(pc, sizeof(pc), "%3d%%", pctRounded);
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
  int fw = (int)(bw * constrain(pct, 0.0f, 100.0f) / 100.0f);
  // fillRoundRect (not fillRect) even for the small-fill case, so the fill
  // never leaves square corners poking past the track's rounded ends once
  // the card background stops being redrawn every frame to hide them. Uses
  // the real fw (not widened) — only the corner style changes here, not the
  // percentage it represents. growRight anchors the fill to the track's
  // right edge (bx+bw-fw), growing leftward as pct increases; the default
  // (growRight=false) anchors to the left edge (bx), growing rightward —
  // the more familiar "loading bar" direction.
  int fx = growRight ? (bx + bw - fw) : bx;
  if (fw > 0) gfx->fillRoundRect(fx, by, fw, bh, bh / 2, barColor(pctRounded));

  char rs[16], line[10 + sizeof(rs) + 1];
  fmtReset(resetMins, rs, sizeof(rs));
  // fmtReset's longest output is 7 chars ("23h 59m", the session card's
  // "%dh %2dm" branch at h=23 — reachable on the weekly card near its
  // window's end) — pad to that so a shorter new string (e.g. "now") still
  // overwrites every pixel the longest previous string could have touched.
  // 204px at size 2 from x=22, well inside the 232px card edge.
  snprintf(line, sizeof(line), "Resets in %-7s", rs);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM, C_PANEL);
  // top+66, not +64 -- was flush against the bar's bottom edge (by+bh=64),
  // zero gap ("bar too close to text"). Text draws opaque per-glyph
  // (setTextColor(fg, C_PANEL)), so it self-clears at the new position --
  // no separate clear needed, unlike touching the bar itself would require.
  gfx->setCursor(x + 14, top + 66);
  gfx->print(line);
}

// Stats screen: mascot header + 5h/7d meters. `full` clears and redraws
// everything (first entry / wake / mode switch / mascot-idle exit); a
// steady-state data update (`full=false`) skips the full-screen clear and the
// static mascot/title, redrawing only what can actually change — the two
// meter cards handle their own clean overwrite (see drawMeter), and the
// status dot is small enough to just always redraw (accent or blanked back
// to the background) rather than gate it behind `full` too.
static void drawUsage(const UsageData& u, bool full, bool growRight) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  s_mascotPrimed = false;   // force a full redraw next time the idle animation shows

  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: a small calm mascot pose + title.
    blitMascot(gfx, mascotIdleCells(), mascotIdlePalette(), 6, 4, 2);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(56, 12);
    gfx->print("CLAUDE");
  }

  if (!u.valid) {
    if (full) gfxDrawCentered(u.error ? "daemon error" : "waiting...", 120, 2, C_DIM);
    return;
  }

  // A non-"allowed" status (warning / rejected) gets a small accent flag;
  // always redraw this dot (in its "on" or "off"/background color) since it
  // can flip between identical-looking data updates and isn't covered by
  // the full-screen clear on steady-state redraws.
  bool warn = u.status[0] && strncmp(u.status, "allowed", 7) != 0;
  // x=160: clear of the clock overlay's flip-clock cards (right-aligned to
  // x=232, clear rect starts ~x=173) -- was x=228, which sat under the
  // clock's last digit card and punched a hole in it on every redraw here
  // (this dot is ungated/unconditional, the clock only redraws every 30s).
  gfx->fillCircle(160, 18, 5, warn ? C_ACCENT : C_BLACK);

  drawMeter(gfx, 50,  "5h", u.sessionPct, u.sessionResetMin, full, growRight);
  drawMeter(gfx, 138, "7d", u.weeklyPct,  u.weeklyResetMin, full, growRight);
}

// Idle animation: full-screen mascot, diffed cell-by-cell for a flicker-free draw.
static void drawMascot(const uint8_t* cells, const uint16_t* palette, bool restart) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx || !cells || !palette) return;
  uint16_t pal[MASCOT_PALETTE_SIZE];
  loadPalette(palette, pal);
  const int CP = TFT_WIDTH / MASCOT_GRID;                 // 240 / 20 = 12
  // Grid exactly fills the screen (20*12 = 240) -- no framing slack, so any
  // off-centering is the sprite's own pixel content. Most animation sets
  // (idle breathe/blink, dance sway/bounce) are hand-drawn symmetric about
  // grid column/row 10, but a 20-cell grid has no true-center cell at 9.5 --
  // so they visibly sit half a cell right+low. Nudging the draw origin back
  // half a cell recenters those (explicit tradeoff, chosen deliberately):
  // it puts the one already-centered set (work_coding) 6px off the other
  // way, worse proportionally since that pose nearly fills the grid.
  const int x0 = (TFT_WIDTH  - MASCOT_GRID * CP) / 2 - CP / 2;
  const int y0 = (TFT_HEIGHT - MASCOT_GRID * CP) / 2 - CP / 2;

  // Full redraw on (re)entry or whenever the palette changes (animation switch);
  // otherwise only repaint the cells that changed since the last frame.
  bool full = restart || !s_mascotPrimed || palette != s_mascotPalette;
  if (full) gfx->fillScreen(C_BLACK);

  for (int i = 0; i < MASCOT_GRID * MASCOT_GRID; i++) {
    uint8_t code = pgm_read_byte(&cells[i]);
    if (!full && code == s_prevCells[i]) continue;
    s_prevCells[i] = code;
    uint16_t color = (code < MASCOT_PALETTE_SIZE) ? pal[code] : 0;
    int gx = i % MASCOT_GRID, gy = i / MASCOT_GRID;
    gfx->fillRect(x0 + gx * CP, y0 + gy * CP, CP, CP, color);
  }
  s_mascotPrimed  = true;
  s_mascotPalette = palette;
}

// ---- DisplayMode ----------------------------------------------------------
void UsageMode::begin(const Settings& s) {
  usageInit(s);
  mascotInit();
  usageSampled_ = 0;
  usageRenderedOk_ = 0xFFFFFFFF;
  showingMascot_ = false;
  needRender_ = true;
  needFullRender_ = true;
}

// Top-right "HH:MM" clock, this page only (per explicit request -- was a
// global overlay in main.cpp, moved here). Redraws once every 2s, or
// immediately on first call after this page hasn't been serviced for at
// least 2s (i.e. just switched onto this page) -- wraparound-safe pattern
// matches CalendarMode.cpp's nextPageMs_. A full black fillRect (wider/
// taller than the nominal glyph box) runs before the text every time this
// fires, since an exact-fit box left a sliver of old ink visible on some
// digits (e.g. bottom of "8").
void UsageMode::drawClockOverlay() {
  struct tm t;
  if (!clockNow(t)) return;   // unsynced -- nothing trustworthy to show yet
  int curMinute = t.tm_hour * 60 + t.tm_min;
  bool minuteChanged = curMinute != clockLastMinute_;
  if (!minuteChanged && (int32_t)(millis() - clockNextRedrawMs_) < 0) return;
  clockNextRedrawMs_ = millis() + 30000;   // redraw interval, per explicit request (was 2s)
  clockLastMinute_ = curMinute;

  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  const char digits[4] = { buf[0], buf[1], buf[3], buf[4] };

  // Flip-clock look: 4 grey digit cards (no colon), red digits -- per
  // explicit request, replacing the plain "HH:MM" text.
  const int cardW = 13, cardH = 22, gapIn = 1, gapPair = 3;
  const int totalW = cardW * 4 + gapIn * 2 + gapPair;
  const int cardsRight = 8 + 224;   // matches drawMeter's card right edge (x=8, w=224)
  const int x0 = cardsRight - totalW, y = 14;
  gfx->fillRect(x0 - 2, y, totalW + 4, cardH, C_BLACK);   // clear old footprint too
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

void UsageMode::invalidate(const Settings& s) {
  needRender_ = true;
  needFullRender_ = true;
  showingMascot_ = false;
  usageRenderedOk_ = 0xFFFFFFFF;
  usageInit(s);
  usageForceRefresh();
}

void UsageMode::service(const Settings& s) {
  // Pull mode: poll the daemon when a Usage URL is set. Push mode: leave it blank
  // and the daemon POSTs to /api/usage (for networks where the device can't reach
  // the PC). Either way usageGet() drives the render below.
  if (s.usage.usageUrl.length() >= 8) usageService(s);

  const UsageData& u = usageGet();

  // Feed the burn-rate tracker once per fresh reading (drives the mascot's mood).
  if (u.valid && u.lastOkMs != usageSampled_) {
    usageSampled_ = u.lastOkMs;
    mascotSample(u.sessionPct);
  }

  // Considered stale after ~2 missed polls (plus a grace) — then show the animation.
  uint32_t staleMs = (uint32_t)s.usage.pollSec * 1000UL * 2UL + USAGE_STALE_GRACE_MS;

  if (usageFresh(staleMs)) {
    if (showingMascot_) { showingMascot_ = false; needRender_ = true; needFullRender_ = true; }
    if (u.lastOkMs != usageRenderedOk_) { usageRenderedOk_ = u.lastOkMs; needRender_ = true; }
    if (needRender_) {
      drawUsage(u, needFullRender_, s.usage.barGrowRight);
      needRender_ = false;
      // Only consume the full-redraw flag once data was actually valid —
      // usageFresh() already implies u.valid so drawUsage's early-return
      // !u.valid path can't currently be reached here, but keep this
      // defensive: if that ever changes, an invalid-data call must not eat
      // a pending full-redraw request, or the next valid render would wrongly
      // skip the header/mascot/labels.
      if (u.valid) needFullRender_ = false;
    }
  } else {
    if (!showingMascot_) {
      showingMascot_ = true;
      usageRenderedOk_ = 0xFFFFFFFF;
      mascotReset();
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/true);
    } else if (mascotTick()) {
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/false);
    }
  }

  // Only on the real usage-bars page -- the mascot's full-screen fillScreen
  // on entry already clears any clock footprint, so skipping this while
  // showingMascot_ leaves no stale digits behind.
  if (!showingMascot_) drawClockOverlay();
}
