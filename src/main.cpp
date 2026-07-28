// smalltv-mod — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Three features, each a self-contained DisplayMode (see Mode.h), picked in the
// web UI and dispatched from the registry below:
//   - Ticker (features/ticker):  stock/crypto price, % change, sparkline.
//   - Usage  (features/usage):   Claude 5h/7d usage bars + animated mascot.
//   - Radar  (features/radar):   live ADS-B plane radar (compiled in when WITH_RADAR).
// Shared plumbing (WiFi, web UI, OTA, display core, settings) lives at src root.
//
// License: WTFPL
#include <Arduino.h>
#include "Platform.h"
#include "config.h"
#include "Settings.h"
#include "Net.h"
#include "Gfx.h"
#include "WebPortal.h"
#include "OtaUpdate.h"
#include "Mode.h"
#include "Clock.h"

#if WITH_TICKER
#include "TickerMode.h"
#endif
#if WITH_USAGE
#include "UsageMode.h"
#endif
#if WITH_RADAR
#include "RadarMode.h"
#endif
#if WITH_CALENDAR
#include "CalendarMode.h"
#include "ZaiMode.h"
#include "CalendarClient.h"   // zaiGet() for carouselHas()'s data-presence gate
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_TICKER
  &g_tickerMode,
#endif
#if WITH_USAGE
  &g_usageMode,
#endif
#if WITH_CALENDAR
  &g_zaiMode,
#endif
#if WITH_RADAR
  &g_radarMode,
#endif
#if WITH_CALENDAR
  &g_calendarAgendaMode,
  &g_calendarAgendaMode2,
  &g_calendarWeatherMode,
#endif
};
static const size_t kModeCount = sizeof(kModes) / sizeof(kModes[0]);

// ---- carousel -------------------------------------------------------------
// MODE_CAROUSEL rotates through the ticked features. Switches call wake() on
// the incoming mode: repaint from cached data, no refetch.
static size_t   g_carIdx = 0;
static uint32_t g_carSwitch = 0;

// g_carOrder is a permutation of kModes[] indices -- g_carIdx indexes INTO
// this, not into kModes[] directly, so carouselNext()/activeMode() walk the
// user's chosen order (web UI up/down arrows -> Settings.carouselOrder)
// instead of always the compiled-in registration order.
static size_t g_carOrder[kModeCount];

// Rebuild g_carOrder from s.carouselOrder (comma-separated mode id()s).
// Unlisted or no-longer-compiled ids are simply absent from the CSV and
// skipped; any kModes[] entry not mentioned is appended at the end in its
// original compiled order, so a firmware update that adds/removes a mode
// (or a device that's never saved a custom order -- carouselOrder=="")
// never loses a mode from rotation. Call after loadSettings() and again
// whenever settings are saved (appInvalidate()), since the order string
// itself can change on save.
static void rebuildCarouselOrder(const Settings& s) {
  bool used[kModeCount] = {false};
  size_t n = 0;
  const String& ord = s.carouselOrder;
  int start = 0;
  while (n < kModeCount && start <= (int)ord.length()) {
    int comma = ord.indexOf(',', start);
    String tok = (comma == -1) ? ord.substring(start) : ord.substring(start, comma);
    tok.trim();
    if (tok.length()) {
      for (size_t i = 0; i < kModeCount; i++) {
        if (!used[i] && tok.equals(kModes[i]->id())) {
          g_carOrder[n++] = i;
          used[i] = true;
          break;
        }
      }
    }
    if (comma == -1) break;
    start = comma + 1;
  }
  for (size_t i = 0; i < kModeCount; i++)
    if (!used[i]) g_carOrder[n++] = i;
}

static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
    // Skip ticker in the carousel until at least one symbol is configured —
    // otherwise it just shows an empty "No tickers" page every rotation.
    case MODE_STOCKS: return s.carouselTicker && s.ticker.symbolCount > 0;
    case MODE_USAGE:  return s.carouselUsage;
    case MODE_RADAR:  return s.carouselRadar;
    case MODE_CAL_AGENDA:  return s.carouselAgenda;
    // Only in rotation when there's actually a 4th-6th event to show --
    // same "skip until there's real content" pattern as ticker/z.ai above.
    case MODE_CAL_AGENDA2: return s.carouselAgenda2 && calendarGet().count > 3;
    case MODE_CAL_WEATHER: return s.carouselWeather;
#if WITH_CALENDAR
    // Same "skip until there's real content" pattern as ticker above --
    // stays out of the carousel until the daemon has a working z.ai key
    // configured and has actually pushed data at least once.
    case MODE_ZAI:    return s.carouselZai && zaiGet().valid;
#endif
    default:          return true;
  }
}

// Advance g_carIdx to the next ticked mode (stays put if none other is ticked).
// g_carIdx indexes g_carOrder, not kModes[] directly -- see g_carOrder's
// comment above.
static void carouselNext(const Settings& s) {
  for (size_t hop = 1; hop <= kModeCount; hop++) {
    size_t cand = (g_carIdx + hop) % kModeCount;
    size_t real = g_carOrder[cand];
    if (!carouselHas(s, kModes[real])) continue;
    if (cand != g_carIdx) {
      g_carIdx = cand;
      kModes[real]->wake(s);
    }
    return;
  }
}

static DisplayMode* activeMode(const Settings& s) {
  if (s.mode == MODE_CAROUSEL && kModeCount > 0) {
    if (g_carSwitch == 0) g_carSwitch = millis();
    if (!carouselHas(s, kModes[g_carOrder[g_carIdx]])) carouselNext(s);   // settings changed
    // Usage (Claude quota) gets double the dwell of every other mode --
    // e.g. 2min usage / 1min everything else at the 60s default.
    uint32_t dwellMs = (uint32_t)s.carouselSec * 1000UL;
    if (kModes[g_carOrder[g_carIdx]]->modeConst() == MODE_USAGE) dwellMs *= 2;
    if (millis() - g_carSwitch >= dwellMs) {
      g_carSwitch = millis();
      carouselNext(s);
    }
    return kModes[g_carOrder[g_carIdx]];
  }
  for (size_t i = 0; i < kModeCount; i++)
    if (kModes[i]->modeConst() == s.mode) return kModes[i];
  return kModeCount ? kModes[0] : nullptr;   // fall back to the first compiled mode
}

static Settings g_settings;
static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was an exception -> don't re-enter the crash
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)
#if HAS_LDR
static uint32_t g_lastAutoBr = 0;
static uint8_t  g_ldrCache   = DEFAULT_BRIGHTNESS;   // last LDR reading (2 s cadence)
#endif

// Single brightness resolver: night mode overrides auto-brightness overrides the
// manual level. Only writes the PWM when the effective target changes.
static uint8_t appEffectiveBrightness() {
  if (clockNightActive()) return g_settings.clock.nightLevel;
#if HAS_LDR
  if (g_settings.autoBrightness) {
    if (millis() - g_lastAutoBr > 2000) {
      g_lastAutoBr = millis();
      int raw = analogRead(LDR_PIN);
      g_ldrCache = (uint8_t)constrain(raw * 100 / ADC_MAX, 5, 100);
    }
    return g_ldrCache;
  }
#endif
  return g_settings.brightness;
}

void appApplyBrightness() {
  uint8_t t = appEffectiveBrightness();
  if ((int)t != g_lastBr) {
    g_lastBr = t;
    gfxSetBrightness(t, g_settings.backlightInverted);
  }
}

// Exposed to the web portal (/api/status) so the last reset reason is visible.
const char* appResetReason() { return g_resetReason.c_str(); }

// Called by the web portal after settings are applied: re-init every mode and
// force a fresh repaint so a mode/URL/symbol change takes effect immediately.
void appInvalidate() {
  // carouselOrder itself may have just changed -- rebuilding g_carOrder can
  // fully repermute it, which would leave g_carIdx pointing at a different
  // mode than the one actually on screen (activeMode() would then return
  // the wrong DisplayMode with no wake() ever called on it). Re-anchor
  // g_carIdx to whichever slot now holds the mode that was actually active
  // before the rebuild.
  DisplayMode* cur = kModeCount ? kModes[g_carOrder[g_carIdx]] : nullptr;
  rebuildCarouselOrder(g_settings);
  for (size_t i = 0; i < kModeCount; i++) {
    if (kModes[g_carOrder[i]] == cur) { g_carIdx = i; break; }
  }
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->invalidate(g_settings);
}

static void bootProgress(const char* msg) {
  gfxBoot("SmallTV", msg);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(FW_NAME " " FW_VERSION);

  // Capture why we (re)booted. On a reboot loop this is the key clue, and the
  // device's UART isn't exposed — so we also show it on screen below. On the
  // ESP8266 we also keep the crash PC (epc1) for addr2line decoding; the
  // ESP32-C2 (RISC-V) doesn't expose it, so epc/addr come back empty there.
  PlatformReset pr = platformResetInfo();
  Serial.print("[boot] reset reason: ");
  Serial.println(pr.reason);

  if (pr.wasCrash) {
    g_safeMode = true;                   // crashed last boot -> stay out of the crash path
    strlcpy(g_epcStr,  pr.epc,  sizeof(g_epcStr));
    strlcpy(g_addrStr, pr.addr, sizeof(g_addrStr));
    char rich[80];
    snprintf(rich, sizeof(rich), "%s epc %s addr %s", pr.reason.c_str(),
             g_epcStr[0] ? g_epcStr : "-", g_addrStr[0] ? g_addrStr : "-");
    g_resetReason = rich;
  } else {
    g_resetReason = pr.reason;
  }

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);
  rebuildCarouselOrder(g_settings);

  Serial.println("[boot] display");
  gfxBegin(g_settings);
  gfxBoot(g_safeMode ? "Crashed" : "SmallTV", FW_VERSION);

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that WiFi (STA) is up — but only if night mode is enabled, so a
  // ticker-only device doesn't pay the SNTP heap cost (which can starve the cash.ch
  // TLS handshake on the ESP8266). clockReapply arms it iff needed. Skipped after a
  // crash so a fault in here can't boot-loop before the web server starts (the
  // device then comes up in safe mode, OTA-recoverable, instead of needing UART).
  if (!g_safeMode) clockReapply(g_settings);

  // A GitHub update queued from the web UI runs now, before the features claim
  // the heap (the download needs a 16 KB TLS buffer that only fits at boot).
  // On success it reboots into the new image; a no-op stub on the ESP32 targets.
  if (otaBootRequested()) {
    Serial.println("[boot] github update");
    gfxBoot("SmallTV", "updating...");
    otaBootUpdate(g_settings);
    gfxBoot("SmallTV", "update failed");   // still here -> failed; details in the web UI
    delay(1200);
  }

  Serial.println("[boot] web");
  webPortalBegin(g_settings);

  Serial.println("[boot] modes");
  for (size_t i = 0; i < kModeCount; i++) kModes[i]->begin(g_settings);
  Serial.println("[boot] done");

  if (netMode() == NET_AP) {
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str());
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str());
    delay(3500);
  }
}

void loop() {
  netLoop();
  webPortalLoop();

  if (webPortalRebootDue()) {
    delay(120);
    ESP.restart();
  }

  if (g_safeMode) {
    delay(5);
    return;  // crashed last boot: web UI stays up for OTA recovery, no rendering
  }

  if (netMode() == NET_AP) {
    delay(5);
    return;  // setup mode: AP info stays on screen
  }

  // --- STA mode: the active feature fetches + renders itself ---

  // Night-mode state machine (NTP-trust gate), then apply the effective brightness
  // (night override / auto-brightness / manual level).
  clockService(g_settings);
  appApplyBrightness();

  DisplayMode* m = activeMode(g_settings);
  if (m) m->service(g_settings);

  delay(5);
}
