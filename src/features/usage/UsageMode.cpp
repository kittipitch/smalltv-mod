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
// `known=false` means the account has no such window (see UsageData::hasWeekly):
// the percentage and the countdown are both drawn as N/A and the bar is left
// empty. Drawing 0%% there would be a fabricated reading, not a measurement.
static void drawMeter(Arduino_GFX* gfx, int top, const char* label,
                      float pct, int resetMins, bool full, bool growRight,
                      bool known = true) {
  const int x = 8, w = 224, h = 82;
  // Only clear+redraw the card background on structural changes. On a plain
  // data update, everything below is self-overwriting (opaque text, self-
  // clearing bar), so skipping this is what actually removes the flash —
  // this was the bug an Opus review caught: leaving this unconditional wiped
  // the label below every partial render, and also meant every push still
  // repainted ~64% of the screen, defeating the point of `full` entirely.
  if (full) gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

  // Label left, percentage right, bar fills from the right edge.
  // Same value rule on every quota page (Claude, z.ai, Codex, Antigravity),
  // per owner 2026-09-12 ("the meter shud look the same always"): at most 2
  // digits, so a real 100 prints "99%" in RED -- red means "capped, the real
  // value is 100". The bar below still uses the real pct. First asked for
  // Antigravity only (2026-07-30), where the label leaves no room for 3 digits.
  int pctRounded = (int)lroundf(constrain(pct, 0.0f, 100.0f));
  char pc[8];
  if (known) snprintf(pc, sizeof(pc), "%2d%%", constrain(pctRounded, 0, 99));
  else       strlcpy(pc, "N/A", sizeof(pc));
  uint8_t sz = gfxFitSize(pc, 150, 5);
  int pcw = gfxTextW(pc, sz);
  gfx->setTextSize(sz);
  gfx->setTextColor((known && pct >= 100.0f) ? C_RED : C_WHITE, C_PANEL);
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
  int fw = known ? (int)(bw * constrain(pct, 0.0f, 100.0f) / 100.0f) : 0;
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
  if (known) fmtReset(resetMins, rs, sizeof(rs));
  else       strlcpy(rs, "N/A", sizeof(rs));
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
// `stale` = the feed has gone quiet. The numbers stay on screen -- they are the
// last real reading and still the most useful thing this page can show -- and
// staleness is INDICATED by dimming the header, never SUBSTITUTED with a notice
// that throws the data away (owner requirement, 2026-09-11).
static void drawUsage(const UsageData& u, bool full, bool growRight, bool stale = false) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  s_mascotPrimed = false;   // force a full redraw next time the idle animation shows

  if (full) {
    gfx->fillScreen(C_BLACK);
    // Header: a small calm mascot pose + title.
    blitMascot(gfx, mascotIdleCells(), mascotIdlePalette(), 6, 4, 2);
    gfx->setTextSize(3);
    gfx->setTextColor(stale ? C_DIM : C_WHITE);
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

  // Age the countdowns while the feed is quiet. They were computed when the
  // reading arrived, so on a stale page they froze -- a unit whose daemon went
  // to sleep an hour ago kept claiming the same "Resets in", which is a wrong
  // number rather than an old one. Floor at 0 ("now") instead of going negative.
  int ageMin = 0;
  if (stale && u.lastOkMs) ageMin = (int)((millis() - u.lastOkMs) / 60000UL);
  int r5 = u.sessionResetMin - ageMin; if (r5 < 0) r5 = 0;
  int rw = u.weeklyResetMin  - ageMin; if (rw < 0) rw = 0;
  drawMeter(gfx, 50,  "5h", u.sessionPct, r5, full, growRight);
  drawMeter(gfx, 138, "7d", u.weeklyPct,  rw, full, growRight, u.hasWeekly);
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
  // half a cell recenters those. "work coding" is drawn already-centered on
  // its own grid though, so the same nudge pushed IT 6px off the other way
  // -- confirmed live ("the one we see the most are lopsided", the normal-
  // pace mood being the commonly-seen one -- "we adjusted the minimal mood
  // mascot remember? adjust the rest too"). Per-animation, not global: skip
  // the nudge only for "work coding", apply it to every other set.
  bool skipNudge = strcmp(mascotName(), "work coding") == 0;
  const int nudge = skipNudge ? 0 : CP / 2;
  const int x0 = (TFT_WIDTH  - MASCOT_GRID * CP) / 2 - nudge;
  const int y0 = (TFT_HEIGHT - MASCOT_GRID * CP) / 2 - nudge;

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

void UsageMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  // Regaining the screen after another carousel page repainted over us. Two
  // pieces of state believe they still own the framebuffer and both must be
  // dropped, or the idle mascot repaints as a cell-diff on top of whatever
  // page was showing -- the "mascot superimposed on the forecast" symptom.
  //
  // Deliberately NOT invalidate(): that ends in usageForceRefresh(), which in
  // pull mode would fire an HTTP poll on every carousel hop.
  showingMascot_ = false;   // -> drawMascot(restart=true) -> fillScreen
  showingDaemonOff_ = false; // -> stale usage repaint -> fillScreen
  s_mascotPrimed = false;   // the cell-diff base is stale
}

void UsageMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  needFullRender_ = true;
  showingMascot_ = false;
  showingDaemonOff_ = false;
  usageRenderedOk_ = 0xFFFFFFFF;
  // The idle mascot is drawn as a cell-diff against what it believes is still
  // on screen. Any other carousel page repaints over it, so that belief is
  // stale the moment this mode loses the screen -- and invalidate() is exactly
  // when it gets it back (wake() calls this). Without the reset the next idle
  // frame skips its fillScreen and punches only the CHANGED cells over
  // whatever page is showing, which looks like the mascot superimposed on the
  // forecast/agenda. drawUsage() already clears this for the stats screen; the
  // idle path never goes through drawUsage(), so it has to be cleared here too.
  s_mascotPrimed = false;
  // Deliberately NOT usageInit() here: that clears the cached reading, and on
  // a push-mode device (usageUrl empty -- the daemon POSTs to /api/usage)
  // there is no device-side fetch to replace it. Every settings save then
  // dropped this page to the full-screen mascot until the next daemon push
  // landed, which for an unrelated POST -- e.g. a home-automation script
  // pushing {"brightness":N} -- is a visible regression for no benefit.
  // Same reasoning CalendarMode::invalidate() already documents. Pull mode
  // loses nothing: usageForceRefresh() re-polls on the next service tick,
  // which is all it ever wanted from usageInit(), and usageService()
  // self-inits if begin() somehow hasn't run.
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
    if (showingMascot_ || showingDaemonOff_) {
      showingMascot_ = false;
      showingDaemonOff_ = false;
      needRender_ = true; needFullRender_ = true;
    }
    if (u.lastOkMs != usageRenderedOk_) { usageRenderedOk_ = u.lastOkMs; needRender_ = true; }
    if (weeklyKnownRendered_ != (int8_t)u.hasWeekly) {
      weeklyKnownRendered_ = (int8_t)u.hasWeekly;
      needRender_ = true; needFullRender_ = true;
    }
    if (needRender_) {
      // A full repaint clears the screen, clock included, and
      // drawClockOverlay() only redraws on a minute change or its 30 s
      // deadline -- so the corner sat blank for up to 30 s after a carousel
      // re-entry, a stale/fresh transition or a settings change.
      if (needFullRender_) { clockLastMinute_ = -1; clockNextRedrawMs_ = millis(); }
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
  } else if (!u.valid) {
    // Warm-up, NOT an outage: no reading has ever landed this boot, so there is
    // nothing to have lost. This is the mascot's home -- the first seconds after
    // power-on, and the permanent state of a unit that has no daemon at all.
    if (!showingMascot_) {
      showingMascot_ = true;
      showingDaemonOff_ = false;
      usageRenderedOk_ = 0xFFFFFFFF;
      mascotReset();
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/true);
    } else if (mascotTick()) {
      drawMascot(mascotCells(), mascotPalette(), /*restart=*/false);
    }
  } else {
    // Data landed once and then stopped. main.cpp's link gate owns every case
    // where the DEVICE is at fault (it takes the whole screen before this mode
    // ever renders), so reaching here means the device is fine and the daemon's
    // end went quiet.
    //
    // A quiet daemon is now a NORMAL condition, not a fault to announce: the
    // owner's machine is simply off. This used to hand the whole screen to
    // drawDaemonOffline(), which discarded the last reading -- the only usage
    // data such a unit will have until that machine comes back. Keep the page,
    // dim the header.
    bool full = !showingDaemonOff_;
    if (full) {
      showingDaemonOff_ = true;
      showingMascot_ = false;
      usageRenderedOk_ = 0xFFFFFFFF;
      s_mascotPrimed = false;
    }
    if (full || (int32_t)(millis() - daemonOffNextMs_) >= 0) {
      if (full) { clockLastMinute_ = -1; clockNextRedrawMs_ = millis(); }
      drawUsage(u, full, s.usage.barGrowRight, /*stale=*/true);
      // Nothing on this page changes while the feed is quiet, so redraw rarely.
      daemonOffNextMs_ = millis() + 30000;
    }
  }

  // Only on the real usage-bars page -- the mascot's full-screen fillScreen
  // on entry already clears any clock footprint, so skipping this while
  // showingMascot_ leaves no stale digits behind.
  // The stale page is still the usage page, so it keeps its clock. Only the
  // mascot (which owns the whole screen) suppresses the overlay.
  if (!showingMascot_) drawClockOverlay();
}
