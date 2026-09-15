// AlbumMode.h — photo page: raw RGB565 frames streamed in by the daemon.
//
// There is no image decoder on this board and no room for one (the Ultra build
// has ~8 KB of flash budget left). The daemon does the resizing and the RGB565
// conversion on the host, where Pillow already lives, and pushes a finished
// 240x240 frame to /api/album. The device only has to move bytes to the panel.
//
// Two paths, deliberately different:
//
//   live      the upload handler draws each row to the panel AS IT ARRIVES, so
//             a frame never exists in RAM. 240x2 bytes of row buffer, not
//             115,200. This is the only reason the feature fits in ~26 KB of
//             free heap.
//   fallback  every ALBUM_CHECKPOINT_EVERY-th frame is ALSO written to
//             /album.565, and that file is what wake() repaints from when the
//             carousel comes back round with no fresh push in hand — daemon
//             stopped, host asleep, WiFi blip, or straight after a reboot.
//
// Why not checkpoint every frame: 115,200 B per rotation is ~660 whole-filesystem
// rewrites a day on the 1 MB layout, which is roughly five months of flash life.
// Every tenth frame is ~17 years. The owner accepted a repeated picture while
// the daemon is down, so the cheap option is the right one.
#pragma once
#include <Arduino.h>
#include "Mode.h"

#if WITH_ALBUM

// ---- receive path, driven by WebPortal's multipart upload handler ----------
// Call order is START -> CHUNK* -> END. albumRxBegin() decides up front whether
// this frame is drawn (only when the album page is the one on screen) and
// whether it is checkpointed; the chunk path then does no policy at all.
void albumRxBegin();
void albumRxChunk(const uint8_t* data, size_t len);
// Returns true when a complete, correctly-sized frame arrived.
bool albumRxEnd(bool ok);

// Seconds until the album page NEXT starts, for the push reply that keeps the
// daemon in step without polling. Strictly in the future (a push made while the
// page is up is answered with a full cycle, not 0). -1 = not in rotation.
int albumSecsToNextSlot();
// True while the album page is the one currently on screen. Both live in
// main.cpp, which owns the carousel index and its dwell clock.
bool albumPageIsUp();

class AlbumMode : public DisplayMode {
 public:
  const char* id() const override { return "album"; }
  uint8_t modeConst() const override { return MODE_ALBUM; }
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
};

extern AlbumMode g_albumMode;

#endif  // WITH_ALBUM
