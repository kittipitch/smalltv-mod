#include "AlbumMode.h"
#if WITH_ALBUM
#include <Arduino_GFX_Library.h>
#include <LittleFS.h>
#include "Gfx.h"
#include "config.h"

AlbumMode g_albumMode;

// The wire format is fixed, so every size below is a constant and nothing is
// allocated at runtime: 240 x 240 pixels, RGB565, big-endian, no header.
static const uint16_t kW = ALBUM_W;
static const uint16_t kH = ALBUM_H;
static const uint32_t kFrameBytes = (uint32_t)kW * kH * 2;

// One row, reused for every row of every frame. 480 B in .bss rather than on
// the stack -- the ESP8266 continuation stack is only ~1800 B (see contstk in
// /api/status) and the upload handler is already several frames deep.
static uint16_t s_row[kW];

// ---- receive state --------------------------------------------------------
static uint32_t s_rx      = 0;      // bytes accepted this frame
static uint16_t s_rowLen  = 0;      // bytes currently held in s_row
static uint16_t s_y       = 0;      // next panel row to draw
static bool     s_draw    = false;  // draw this frame (album page is on screen)
static bool     s_save    = false;  // checkpoint this frame to /album.565
static bool     s_open    = false;  // s_ck is open
static File     s_ck;
static uint16_t s_sinceCk = ALBUM_CHECKPOINT_EVERY;  // force a checkpoint on the first frame

// Blit one full row. draw16bitRGBBitmap lives on Arduino_GFX itself (the
// writeAddrWindow/writePixels pair is Arduino_TFT-only and gfxDev() hands back
// the base type), and it still routes through the tone-corrected writePixels
// override in Gfx.cpp, so photos get the same panel calibration as text.
static void blitRow(uint16_t y) {
  Arduino_GFX* g = gfxDev();
  if (g && y < kH) g->draw16bitRGBBitmap(0, (int16_t)y, s_row, kW, 1);
}

void albumRxBegin() {
  s_rx = 0; s_rowLen = 0; s_y = 0;
  s_draw = albumPageIsUp();   // only paint when the page is actually on screen
  s_save = (++s_sinceCk >= ALBUM_CHECKPOINT_EVERY);
  s_open = false;
  if (s_save) {
    // Written under a temp name and renamed on success, so a push that dies
    // half way cannot leave a torn frame as the offline fallback.
    s_ck = LittleFS.open(ALBUM_TMP_PATH, "w");
    s_open = (bool)s_ck;
    if (!s_open) s_save = false;
  }
}

void albumRxChunk(const uint8_t* data, size_t len) {
  if (!data || !len) return;
  // Ignore anything past a full frame rather than trusting the sender's length.
  if (s_rx >= kFrameBytes) return;
  if (s_rx + len > kFrameBytes) len = kFrameBytes - s_rx;
  s_rx += len;

  if (s_open && s_ck.write(data, len) != len) {   // out of space or a bad block
    s_ck.close(); s_open = false; s_save = false;
    LittleFS.remove(ALBUM_TMP_PATH);
  }
  if (!s_draw) return;

  // Reassemble rows across arbitrary chunk boundaries: TCP chunks (up to 536 B
  // per segment on the ESP8266 low-memory lwIP build) do not line up with a
  // 480-byte row, and they can split a pixel in half.
  uint8_t* rowBytes = (uint8_t*)s_row;
  const uint16_t rowBytesLen = kW * 2;
  while (len) {
    uint16_t take = rowBytesLen - s_rowLen;
    if (take > len) take = len;
    memcpy(rowBytes + s_rowLen, data, take);
    s_rowLen += take; data += take; len -= take;
    if (s_rowLen == rowBytesLen) {
      for (uint16_t i = 0; i < kW; i++) s_row[i] = __builtin_bswap16(s_row[i]);
      blitRow(s_y++);
      s_rowLen = 0;
      yield();     // 240 rows of SPI: let WiFi and the TCP stack breathe
    }
  }
}

bool albumRxEnd(bool ok) {
  bool whole = ok && (s_rx == kFrameBytes);
  if (s_open) {
    s_ck.close(); s_open = false;
    if (whole && LittleFS.rename(ALBUM_TMP_PATH, ALBUM_PATH)) {
      s_sinceCk = 0;
    } else {
      LittleFS.remove(ALBUM_TMP_PATH);
    }
  }
  // A short or aborted frame leaves the panel half-painted; the next slot
  // repaints from the checkpoint, so don't try to patch it here.
  s_rowLen = 0;
  return whole;
}

// ---- fallback path --------------------------------------------------------
// Repaint from the last checkpoint. Same row-at-a-time shape as the live path,
// so this never allocates either.
static void drawCheckpoint() {
  Arduino_GFX* g = gfxDev();
  if (!g) return;
  File f = LittleFS.open(ALBUM_PATH, "r");
  if (!f || f.size() != kFrameBytes) {
    if (f) f.close();
    g->fillScreen(C_BLACK);
    return;
  }
  for (uint16_t y = 0; y < kH; y++) {
    if (f.read((uint8_t*)s_row, kW * 2) != (int)(kW * 2)) {
      // Size was checked above, so a short read means a damaged filesystem.
      // Black out the rest rather than leaving the previous page's pixels
      // showing through the bottom of a photo.
      g->fillRect(0, (int16_t)y, kW, (int16_t)(kH - y), C_BLACK);
      break;
    }
    for (uint16_t i = 0; i < kW; i++) s_row[i] = __builtin_bswap16(s_row[i]);
    blitRow(y);
    yield();
  }
  f.close();
}

void AlbumMode::begin(const Settings& s) {
  (void)s;
  // Any half-written checkpoint from a power cut mid-push is dead weight.
  if (LittleFS.exists(ALBUM_TMP_PATH)) LittleFS.remove(ALBUM_TMP_PATH);
}

// Nothing to poll: frames arrive by push and are painted as they land.
void AlbumMode::service(const Settings& s) { (void)s; }

void AlbumMode::invalidate(const Settings& s) { (void)s; drawCheckpoint(); }

void AlbumMode::wake(const Settings& s) { (void)s; drawCheckpoint(); }

#endif  // WITH_ALBUM
