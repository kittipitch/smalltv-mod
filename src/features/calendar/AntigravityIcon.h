// AntigravityIcon.h -- auto-generated 1-bit PROGMEM mask, 32x32.
// Source: Google Antigravity's logo mark (vector SVG, not the padded app
// icon -- the app-icon PNG variant is a solid-filled rounded square with
// the mark drawn inside; using it directly would have produced a solid
// colored badge silhouette, the exact style already rejected for
// ZaiIcon.h per that file's own header comment). Rasterized at 16x15 (half
// this canvas) via rsvg-convert, top-aligned in a 16x16 buffer (the 1px
// slack from the 15px-tall render lands at the bottom, not centered --
// invisible at this resolution, not worth a fractional-pixel offset),
// alpha-thresholded (>50%), then nearest-neighbor upscaled 2x to 32x32 --
// per live feedback ("make it pixellated like this") a plain 32x32 render
// looked too smooth against the reference; doubling from a coarser 16x16
// source gives visibly blocky 2x2 pixels while keeping the two-peak notch
// that reads as the Antigravity mark intact (verified via an ASCII dump of
// the mask before committing -- a 12x12 source was also tried and closed
// the notch into a blob, discarded). Same drawBitmap convention as
// WeatherIcons.h/ZaiIcon.h -- see WeatherIcons.h for why this is 1-bit
// PROGMEM and not RGB565 (word-access PROGMEM reads crash the ESP8266).
//
// Re-scaled a later session: the raw rasterization above had its ink
// spanning 30 of the 32 rows; UsageMode.cpp's mascot header (the visual
// reference every quota page's header icon should match) only has 13
// grid rows of ink at cellPx=2 -- 26px. Both drawn at the same y=12, so
// the taller icon reached 4px further down than the mascot, crowding the
// first card below more than every other page ("looks very close to the
// border").
//
// THIS REPLACES AN EARLIER BAD EDIT that shrank height ONLY (a duplicate
// row-pair deleted from the solid belly) while leaving the full 32px
// width untouched -- i.e. a non-uniform squish that flattened the mark
// and distorted its aspect ratio. Traced from explicit user feedback on
// the sibling icon ("the logo of codex is ugly"), then confirmed for all
// three: "so yes we do maintain the proportion to the original logo...
// not flatten to get the height only."
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
// One extra step this icon needs and the other two must NOT get: the
// source mark is perfectly mirror-symmetric about its vertical axis
// (verified programmatically on the original array), but a 32->30 NEAREST
// resample is not mirror-preserving -- it drops two columns that aren't
// each other's partners, so the raw output had a 15px-wide odd body and
// legs differing by 1px, reading as a lean on a mark that tapers to a
// point. Fixed by OR-ing the resized grid with its own horizontal mirror
// about the ink bbox center (cols 1-30, so c <-> 31-c) after the resize.
// This cannot change the bounding box (it maps the bbox onto itself), so
// the 30w x 28h result and the exact 28/30 scale factor are untouched --
// re-verified after the fact, along with the two-peak notch still being
// open. Do NOT copy this step to CodexIcon.h or ZaiIcon.h: their marks
// (a ">_" glyph, a "Z") are asymmetric by design and mirroring would
// corrupt them.
//
// Consequence worth knowing: at a 28/30 scale a few rows no longer come
// in identical 2px pairs (the resample lands some steps on 1px). That is
// unavoidable when scaling proportionally to a non-multiple height, and
// is the accepted tradeoff -- correct proportions beat perfectly uniform
// 2px steps. Any earlier comment here claiming "every remaining row
// keeps its identical 2px partner" / "a plain removal, not a resample"
// described the bad edit and no longer applies. Verified via an ASCII
// dump of the final mask: the two-peak notch reads correctly.
#pragma once
#include <Arduino.h>

#define ANTIGRAVITY_ICON_SIZE 32

static const uint8_t kAntigravityIcon[128] PROGMEM = {
  0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x3F, 0xFC, 0x00,
  0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
  0x01, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0x80,
  0x01, 0xFC, 0x3F, 0x80, 0x01, 0xFC, 0x3F, 0x80, 0x01, 0xF0, 0x0F, 0x80,
  0x01, 0xF0, 0x0F, 0x80, 0x07, 0xC0, 0x03, 0xE0, 0x07, 0xC0, 0x03, 0xE0,
  0x07, 0x80, 0x01, 0xE0, 0x1F, 0x80, 0x01, 0xF8, 0x1F, 0x80, 0x01, 0xF8,
  0x1E, 0x00, 0x00, 0x78, 0x1E, 0x00, 0x00, 0x78, 0x78, 0x00, 0x00, 0x1E,
  0x78, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
