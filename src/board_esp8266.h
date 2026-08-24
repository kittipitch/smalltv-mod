// board_esp8266.h — pin map + panel quirks for the original GeekMagic SmallTV.
//   ESP-12F (ESP8266), 1.54" 240x240 ST7789 IPS over hardware SPI.
// Pin mapping confirmed from teardowns / ESPHome + Tasmota community configs.
#pragma once

// Display wiring (ST7789 240x240 over hardware SPI)
//   ESP8266 HW-SPI fixed pins: SCLK=GPIO14, MOSI=GPIO13.
#define TFT_SCLK   14   // D5  (HW SPI clock, fixed)
#define TFT_MOSI   13   // D7  (HW SPI data,  fixed)
#define TFT_DC      0   // D3  (data/command) — also a boot-strap pin
#define TFT_RST     2   // D4  (reset)        — also a boot-strap pin / onboard LED
// Chip select. GPIO15 on the original SmallTV, but community teardowns of the
// vendor rebrands (and the ESP32-C2 board) report the panel's CS tied straight
// to GND, with GPIO15 unused. Driving GPIO15 there is not merely pointless: it
// is a boot-strap pin, and a board without the usual external pulldown can be
// left sampling GPIO15 high at reset, which selects SDIO boot and the chip
// never runs the sketch — no display, no WiFi, unfixed by a power cycle.
// Build with -D SDPRO_CS_GND to pass GFX_NOT_DEFINED instead (see Gfx.cpp).
#ifdef SDPRO_CS_GND
#define TFT_CS     -1   // panel CS tied to GND; leave GPIO15 alone
#else
#define TFT_CS     15   // D8  (chip select)  — also a boot-strap pin
#endif
#define TFT_BL      5   // D1  (backlight, PWM capable)

// Panel colour order: this unit's ST7789 is wired RGB (0 = leave MADCTL RGB bit).
// Tested TFT_BGR=1 on this project's clone hardware — made colors worse
// (orange mascot turned purple), confirming RGB (0) is correct here too. The
// cool/blue cast on this unit is real but is not a color-order issue — see
// the tone()/usage.warmth+saturation live slider in
// features/usage/UsageMode.cpp for the actual fix applied.
#define TFT_BGR     0

// Backlight is active-low (ESPHome uses `inverted: true`). Runtime-overridable.
#define TFT_BL_DEFAULT_INVERTED true

// Ambient light sensor (LDR) on the ESP8266 ADC. Not all units populate it, but
// the pin exists; auto-brightness is opt-in via settings.
#define HAS_LDR     1
#define LDR_PIN    A0
#define ADC_MAX  1023   // ESP8266 ADC is 10-bit

// ---------------------------------------------------------------------------
// Board capability macros. SDPRO_CS_GND itself must not be tested anywhere
// outside this file and config.h -- every other file tests one of these
// instead. Added after OtaUpdate.cpp accumulated two separate #ifdef
// SDPRO_CS_GND blocks for two DIFFERENT reasons (SD PRO's is permanent
// hardware/asset-strategy, Ultra's is a temporary release-cadence gap), which
// then sat next to each other reading as one piece of historical cruft --
// exactly the shape a future cleanup could delete both at once by mistake
// (fable pre-flash-adjacent audit finding, 2026-08-25). Keeping both
// reasons here, side by side, is the fix: whoever changes one sees the other.
//
// BOARD_SELF_UPDATE: whether a real, board-matching GitHub release asset
// exists to safely pull via the web UI's self-updater.
//   SD PRO: PERMANENTLY 0. No asset for this board will ever exist upstream
//   -- SD PRO is this fork's own board, giovi321 doesn't build for it. The
//   generic ESP8266 asset is 4M1M/TFT_CS-15, and driving GPIO15 there is the
//   exact boot-strap brick that killed unit #1 (see TFT_CS above). Do not
//   flip this to 1 for any reason short of publishing a real SD PRO asset.
//   Ultra: 0 FOR NOW ONLY, not for the same reason. This fork simply has no
//   published (non-draft) release with a matching Settings/config.json
//   schema yet -- upstream's own asset resolves fine but is a different
//   codebase (see OtaUpdate.cpp's otaCheckLatest()). Flip this to 1 once
//   this fork ships a real matching-schema release. Flipping it does NOT
//   make SD PRO's case (above) safe -- that one stays 0 regardless.
#ifdef SDPRO_CS_GND
#define BOARD_SELF_UPDATE      0
#define BOARD_NO_UPDATE_REASON "self-update disabled on this board (SD PRO)"
#else
#define BOARD_SELF_UPDATE      0
#define BOARD_NO_UPDATE_REASON "self-update disabled: fork has no matching release yet"
#endif

// Default warmth/saturation sliders (Settings::toneB/toneSat). The SD PRO's
// panel is a different part from the Ultra's -- less saturated, no blue
// cast -- so it needs the opposite correction. Values found live via the
// display sliders and confirmed on the physical screen (unit .25,
// 2026-08-22); not derived from a datasheet.
#ifdef SDPRO_CS_GND
#define BOARD_TONE_B   100
#define BOARD_TONE_SAT 200
#else
#define BOARD_TONE_B    95   // slight default blue reduction -- cures the Ultra panel's blue cast
#define BOARD_TONE_SAT 100
#endif
