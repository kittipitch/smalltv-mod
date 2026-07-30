// CodexIcon.h -- auto-generated 1-bit PROGMEM mask, 32x32.
// Source: the real Codex CLI app icon (user-supplied PNG, a rounded
// cloud/blob mark, blue-purple gradient, with a white ">_" terminal
// prompt glyph cut into it) -- NOT the older OpenAI teal flower-knot
// badge (checked ClaudeBar's bundled CodexIcon.svg first; that's a
// different, outdated icon and was discarded). Isolated the blob from
// its checkerboard-transparency background by color, not alpha (the
// source PNG has no alpha channel -- the checker pattern is baked into
// real pixels): a pixel counts as "mark" when its blue channel exceeds
// its red channel by more than 40, which cleanly separates the
// blue-dominant blob from the near-neutral checker squares AND from the
// white ">_" glyph -- the glyph pixels fail that test too, so they fall
// out as holes in the mask, matching how they read as negative space in
// the original. Downsampled to 16x15 with a box filter, rethresholded,
// then nearest-neighbor upscaled 2x to 32x32 -- same pixelated-look
// pipeline as AntigravityIcon.h, chosen for the same reason (a direct
// 32x32 render looked too smooth against the reference; a coarser
// 16-wide source doubled up gives visible 2x2 blocks). Same drawBitmap
// convention as WeatherIcons.h/ZaiIcon.h/AntigravityIcon.h -- see
// WeatherIcons.h for why this is 1-bit PROGMEM and not RGB565
// (word-access PROGMEM reads crash the ESP8266).
//
// Re-scaled a later session, same reason as AntigravityIcon.h: ink
// originally spanned 30 of 32 rows, 4px taller than the mascot header's
// own 26px ink height at the same y=12 draw position ("looks very close
// to the border"). Unlike Antigravity, this shape has no solid multi-row
// run to trim invisibly from -- every row pair here is a distinct step
// of the underlying blob outline. Rather than resampling (which shaved
// individual diagonal rows unevenly, breaking the uniform 2x2 blockiness
// this icon is deliberately built around), one duplicate row-pair was
// removed outright from a plain mid-body widening step (rows 8-9 of 30,
// well clear of the ">_" glyph-hole rows and the top/bottom caps).
// Lands at 28px, same deliberate tradeoff as AntigravityIcon.h (2px
// taller than an exact mascot match, in exchange for every row staying
// a clean 2px step). Verified via an ASCII dump before/after: the blob
// outline and glyph hole are intact, just one less widening row.
#pragma once
#include <Arduino.h>

#define CODEX_ICON_SIZE 32

// Sampled from the source PNG's own gradient near its top lobe (RGB
// 172,166,255 -- the actual purple part; the gradient's center/bottom
// runs more blue than purple, an earlier sample from there produced a
// misleadingly-named constant, corrected before flashing) and converted
// to RGB565 -- per live feedback ("we can use that purple color but
// make it mono"), the icon renders in this one color instead of
// white/C_SKY like the other quota-page icons.
#define C_CODEX_PURPLE 0xAD3F

static const uint8_t kCodexIcon[128] PROGMEM = {
  0x00, 0x3F, 0xC0, 0x00, 0x00, 0x3F, 0xC0, 0x00, 0x00, 0xFF, 0xFF, 0xC0,
  0x00, 0xFF, 0xFF, 0xC0, 0x03, 0xFF, 0xFF, 0xF0, 0x03, 0xFF, 0xFF, 0xF0,
  0x0F, 0xFF, 0xFF, 0xFC, 0x0F, 0xFF, 0xFF, 0xFC, 0xFF, 0x3F, 0xFF, 0xFC,
  0xFF, 0x3F, 0xFF, 0xFC, 0xFF, 0x0F, 0xFF, 0xFC, 0xFF, 0x0F, 0xFF, 0xFC,
  0xFF, 0xCF, 0xFF, 0xFF, 0xFF, 0xCF, 0xFF, 0xFF, 0x3F, 0x0F, 0xFF, 0xFF,
  0x3F, 0x0F, 0xFF, 0xFF, 0x3F, 0x3F, 0x00, 0xFF, 0x3F, 0x3F, 0x00, 0xFF,
  0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xF0,
  0x3F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xC0, 0x0F, 0xFF, 0xFF, 0xC0,
  0x03, 0xFF, 0xFF, 0x00, 0x03, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0xFC, 0x00,
  0x00, 0x03, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
