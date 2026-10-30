#include "MoviesMode.h"
#if WITH_MOVIES
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "Gfx.h"
#include "config.h"

MoviesMode g_moviesMode;

// Wire format (clawdmeter-daemon movies.pack_movie): the poster, 160 x 240 RGB565 big-endian, then an
// 8-byte trailer 'M','V', tomatometer, popcorn, year LE, source, 0 (score 255 = none; source 0 = in
// theaters, 1 = at home). The poster is a picture; the column is drawn HERE in the device's own font,
// so it matches the quota pages (owner 2026-10-31: "poster = pic and the font shud be font").
static const uint16_t kPW = MOVIES_POSTER_W;
static const uint16_t kPH = MOVIES_POSTER_H;
static const uint32_t kPosterBytes = (uint32_t)kPW * kPH * 2;
static const uint32_t kFrameBytes = MOVIES_BYTES;
#define C_PANEL 0x18E3   // card fill 0x1f1f1e: the quota pages' card colour

// 12x12 icons drawn at 2x: '#' = main colour, 'g' = second colour, '.' = card background.
static const char kTomato[12][13] PROGMEM = {
  "....gg.g....", "...gggggg...", "..##gggg##..", ".##########.", "############", "############",
  "############", "############", "############", ".##########.", "..########..", "....####...."};
static const char kPopcorn[12][13] PROGMEM = {
  "..###.###...", ".#########..", "###########.", ".#########..", "..g#g#g#g...", "..g#g#g#g...",
  "..g#g#g#g...", "...g#g#g#...", "...g#g#g#...", "...g#g#g#...", "....g#g#....", "....g#g#...."};
static const char kFilm[12][13] PROGMEM = {      // clapperboard: in theaters
  "##gg##gg##gg", "gg##gg##gg##", "############", "............", "############", "############",
  "##g######g##", "############", "############", "##g######g##", "############", "############"};
static const char kTv[12][13] PROGMEM = {        // television: at home
  "...#....#...", "....#..#....", ".....##.....", "############", "#gggggggggg#", "#gggggggggg#",
  "#gggggggggg#", "#gggggggggg#", "#gggggggggg#", "############", "..#......#..", "..#......#.."};

static uint16_t s_row[kPW];         // one poster row, reused; .bss, not the ~1800 B continuation stack
static uint32_t s_rx   = 0;
static bool     s_open = false;
static File     s_f;
void moviesRxBegin() {
  s_rx = 0;
  s_f = LittleFS.open(MOVIES_TMP_PATH, "w");
  s_open = (bool)s_f;
}

void moviesRxChunk(const uint8_t* data, size_t len) {
  if (!data || !len || !s_open) return;
  if (s_rx >= kFrameBytes) return;                 // ignore anything past a full frame
  if (s_rx + len > kFrameBytes) len = kFrameBytes - s_rx;
  if (s_f.write(data, len) != len) {               // out of space or a bad block
    s_f.close(); s_open = false;
    LittleFS.remove(MOVIES_TMP_PATH);
    return;
  }
  s_rx += len;
}

bool moviesRxEnd(bool ok) {
  bool whole = ok && s_open && (s_rx == kFrameBytes);
  if (s_open) { s_f.close(); s_open = false; }
  // Replaces an unshown NEXT, so there are never more than two frames at rest.
  if (whole && LittleFS.rename(MOVIES_TMP_PATH, MOVIES_NEXT_PATH)) return true;
  LittleFS.remove(MOVIES_TMP_PATH);
  return false;
}

bool moviesHasCurrent() { return LittleFS.exists(MOVIES_PATH); }

static void drawWaiting() {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  g->fillScreen(C_BLACK);
  g->setTextColor(C_WHITE);
  g->setTextSize(3);
  g->setCursor(66, 92);
  g->print("Movies");
  g->setTextColor(C_GRAY);
  g->setTextSize(2);
  g->setCursor(12, 136);
  g->print("waiting for daemon");
}

static void drawIcon(Arduino_GFX* g, const char (*art)[13], int cx, int y, uint16_t main, uint16_t other) {
  int x0 = cx - 12;
  for (int r = 0; r < 12; r++)
    for (int c = 0; c < 12; c++) {
      char p = (char)pgm_read_byte(&art[r][c]);
      if (p != '.') g->fillRect(x0 + 2 * c, y + 2 * r, 2, 2, p == '#' ? main : other);
    }
}

static void drawCentered(Arduino_GFX* g, int cx, int y, const char* text, uint8_t maxSize, int width) {
  uint8_t sz = gfxFitSize(text, width, maxSize);
  g->setTextSize(sz);
  g->setTextColor(C_WHITE, C_PANEL);              // the quota pages' value colour
  g->setCursor(cx - gfxTextW(text, sz) / 2, y);
  g->print(text);
}

static void scoreText(uint8_t v, char* out, size_t n) {
  if (v > 100) strlcpy(out, "--", n);
  else snprintf(out, n, "%d%%", v);
}

// The right column: three rounded cards like the quota pages, icons, and text in the device's font.
static void drawColumn(Arduino_GFX* g, const uint8_t* t) {
  const int x = kPW + 4, w = 240 - kPW - 8, cx = x + w / 2, inner = w - 6;
  g->fillRect(kPW, 0, 240 - kPW, 240, C_BLACK);
  const int tops[3] = {4, 83, 162};
  for (int i = 0; i < 3; i++) g->fillRoundRect(x, tops[i], w, 74, 8, C_PANEL);
  char buf[8];
  drawIcon(g, kTomato, cx, tops[0] + 8, 0xF986 /* tomato red */, 0x3D87 /* leaf green */);
  scoreText(t[2], buf, sizeof(buf));
  drawCentered(g, cx, tops[0] + 42, buf, 3, inner);
  drawIcon(g, kPopcorn, cx, tops[1] + 8, 0xFE80 /* popcorn yellow */, 0xE945 /* stripe red */);
  scoreText(t[3], buf, sizeof(buf));
  drawCentered(g, cx, tops[1] + 42, buf, 3, inner);
  if (t[6] == 0) drawIcon(g, kFilm, cx, tops[2] + 8, C_WHITE, 0x4208);
  else           drawIcon(g, kTv, cx, tops[2] + 8, C_WHITE, 0x041F);
  snprintf(buf, sizeof(buf), "%u", (unsigned)(t[4] | (t[5] << 8)));
  drawCentered(g, cx, tops[2] + 44, buf, 2, inner);   // year: smaller than the scores (owner)
}

static void drawCurrent() {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  File f = LittleFS.open(MOVIES_PATH, "r");
  uint8_t t[8] = {0};
  bool ok = f && f.size() == kFrameBytes && f.seek(kPosterBytes) && f.read(t, 8) == 8 &&
            t[0] == 'M' && t[1] == 'V' && f.seek(0);
  if (!ok) {
    if (f) f.close();
    drawWaiting();
    return;
  }
  for (uint16_t y = 0; y < kPH; y++) {
    if (f.read((uint8_t*)s_row, kPW * 2) != (int)(kPW * 2)) {
      g->fillRect(0, (int16_t)y, kPW, (int16_t)(kPH - y), C_BLACK);   // damaged file: no stale pixels
      break;
    }
    for (uint16_t i = 0; i < kPW; i++) s_row[i] = __builtin_bswap16(s_row[i]);
    gfxToneRow(s_row, kPW);    // draw16bitRGBBitmap skips the driver's tone hooks (see Gfx.h)
    g->draw16bitRGBBitmap(0, (int16_t)y, s_row, kPW, 1);
    yield();
  }
  f.close();
  drawColumn(g, t);
}

bool moviesPromote() {
  if (!LittleFS.exists(MOVIES_NEXT_PATH)) return false;
  if (!LittleFS.rename(MOVIES_NEXT_PATH, MOVIES_PATH)) return false;
  if (moviesPageIsUp()) drawCurrent();
  return true;
}

void MoviesMode::begin(const Settings& s) {
  (void)s;
  // A push cut off by a reboot or power loss leaves a half-written temp file: dead weight.
  if (LittleFS.exists(MOVIES_TMP_PATH)) LittleFS.remove(MOVIES_TMP_PATH);
}

void MoviesMode::service(const Settings& s) { (void)s; }   // frames arrive by push
// Every settings save calls invalidate() on ALL modes: repaint only when this page is the one
// on screen, or it would cover the album photo (or flash over any other page).
void MoviesMode::invalidate(const Settings& s) { (void)s; if (moviesPageIsUp()) drawCurrent(); }
void MoviesMode::wake(const Settings& s) { (void)s; drawCurrent(); }

#endif  // WITH_MOVIES
