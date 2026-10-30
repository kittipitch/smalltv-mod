#include "MoviesMode.h"
#if WITH_MOVIES
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "Gfx.h"
#include "config.h"

MoviesMode g_moviesMode;

// Same wire format as the album: 240 x 240, RGB565, big-endian, no header.
static const uint16_t kW = MOVIES_W;
static const uint16_t kH = MOVIES_H;
static const uint32_t kFrameBytes = (uint32_t)kW * kH * 2;

static uint16_t s_row[kW];          // one row, reused; .bss, not the ~1800 B continuation stack
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

static void drawCurrent() {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  File f = LittleFS.open(MOVIES_PATH, "r");
  if (!f || f.size() != kFrameBytes) {
    if (f) f.close();
    drawWaiting();
    return;
  }
  for (uint16_t y = 0; y < kH; y++) {
    if (f.read((uint8_t*)s_row, kW * 2) != (int)(kW * 2)) {
      g->fillRect(0, (int16_t)y, kW, (int16_t)(kH - y), C_BLACK);   // damaged file: no stale pixels
      break;
    }
    for (uint16_t i = 0; i < kW; i++) s_row[i] = __builtin_bswap16(s_row[i]);
    g->draw16bitRGBBitmap(0, (int16_t)y, s_row, kW, 1);
    yield();
  }
  f.close();
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
void MoviesMode::invalidate(const Settings& s) { (void)s; drawCurrent(); }
void MoviesMode::wake(const Settings& s) { (void)s; drawCurrent(); }

#endif  // WITH_MOVIES
