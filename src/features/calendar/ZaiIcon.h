// ZaiIcon.h -- auto-generated 1-bit PROGMEM mask, 40x40.
// Source: z.ai's own logo (https://z-cdn.chatglm.cn/z-ai/static/logo.svg),
// rasterized then thresholded to isolate the white "Z" glyph (plus its
// antialiased edge, which conveniently also captures a thin rounded-square
// frame outline) -- everything else (the square's dark fill, background)
// is "off", so on this device's black screen it reads as a plain white Z
// with a light frame, not a solid colored badge (per explicit live
// feedback against a reference screenshot -- an earlier square-silhouette
// version was rejected). Same drawBitmap convention as WeatherIcons.h --
// see that file's header comment for why this is 1-bit PROGMEM and not
// RGB565 (word-access PROGMEM reads crash on this chip).
//
// Re-scaled a later session: ink originally spanned 38 of 40 rows,
// leaving no room to draw at the y=12 margin every quota-page icon uses
// (verified in-tree: ZaiMode.cpp, CodexMode.cpp and AntigravityMode.cpp
// all call drawBitmap(6, 12, ...) -- all three are at y=12 today).
//
// THIS REPLACES AN EARLIER BAD EDIT that shrank height ONLY, leaving the
// full original width untouched -- i.e. a non-uniform squish that
// flattened the frame from a square into a wide rectangle and distorted
// the "Z" glyph with it. Traced from explicit user feedback on the
// sibling icon ("the logo of codex is ugly"), then confirmed for all
// three: "so yes we do maintain the proportion to the original logo...
// not flatten to get the height only."
//
// Correct method, applied here: decode the ORIGINAL undistorted array
// and crop to its true ink bounding box, computed rather than assumed --
// rows 1-38 and cols 1-38, i.e. 38w x 38h (square, as the rounded-square
// frame should be). Then resize with ONE uniform scale factor on both
// axes via PIL: 28/38 = 0.736842, giving exactly 28w x 28h -- still
// square, aspect ratio preserved to the pixel. Image.LANCZOS is used
// here, not NEAREST: unlike CodexIcon.h/AntigravityIcon.h's blocky
// 2x-doubled pixel art, this is a smoothly antialiased glyph+frame, and
// a smooth resize is the correct tool for a smooth source (same
// principle, opposite choice). The LANCZOS output is grayscale, so it is
// re-thresholded back to 1-bit at >127. The result is then placed
// top-aligned at row 0 and horizontally centered (ox=6) inside the
// unchanged 40x40 canvas; the surrounding area is blank. Top-aligned,
// not vertically centered, on purpose: centering would push the whole
// mark 6px further down the screen than the placement already accepted
// on-device, undoing the header-vs-card spacing this shrink exists for.
//
// The frame rail comes out 1px thick at this scale (it was 1-2px before
// too, from the same threshold pipeline) -- expected, not a defect.
// Verified via an ASCII dump of the final mask: the rounded-square frame
// and the "Z" glyph inside it both read correctly.
#pragma once
#include <Arduino.h>

#define ZAI_ICON_SIZE 40

static const uint8_t kZaiIcon[200] PROGMEM = {
  0x00, 0x3F, 0xFF, 0xFC, 0x00, 0x00, 0xE0, 0x00, 0x07, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x80,
  0x03, 0x00, 0x00, 0x00, 0xC0, 0x02, 0x00, 0x00, 0x00, 0x40,
  0x02, 0x0F, 0xF3, 0xF8, 0x40, 0x02, 0x1F, 0xE7, 0xF0, 0x40,
  0x02, 0x0F, 0xEF, 0xE0, 0x40, 0x02, 0x00, 0x0F, 0xE0, 0x40,
  0x02, 0x00, 0x1F, 0xC0, 0x40, 0x02, 0x00, 0x3F, 0x80, 0x40,
  0x02, 0x00, 0x7F, 0x80, 0x40, 0x02, 0x00, 0xFF, 0x00, 0x40,
  0x02, 0x00, 0xFF, 0x00, 0x40, 0x02, 0x01, 0xFE, 0x00, 0x40,
  0x02, 0x01, 0xFC, 0x00, 0x40, 0x02, 0x03, 0xF8, 0x00, 0x40,
  0x02, 0x07, 0xF0, 0x00, 0x40, 0x02, 0x07, 0xF7, 0xF0, 0x40,
  0x02, 0x0F, 0xE7, 0xF8, 0x40, 0x02, 0x1F, 0xCF, 0xF0, 0x40,
  0x02, 0x00, 0x00, 0x00, 0x40, 0x03, 0x00, 0x00, 0x00, 0xC0,
  0x01, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x80,
  0x00, 0xE0, 0x00, 0x07, 0x00, 0x00, 0x3F, 0xFF, 0xFC, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
