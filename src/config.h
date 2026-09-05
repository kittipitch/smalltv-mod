// config.h — compile-time constants for smalltv-mod
//
// Hardware: three board variants, all a 1.54" 240x240 ST7789 IPS panel:
//   - Original GeekMagic SmallTV: ESP-12F (ESP8266)      [board_esp8266.h]
//   - Knockoff SmallTV:           ESP32-C2 / ESP8684      [board_esp32c2.h]
//   - NMMiner NM-TV-154:          classic ESP32 (WROOM-32E) [board_esp32.h]
// The board-specific pin map + panel quirks live in the board headers, selected
// below by the build-time target macro. Everything else here is shared.
#pragma once

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define FW_NAME     "smalltv-mod"
#define FW_VERSION  "1.0.0-kitt22"

// Project / update references (shown in the web UI; used by the GitHub self-update)
//
// REPO_URL is display-only (web UI footer + the Update tab's "the GitHub
// repo" hint, both set at runtime from /api/status's "repo" field — see
// webui.h's onstatus() handler) -- points students at this fork, where
// kittipitch/smalltv-mod's own tagged releases now live.
//
// UPDATE_REPO_OWNER/UPDATE_REPO_NAME are separate and feed OtaUpdate.cpp's
// self-update check -- these DO point at the fork now. They used to be the
// the now-removed REPO_OWNER/REPO_NAME (pointed at giovi321) on the theory that
// "this fork has no matching-schema release yet" -- that stopped being true
// once build.yml's release-attach step started publishing real per-board
// assets here (confirmed live: `gh api repos/kittipitch/smalltv-mod/
// releases/latest` lists smalltv-mod-firmware.bin, -sdpro.bin, -c2.bin,
// -esp32.bin, matching every UPDATE_ASSET name below). Leaving self-update
// pointed at upstream while it silently matched upstream's OWN
// smalltv-mod-firmware.bin was a real bug, not a safety feature -- it meant
// a stray BOARD_SELF_UPDATE=1 on a TLS-capable (non-slim) Ultra build would
// have downloaded and flashed VANILLA UPSTREAM firmware, silently wiping
// every fork feature (Codex/OpenRouter/z.ai/etc). Found live: "there are at
// lease two devices we have and if they fetch the wrong bit they will
// brick" / "make sure the fetch the correct bin from our mod and not from
// the upstream if click".
#define REPO_URL      "https://github.com/kittipitch/smalltv-mod"
#define UPDATE_REPO_OWNER "kittipitch"
#define UPDATE_REPO_NAME  "smalltv-mod"
// Release asset the GitHub self-updater pulls — one app image per target.
// Which physical board this image was built for. Reported by /api/status so a
// fleet can be told apart after flashing -- every target otherwise reports the
// same fw/version, and the SD PRO clone and the Ultra are easy to confuse (the
// MAC OUI has differed so far, but that is the module maker's prefix, not a
// product id, so it is a hint rather than a rule).
#if defined(SMALLTV_ESP32C2)
  #define FW_BOARD "esp32-c2"
#elif defined(SMALLTV_ESP32)
  #define FW_BOARD "esp32"
#elif defined(SDPRO_CS_GND)
  #define FW_BOARD "sdpro"
#else
  #define FW_BOARD "esp8266"
#endif

#if defined(SMALLTV_ESP32C2)
  #define UPDATE_ASSET "smalltv-mod-firmware-c2.bin"
#elif defined(SMALLTV_ESP32)
  #define UPDATE_ASSET "smalltv-mod-firmware-esp32.bin"
#elif defined(SDPRO_CS_GND)
  // This name USED TO be deliberately unpublished-anywhere (a nonexistent
  // upstream asset name, making the self-updater fail closed with "no
  // update available"). That stopped being a real safety net the moment
  // self-update was repointed at the fork (see UPDATE_REPO_OWNER above) --
  // kittipitch/smalltv-mod's own releases DO publish this exact name now.
  // The actual, still-enforced protection for this board is
  // BOARD_SELF_UPDATE=0 in board_esp8266.h (a hardware-safety gate,
  // permanent, independent of any repo/asset naming): flashing the generic
  // ESP8266 image (4M1M layout, TFT_CS 15) onto this board would
  // reintroduce the GPIO15 boot-strap brick mechanism that bricked unit #1
  // (codex pre-flash audit finding, 2026-08-22). Do not re-enable
  // BOARD_SELF_UPDATE for SD PRO on the assumption that this asset name is
  // itself a safety measure -- it no longer is.
  #define UPDATE_ASSET "smalltv-mod-firmware-sdpro.bin"
#else
  #define UPDATE_ASSET "smalltv-mod-firmware.bin"
#endif
#define GH_API_HOST   "api.github.com"
#define DAEMON_URL    "https://github.com/giovi321/clawdmeter-daemon"

// ---------------------------------------------------------------------------
// Display wiring + panel quirks — board-specific, pulled from the right header.
// Provides TFT_SCLK/MOSI/DC/RST/CS/BL, TFT_BGR, TFT_BL_DEFAULT_INVERTED,
// HAS_LDR/LDR_PIN/ADC_MAX. Both panels are 1.54" 240x240 ST7789 IPS.
// ---------------------------------------------------------------------------
#if defined(SMALLTV_ESP32C2)
  #include "board_esp32c2.h"
#elif defined(SMALLTV_ESP32)
  #include "board_esp32.h"
#else
  #include "board_esp8266.h"
#endif

#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ---------------------------------------------------------------------------
// Limits (bound RAM usage on the ESP8266)
// ---------------------------------------------------------------------------
#define MAX_WIFI_NETS     4    // saved WiFi networks; strongest visible wins at boot
#define MAX_NAME_LEN     20    // friendly name shown on screen
#define MAX_URL_LEN     200    // webhook base URL

// ---------------------------------------------------------------------------
// Display mode — what the device shows
//   1 = Claude usage meter (mascot + 5h/7d usage bars, fed by the daemon/)
//   2 = plane radar
//   3 = carousel: rotate through the ticked features on a timer
//   4 = next calendar event + weather/air quality
// ---------------------------------------------------------------------------
#define MODE_USAGE     1
#define MODE_RADAR     2
#define MODE_CAROUSEL  3
#define MODE_CALENDAR  4   // unused/unregistered -- superseded by the 3 modes below
#define MODE_CAL_AGENDA  5
#define MODE_CAL_WEATHER 6
#define MODE_CAL_AQI     7
#define MODE_ZAI         8
#define MODE_CAL_AGENDA2 9   // agenda's 2nd page (events 4-6) -- own carousel entry, auto-hidden
#define MODE_CODEX       10  // Codex CLI's real ChatGPT-plan quota, same shape as MODE_ZAI
#define MODE_ANTIGRAVITY 11  // Antigravity CLI's (agy) model quota, same shape as MODE_ZAI
#define MODE_CAL_FORECAST 12 // 3-day weather+AQI forecast, own carousel entry next to weather
                              // when there aren't more than 3 events (see carouselHas() in main.cpp)
#define MODE_OPENROUTER 13  // OpenRouter API $ spend, daemon-pushed, same shape as MODE_ANTIGRAVITY
#define DEFAULT_MODE MODE_CAROUSEL
#define DEFAULT_CAROUSEL_SEC 60      // per-mode dwell in carousel

// ---------------------------------------------------------------------------
// Compile-time feature toggles. All shipping features are on by default; a lean
// build drops one by setting e.g. -D WITH_RADAR=0 in a PlatformIO env, which
// omits that feature's module from the registry and its web UI section.
// (WITH_RADAR ships off until the radar module lands.)
// ---------------------------------------------------------------------------
#ifndef WITH_USAGE
#define WITH_USAGE 1
#endif
#ifndef WITH_RADAR
#define WITH_RADAR 1
#endif
#ifndef WITH_CALENDAR
#define WITH_CALENDAR 1
#endif

// TLS. BearSSL costs ~73 KB of flash, and on a device the daemon PUSHES to it
// buys nothing: the only remaining caller is a usage PULL from an https:// URL,
// and GitHub self-update (already compiled out on the SD PRO). Set -D WITH_TLS=0
// on a target whose flash budget is tight -- an https:// pull URL then fails
// cleanly with a logged reason instead of linking a stack it never runs.
#ifndef WITH_TLS
#define WITH_TLS 1
#endif

// Claude usage mode: once data stops arriving for this long (PC asleep, daemon
// stopped, network down) the screen switches from the stats to the idle mascot
// animation. Effective timeout also scales with the poll period (see main.cpp).
#define USAGE_STALE_GRACE_MS  20000UL

// ---------------------------------------------------------------------------
// Plane radar (MODE_RADAR)
//   Data source (radar's own selector, independent of the stock one):
//     0 = adsb.fi opendata, fetched directly by the device over HTTPS (no key)
//     1 = custom webhook (a LAN proxy that pre-filters — robust on the ESP8266)
// ---------------------------------------------------------------------------
#define RADAR_SRC_DIRECT   0
#define RADAR_SRC_WEBHOOK  1
#define DEFAULT_RADAR_SRC  RADAR_SRC_DIRECT

// adsb.fi free open-data endpoint (no API key; public rate limit ~1 req/s).
// Full path: /api/v3/lat/{lat}/lon/{lon}/dist/{nm}
#define ADSB_HOST        "opendata.adsb.fi"
#define ADSB_PATH        "/api/v3/lat/"
#define ADSB_USER_AGENT  "Mozilla/5.0 (SmallTV)"

// Bound RAM: nearest N aircraft kept/drawn, and a few home-area airports.
#define MAX_AIRCRAFT     24
#define MAX_AIRPORTS      6
#define MAX_ICAO_LEN      8      // ICAO ident + NUL (e.g. "LSZH")

// Defaults (lat/lon 0,0 is the "not set yet" sentinel -> shows a prompt).
#define DEFAULT_RADAR_LAT       0.0f
#define DEFAULT_RADAR_LON       0.0f
#define DEFAULT_RADAR_RANGE_KM  20
#define DEFAULT_RADAR_POLL_SEC  10     // >=3 keeps us under the 1 req/s limit

// ---------------------------------------------------------------------------
// Calendar + weather (MODE_CALENDAR)
//   Calendar is always pushed by clawdmeter-daemon (POST /api/calendar) — the
//   device never does Google OAuth itself.
//
//   Weather/AQI is pushed too (POST /api/weather), but ALSO fetches itself from
//   Open-Meteo when nothing is pushing (WITH_DEVICE_WEATHER, default on). It was
//   device-direct originally, moved server-side because it was hard to debug on
//   the ESP8266 (no serial access in this project's workflow); the fetch is back
//   as a FALLBACK only, so a daemon-fed device still behaves exactly as before
//   and keeps the daemon's logging. Plain HTTP, so it costs no BearSSL heap.
// ---------------------------------------------------------------------------

// Device-direct weather/AQI fetch, used only when nothing is pushing (see the
// note above). Must be defined HERE, not in CalendarClient.cpp: that file has
// #if guards near its top, and a #define further down evaluates as 0 there --
// silently compiling out the push-detection and leaving the fallback fighting
// a live daemon.
#ifndef WITH_DEVICE_WEATHER
#define WITH_DEVICE_WEATHER 1
#endif

// Defaults (lat/lon 0,0 is the "not set yet" sentinel, same convention as radar).
#define DEFAULT_CAL_LAT           0.0f
#define DEFAULT_CAL_LON           0.0f
#define DEFAULT_WEATHER_POLL_SEC  600     // 10 min; weather doesn't change fast

// ---------------------------------------------------------------------------
// Defaults (used on first boot / factory reset)
// ---------------------------------------------------------------------------
#define DEFAULT_AP_SSID      "SmallTV-Setup"
#define DEFAULT_AP_PASS      ""              // empty => open AP
#define DEFAULT_HOSTNAME     "smalltv"
#define DEFAULT_POLL_SEC      120            // how often to refresh data
#define DEFAULT_BRIGHTNESS    90             // 0..100 %
#define DEFAULT_HTTP_TIMEOUT  8000           // ms per request

// --- Clock / night mode (device-wide) ---
#define NTP_SERVER1             "pool.ntp.org"
#define NTP_SERVER2             "time.nist.gov"
#define DEFAULT_TZ_NAME         ""        // IANA display name; empty = UTC
#define DEFAULT_TZ_POSIX        "UTC0"    // POSIX TZ rule the device feeds SNTP
#define DEFAULT_NIGHT_ENABLED   false
#define DEFAULT_NIGHT_START_MIN 1320      // 22:00
#define DEFAULT_NIGHT_END_MIN   420       // 07:00
#define DEFAULT_NIGHT_LEVEL     0         // 0..100, 0 = backlight fully off

// Night-mode NTP trust: only ENTER night mode when the clock was confirmed by a
// successful NTP sync within NIGHT_NTP_TRUST_MS (else we assume the clock may be
// wrong and keep the screen on). While inside the window but unconfirmed, re-arm
// SNTP every NIGHT_NTP_RESYNC_MS until a fresh sync lands or the window ends
// (morning). Once night mode has switched on, it stays on until the window ends.
#define NIGHT_NTP_TRUST_MS      300000UL  // 5 min: max age of the sync that unlocks night
#define NIGHT_NTP_RESYNC_MS      30000UL  // re-sync attempt cadence while held off
