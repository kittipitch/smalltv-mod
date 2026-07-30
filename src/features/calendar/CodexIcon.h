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
// Re-scaled a later session, same reason and same method as
// AntigravityIcon.h: ink originally spanned 30 of 32 rows, 4px taller
// than the mascot header's own 26px ink height at the same y=12 draw
// position ("looks very close to the border").
//
// THIS REPLACES AN EARLIER BAD EDIT that shrank height ONLY (a row-pair
// deleted from the middle of the blob) while leaving the full 32px width
// untouched -- i.e. a non-uniform squish that flattened the mark and
// distorted its aspect ratio. Traced from explicit user feedback ("the
// logo of codex is ugly"), then confirmed: "so yes we do maintain the
// proportion to the original logo... not flatten to get the height only."
//
// Correct method, applied here: decode the ORIGINAL undistorted array,
// crop to its true ink bounding box (32w x 30h -- full canvas width, 30
// of 32 rows), then resize with ONE uniform scale factor on both axes
// via PIL: 28/30 = 0.933333, giving 30w x 28h (width 32 * 0.933333 =
// 29.87, rounded to 30). Image.NEAREST is used, not LANCZOS -- this is
// deliberately blocky 2x-doubled pixel art (see the rasterization note
// above), so a smooth filter would defeat the whole look. The result is
// then placed top-aligned at row 0 and horizontally centered (ox=1)
// inside the unchanged 32x32 canvas; the surrounding area is blank.
// Top-aligned, not vertically centered, on purpose: the icon draws at
// drawBitmap(6, 12, ...), so centering would push the bottom ink edge
// back to screen y=41 -- exactly where the original sat, re-creating the
// card-crowding this shrink exists to fix.
//
// Consequence worth knowing: at a 28/30 scale a few rows no longer come
// in identical 2px pairs (the resample lands some steps on 1px). That is
// unavoidable when scaling proportionally to a non-multiple height, and
// is the accepted tradeoff -- correct proportions beat perfectly uniform
// 2px steps. Any earlier comment here claiming rows were trimmed
// "rather than resampling" describes the bad edit and no longer applies.
// Verified via an ASCII dump of the final mask: the blob outline and the
// ">_" glyph hole both read correctly.
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
  0x00, 0x3F, 0xC0, 0x00, 0x00, 0x3F, 0xC0, 0x00, 0x00, 0x7F, 0xFF, 0x80,
  0x00, 0x7F, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0xE0, 0x01, 0xFF, 0xFF, 0xE0,
  0x07, 0xFF, 0xFF, 0xF8, 0x1F, 0xFF, 0xFF, 0xF8, 0x1F, 0xFF, 0xFF, 0xF8,
  0x7F, 0xBF, 0xFF, 0xF8, 0x7F, 0xBF, 0xFF, 0xF8, 0x7F, 0x8F, 0xFF, 0xF8,
  0x7F, 0x8F, 0xFF, 0xF8, 0x7F, 0xCF, 0xFF, 0xFE, 0x7F, 0xCF, 0xFF, 0xFE,
  0x1F, 0x8F, 0xFF, 0xFE, 0x1F, 0x8F, 0xFF, 0xFE, 0x1F, 0xBF, 0x00, 0xFE,
  0x1F, 0xBF, 0x00, 0xFE, 0x1F, 0xFF, 0xFF, 0xF8, 0x1F, 0xFF, 0xFF, 0xF8,
  0x1F, 0xFF, 0xFF, 0xE0, 0x07, 0xFF, 0xFF, 0x80, 0x07, 0xFF, 0xFF, 0x80,
  0x01, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0xFC, 0x00,
  0x00, 0x03, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
