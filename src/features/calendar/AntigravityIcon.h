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
// border"). An exact 26px match would've required resampling (PIL
// NEAREST), which shaved individual diagonal rows unevenly -- some legs
// 1px, some 2px -- breaking the uniform 2x2 blockiness this icon (and
// Codex's) was deliberately built around. Fixed instead by deleting one
// duplicate row-pair (rows 6-7 of 30) from the solid 6-row belly
// (0x00,0xFF,0xFF,0x00 x6, now x4) -- a plain removal, not a resample,
// so every remaining row keeps its identical 2px partner. Lands at 28px
// (2px taller than the mascot, not an exact match, but every row still
// steps in clean 2px units) -- the explicit tradeoff picked over an
// uneven 26px. Verified via an ASCII dump before/after: the two-peak
// notch and every other row are untouched, only the belly got shorter.
#pragma once
#include <Arduino.h>

#define ANTIGRAVITY_ICON_SIZE 32

static const uint8_t kAntigravityIcon[128] PROGMEM = {
  0x00, 0x0F, 0xF0, 0x00, 0x00, 0x0F, 0xF0, 0x00, 0x00, 0x3F, 0xFC, 0x00,
  0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x3F, 0xFC, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x03, 0xFF, 0xFF, 0xC0, 0x03, 0xFF, 0xFF, 0xC0,
  0x03, 0xFF, 0xFF, 0xC0, 0x03, 0xFF, 0xFF, 0xC0, 0x03, 0xFC, 0x3F, 0xC0,
  0x03, 0xFC, 0x3F, 0xC0, 0x03, 0xF0, 0x0F, 0xC0, 0x03, 0xF0, 0x0F, 0xC0,
  0x0F, 0xC0, 0x03, 0xF0, 0x0F, 0xC0, 0x03, 0xF0, 0x0F, 0x00, 0x00, 0xF0,
  0x0F, 0x00, 0x00, 0xF0, 0x3F, 0x00, 0x00, 0xFC, 0x3F, 0x00, 0x00, 0xFC,
  0x3C, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x3C, 0xF0, 0x00, 0x00, 0x0F,
  0xF0, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
