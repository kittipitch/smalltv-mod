#include "MoviesMode.h"
#include "MovieIcons.h"
#if WITH_MOVIES
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "Gfx.h"
#include "config.h"

MoviesMode g_moviesMode;

// Wire format (clawdmeter-daemon movies.pack_movie): the poster, 160 x 240 RGB565 big-endian, then an
// 8-byte trailer 'M','V', tomatometer, popcorn, year LE, source, title length (score 255 = none; source
// 0 = in theaters, 1 = at home), then the title as printable ASCII in a 64-byte zero-padded field. The poster is a picture; the column is drawn HERE in the device's own font,
// so it matches the quota pages (owner 2026-10-31: "poster = pic and the font shud be font").
static const uint16_t kPW = MOVIES_POSTER_W;
static const uint16_t kPH = MOVIES_POSTER_H;
static const uint32_t kPosterBytes = (uint32_t)kPW * kPH * 2;
static const uint32_t kFrameBytes = MOVIES_BYTES;
#define C_PANEL 0x18E3   // card fill 0x1f1f1e: the quota pages' card colour

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

// One Twemoji icon (MovieIcons.h), centred on cx, a row at a time through the panel's tone correction.
static void drawIcon(Arduino_GFX* g, const uint16_t* art, int cx, int y) {
  uint16_t row[MOVIE_ICON_PX];
  for (int r = 0; r < MOVIE_ICON_PX; r++) {
    memcpy_P(row, art + r * MOVIE_ICON_PX, sizeof(row));
    gfxToneRow(row, MOVIE_ICON_PX);
    g->draw16bitRGBBitmap((int16_t)(cx - MOVIE_ICON_PX / 2), (int16_t)(y + r), row, MOVIE_ICON_PX, 1);
  }
}

static void drawCentered(Arduino_GFX* g, int cx, int y, const char* text, uint8_t maxSize, int width) {
  uint8_t sz = gfxFitSize(text, width, maxSize);
  g->setTextSize(sz);
  g->setTextColor(C_WHITE);   // transparent: an opaque cell would clip the icon above ("--" sits 9 px up)
  g->setCursor(cx - gfxTextW(text, sz) / 2, y);
  g->print(text);
}

static void scoreText(uint8_t v, char* out, size_t n) {
  if (v > 100) strlcpy(out, "--", n);
  else snprintf(out, n, "%d%%", v);
}

// The right column: ONE rounded card like the quota pages holding three items (tomato + score, popcorn
// + score, film/TV + year), spaced evenly by ink: the gap above the first, between items and below the
// last is the same. Item = 28 px Twemoji icon, 8 px gap, text ink (glcd: 7 rows per size step; "--" inks
// only row 3, so a missing score is spaced by its dash, not by an empty 7-row box).
// The title runs across the top of the screen, over the poster and the card (owner 2026-10-31), in
// yellow at the year's size: at most two centred lines of 20 characters (size 2 = 12 px a character on
// 240 px), word-wrapped; a longer title ends in "..." on the second line. Returns the number of lines.
static const int kTitleCols = 20, kTitleLines = 2, kTitleY = 4, kTitlePitch = 18;
static int wrapTitle(const char* s, char out[kTitleLines][kTitleCols + 1]) {
  int n = 0;
  while (n < kTitleLines) {
    while (*s == ' ') s++;
    const int len = (int)strlen(s);
    if (!len) break;
    int take = len;
    if (len > kTitleCols) {
      take = kTitleCols;                               // one long word: hard cut
      for (int i = kTitleCols; i > 0; i--)
        if (s[i] == ' ') { take = i; break; }
    }
    if (n == kTitleLines - 1 && take < len) {         // more than fits: end in "..."
      int k = len < kTitleCols - 3 ? len : kTitleCols - 3;
      while (k > 0 && s[k - 1] == ' ') k--;
      memcpy(out[n], s, k);
      strcpy(out[n] + k, "...");
    } else {
      memcpy(out[n], s, take);
      out[n][take] = 0;
    }
    s += take;
    n++;
  }
  return n;
}

// Yellow with a 1 px black outline, so it reads over any poster.
static void drawTitle(Arduino_GFX* g, char lines[kTitleLines][kTitleCols + 1], int n) {
  g->setTextSize(2);
  for (int l = 0; l < n; l++) {
    const int x = (240 - gfxTextW(lines[l], 2)) / 2, y = kTitleY + l * kTitlePitch;
    g->setTextColor(C_BLACK);
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (dx || dy) { g->setCursor(x + dx, y + dy); g->print(lines[l]); }
    g->setTextColor(C_YELLOW);
    g->setCursor(x, y);
    g->print(lines[l]);
  }
}

static void drawColumn(Arduino_GFX* g, const uint8_t* t) {
  const int x = kPW + 4, w = 240 - kPW - 8, cx = x + w / 2, inner = w - 6;
  const int iconH = MOVIE_ICON_PX, gapIT = 8;
  char title[kTitleCols * 4 + 1], lines[kTitleLines][kTitleCols + 1];
  const uint8_t tl = t[7] <= 64 ? t[7] : 64;
  memcpy(title, t + 8, tl);
  title[tl] = 0;
  const int nl = wrapTitle(title, lines);
  // The items always sit under ONE title line (size 2 inks 14 rows), gaps all equal; a second line is
  // drawn over the tomato rather than pushing the card down (owner 2026-10-31).
  const int top = nl ? kTitleY + 14 : 4, h = 236 - top;
  char txt[3][8];
  scoreText(t[2], txt[0], sizeof(txt[0]));
  scoreText(t[3], txt[1], sizeof(txt[1]));
  snprintf(txt[2], sizeof(txt[2]), "%u", (unsigned)(t[4] | (t[5] << 8)));
  const uint8_t maxSz[3] = {3, 3, 2};                // year smaller than the scores (owner)
  uint8_t sz[3];
  int inkTop[3], inkH[3], used = 0;
  for (int i = 0; i < 3; i++) {
    sz[i] = gfxFitSize(txt[i], inner, maxSz[i]);
    const bool dash = strcmp(txt[i], "--") == 0;
    inkTop[i] = dash ? 3 * sz[i] : 0;
    inkH[i] = dash ? sz[i] : 7 * sz[i];
    used += iconH + gapIT + inkH[i];
  }
  const int sp = (h - used) / 4;
  g->fillRect(kPW, 0, 240 - kPW, 240, C_BLACK);
  g->fillRoundRect(x, 4, w, 232, 8, C_PANEL);
  int y = top + sp + (h - used - 4 * sp) / 2;        // centre the rounding remainder
  for (int i = 0; i < 3; i++) {
    const uint16_t* art = i == 0 ? kIconTomato : i == 1 ? kIconPopcorn : t[6] == 0 ? kIconFilm : kIconTv;
    drawIcon(g, art, cx, y);
    drawCentered(g, cx, y + iconH + gapIT - inkTop[i], txt[i], sz[i], inner);
    y += iconH + gapIT + inkH[i] + sp;
  }
  drawTitle(g, lines, nl);
}

uint32_t g_moviesDrawMs = 0;   // last full paint, for /api/status (it eats into the page's dwell)

static void drawCurrent() {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  const uint32_t t0 = millis();
  File f = LittleFS.open(MOVIES_PATH, "r");
  uint8_t t[72] = {0};
  bool ok = f && f.size() == kFrameBytes && f.seek(kPosterBytes) && f.read(t, 72) == 72 &&
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
  g_moviesDrawMs = millis() - t0;
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
