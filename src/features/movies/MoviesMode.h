// MoviesMode.h — Movies page: Rotten Tomatoes Certified Fresh, one movie an hour.
//
// The daemon (clawdmeter-daemon --movies) reads the lists, picks the movie and sends the poster (160x240
// RGB565, big-endian) plus a 72-byte trailer with the scores, year, source and title; the device draws the right
// column itself (MoviesMode.cpp). The device stores at most TWO frames and no movie data at all (owner
// 2026-10-31: "dont keep it on device", "current and next one 2 at most"):
//
//   POST /api/movie          multipart field "frame", exactly MOVIES_BYTES (76,872 B) -> /movie_next.565
//                            (streamed through /movie_next.tmp, renamed only when whole)
//   POST /api/movie/promote  /movie_next.565 -> /movie.565, repaint if the page is on screen
//
// The daemon owns the clock (one promote an hour); the device never switches by itself. A failed or
// short upload never touches /movie.565. 24 writes a day, so flash wear is a non-issue (AlbumMode.h
// has the arithmetic). With no /movie.565 yet the page shows a "waiting for the daemon" screen and
// stays in the rotation.
#pragma once
#include <Arduino.h>
#include "Mode.h"

#if WITH_MOVIES

void moviesRxBegin();
void moviesRxChunk(const uint8_t* data, size_t len);
// True when a complete, correctly-sized frame arrived and is now the NEXT frame.
bool moviesRxEnd(bool ok);
// NEXT -> CURRENT. False when there is no next frame (or the rename failed).
bool moviesPromote();
bool moviesHasCurrent();
// True while the Movies page is the one on screen (main.cpp owns the carousel).
bool moviesPageIsUp();
extern uint32_t g_moviesDrawMs;   // last full paint of the page, ms (diagnostics)

class MoviesMode : public DisplayMode {
 public:
  const char* id() const override { return "movies"; }
  uint8_t modeConst() const override { return MODE_MOVIES; }
  void begin(const Settings& s) override;
  void service(const Settings& s) override;
  void invalidate(const Settings& s) override;
  void wake(const Settings& s) override;
};

extern MoviesMode g_moviesMode;

#endif  // WITH_MOVIES
