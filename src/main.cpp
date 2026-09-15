// smalltv-mod — custom firmware for the GeekMagic SmallTV (ESP-12F / ESP8266)
//
// Each feature is a self-contained DisplayMode (see Mode.h), picked in the
// web UI and dispatched from the registry below:
//   - Radar  (features/radar):   live ADS-B plane radar (compiled in when WITH_RADAR).
//   - Usage  (features/usage):   Claude 5h/7d usage bars + animated mascot.
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

#if WITH_USAGE
#include "UsageMode.h"
#endif
#if WITH_RADAR
#include "RadarMode.h"
#endif
#if WITH_CALENDAR
#include "CalendarMode.h"
#include "ZaiMode.h"
#include "CodexMode.h"
#include "AntigravityMode.h"
#include "OpenRouterMode.h"
#include "CalendarClient.h"   // zaiGet()/codexGet()/antigravityGet()/openrouterGet() for carouselHas()'s data-presence gate
#endif
#include "AlbumMode.h"
#include "OfflineScreen.h"    // linkState()/drawOfflineScreen() for the link gate in loop()
#if WITH_USAGE
#include "UsageClient.h"      // usageService(): kept alive under the link gate (pull mode)
#endif

// ---- mode registry --------------------------------------------------------
// The compiled-in features, in display order. main.cpp holds no per-feature
// state of its own — each mode owns its fetch/render/dirty tracking.
static DisplayMode* kModes[] = {
#if WITH_USAGE
  &g_usageMode,
#endif
#if WITH_CALENDAR
  &g_zaiMode,
  &g_codexMode,
  &g_antigravityMode,
  &g_openrouterMode,
#endif
#if WITH_RADAR
  &g_radarMode,
#endif
#if WITH_ALBUM
  &g_albumMode,
#endif
#if WITH_CALENDAR
  &g_calendarAgendaMode,
  &g_calendarAgendaMode2,
  &g_calendarWeatherMode,
  &g_calendarForecastMode,
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
        // MODE_CAL_AGENDA2 is deliberately never placed from the saved CSV --
        // it's forced to sit immediately after MODE_CAL_AGENDA below,
        // unconditionally, regardless of what any saved/hand-crafted
        // carouselOrder string says. There's no web UI affordance to move
        // it independently either -- "we shudnt have ppl able to order
        // them apart" (the two agenda pages are one logical unit split
        // only because a single screen can't show 6 cards at once).
        if (kModes[i]->modeConst() == MODE_CAL_AGENDA2) continue;
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
    if (!used[i] && kModes[i]->modeConst() != MODE_CAL_AGENDA2) g_carOrder[n++] = i;

  // Force MODE_CAL_AGENDA2 to sit right after MODE_CAL_AGENDA's slot (or at
  // the end if agenda somehow isn't compiled in/present -- shouldn't happen,
  // both are gated on WITH_CALENDAR together).
  for (size_t i = 0; i < kModeCount; i++) {
    if (kModes[i]->modeConst() != MODE_CAL_AGENDA2) continue;
    size_t insertAt = n;
    for (size_t j = 0; j < n; j++) {
      if (kModes[g_carOrder[j]]->modeConst() == MODE_CAL_AGENDA) { insertAt = j + 1; break; }
    }
    for (size_t j = n; j > insertAt; j--) g_carOrder[j] = g_carOrder[j - 1];
    g_carOrder[insertAt] = i;
    n++;
    break;
  }
}

static bool carouselHas(const Settings& s, const DisplayMode* m) {
  switch (m->modeConst()) {
    case MODE_USAGE:  return s.carouselUsage;
#if WITH_ALBUM
    // Deliberately NOT gated on "has a frame yet", unlike z.ai/Codex below.
    // The daemon only pushes when this page is due, and it learns when that is
    // from the reply to its own push -- so gating the page on already having a
    // frame would mean it never enters rotation and never gets one.
    case MODE_ALBUM:  return s.carouselAlbum;
#endif
    case MODE_RADAR:  return s.carouselRadar;
    case MODE_CAL_AGENDA:  return s.carouselAgenda;
    // Only in rotation when there's actually a 4th-6th event to show --
    // same "skip until there's real content" pattern as z.ai below.
    case MODE_CAL_AGENDA2: return s.carouselAgenda2 && calendarGet().count > 3;
    case MODE_CAL_WEATHER: return s.carouselWeather;
    case MODE_CAL_FORECAST: return s.carouselForecast;
#if WITH_CALENDAR
    // Same "skip until there's real content" pattern as the others --
    // stays out of the carousel until the daemon has a working z.ai key
    // configured and has actually pushed data at least once.
    case MODE_ZAI:    return s.carouselZai && zaiGet().valid;
    // Same "skip until there's real content" pattern -- stays out of the
    // carousel until the daemon has a working OpenAI key configured and has
    // actually pushed data at least once.
    case MODE_CODEX:  return s.carouselCodex && codexGet().valid;
    // Same "skip until there's real content" pattern -- stays out of the
    // carousel until the daemon has agy authenticated and has actually
    // pushed data at least once.
    case MODE_ANTIGRAVITY: return s.carouselAntigravity && antigravityGet().valid;
    case MODE_OPENROUTER: return s.carouselOpenrouter && openrouterGet().valid;
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

// Standalone Agenda page flip (agenda <-> agenda2 every carouselSec) -- state
// for the logic inside activeMode(); see the comment there.
static uint32_t g_soloSwitch = 0;
static bool g_soloPage2 = false;

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
    if (kModes[i]->modeConst() == s.mode) {
      // Standalone Agenda ("Next event") has a second page (events 4-6) that
      // used to be reachable only in the carousel (where it sits right after
      // agenda, see rebuildCarouselOrder). Flip between the two pages every
      // carouselSec when page 2 is ticked and there is something on it, so
      // "there are two pages" holds outside the carousel too.
      if (s.mode == MODE_CAL_AGENDA && s.carouselAgenda2) {
        if (g_soloSwitch == 0) g_soloSwitch = millis();
        if (calendarGet().count > 3) {
          if (millis() - g_soloSwitch >= (uint32_t)s.carouselSec * 1000UL) {
            g_soloSwitch = millis();
            g_soloPage2 = !g_soloPage2;
          }
        } else {
          g_soloPage2 = false;              // page 2 empty: always answer page 1
        }
        if (g_soloPage2) {
          for (size_t j = 0; j < kModeCount; j++)
            if (kModes[j]->modeConst() == MODE_CAL_AGENDA2) return kModes[j];
        }
      }
      return kModes[i];
    }
  return kModeCount ? kModes[0] : nullptr;   // fall back to the first compiled mode
}

static Settings g_settings;

#if WITH_ALBUM
// Dwell of one slot, mirroring activeMode()'s own rule (usage gets double).
static uint32_t carouselDwellMs(const Settings& s, size_t real) {
  uint32_t d = (uint32_t)s.carouselSec * 1000UL;
  if (kModes[real]->modeConst() == MODE_USAGE) d *= 2;
  return d;
}

// Seconds until the album page NEXT starts -- strictly in the future, so a push
// made while the page is already up is answered with a full cycle rather than
// 0, which would spin the daemon. -1 = the page is not in rotation at all.
// This is the whole synchronisation mechanism: the daemon sleeps on this number
// instead of polling, and every push re-reads it, so drift corrects itself.
int albumSecsToNextSlot() {
  const Settings& s = g_settings;
  if (kModeCount == 0) return -1;
  if (s.mode != MODE_CAROUSEL) return (s.mode == MODE_ALBUM) ? (int)s.carouselSec : -1;
  if (!s.carouselAlbum) return -1;

  size_t cur = g_carOrder[g_carIdx];
  uint32_t d0 = carouselDwellMs(s, cur);
  uint32_t elapsed = millis() - g_carSwitch;
  uint32_t ms = (elapsed >= d0) ? 0 : (d0 - elapsed);   // rest of the current slot

  for (size_t hop = 1; hop <= kModeCount; hop++) {
    size_t real = g_carOrder[(g_carIdx + hop) % kModeCount];
    if (!carouselHas(s, kModes[real])) continue;
    if (kModes[real]->modeConst() == MODE_ALBUM) return (int)((ms + 999) / 1000);
    ms += carouselDwellMs(s, real);
  }
  // Album is the only ticked page: it restarts as soon as this slot ends.
  return (int)((ms + 999) / 1000);
}

// True while the album page is the one on screen -- the upload path paints only
// then, so a frame that lands mid-carousel cannot scribble over another page.
bool albumPageIsUp() {
  if (kModeCount == 0) return false;
  if (g_settings.mode == MODE_ALBUM) return true;
  if (g_settings.mode != MODE_CAROUSEL) return false;
  return kModes[g_carOrder[g_carIdx]]->modeConst() == MODE_ALBUM;
}
#endif

static String   g_resetReason;        // why the chip last reset (diagnostics)
static bool     g_safeMode = false;   // last reset was an exception -> don't re-enter the crash
static char     g_epcStr[16] = "";
static char     g_addrStr[16] = "";
static int g_lastBr = -1;        // last effective brightness written (-1 = none yet)

// ---- link gate ------------------------------------------------------------
// While the DEVICE itself is offline, every page is showing numbers that stopped
// being true, so the offline notice takes over the whole screen instead of any
// mode's render (see the loop() gate). A daemon-only outage does NOT come here:
// that is a per-page state the usage page draws itself, because the other pages'
// data may still be valid.
static uint32_t  g_linkDownSince   = 0;      // 0 = link OK; else millis() the drop began
static bool      g_offlineShown    = false;  // the notice currently owns the screen
static LinkState g_offlineState    = LINK_OK;// which notice is on screen (wifi vs internet)
static uint32_t  g_offlineNextMs   = 0;      // next elapsed-line repaint
// Ride out brief reconnects: the SDK re-associates on its own within seconds, and
// netLoop() only rotates to another saved SSID after a 2-minute dwell, so a
// wobble that heals itself must not flash a scare screen.
#define OFFLINE_GRACE_MS   30000UL
#define OFFLINE_TICK_MS    5000UL
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
  // A settings save can rotate the panel or change the colour tone under a
  // notice that is currently on screen. The notice tracks its own "already
  // painted" state, so without this it would keep doing partial elapsed-line
  // updates over a framebuffer that no longer matches -- the same staleness
  // the modes handle via their invalidate(). Dropping the flag forces the next
  // tick to repaint it in full.
  g_offlineShown = false;
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

// ---------------------------------------------------------------------------
// Display-init guard. Survives a reset in RTC memory (not cleared by a warm
// boot, wiped by a power cycle — which is the behaviour we want: pulling the
// plug gives the panel a fresh chance).
// ---------------------------------------------------------------------------
static bool g_gfxSkipped = false;

#if defined(ESP8266)
static const uint32_t kGfxGuardMagic = 0x47465831;  // "GFX1"
static const uint32_t kGfxGuardSlot  = 0;           // 4-byte block index

static bool gfxGuardTripped() {
  uint32_t v = 0;
  if (!ESP.rtcUserMemoryRead(kGfxGuardSlot, &v, sizeof(v))) return false;
  return v == kGfxGuardMagic;
}
static void gfxGuardArm() {
  uint32_t v = kGfxGuardMagic;
  ESP.rtcUserMemoryWrite(kGfxGuardSlot, &v, sizeof(v));
}
static void gfxGuardClear() {
  uint32_t v = 0;
  ESP.rtcUserMemoryWrite(kGfxGuardSlot, &v, sizeof(v));
}
#else
// Other targets flash over USB, so an unreachable device is a nuisance rather
// than a case-opening. Keep the boot path identical to before.
static bool gfxGuardTripped() { return false; }
static void gfxGuardArm() {}
static void gfxGuardClear() {}
#endif

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
    // Append the RTC crash log if the postmortem hook managed to store one.
    // This is the part that makes a repeat diagnosable: epc + addr alone were
    // not enough to name the caller for the 2026-09-10 crash, because the epc
    // landed in ROM memcpy, which ~490 call sites reach. The stack words are
    // return addresses -- run them through addr2line against THIS build's elf.
    // Consume it: g_resetReason keeps the text for the life of this boot (that is
    // what /api/status serves), so clearing the record leaves no stale copy for a
    // later boot to mistake for its own.
    { String cl;
      if (platformCrashLogRead(cl)) { g_resetReason += " | "; g_resetReason += cl; }
      platformCrashLogClear(); }
  } else {
    g_resetReason = pr.reason;
  }

  Serial.println("[boot] settings");
  settingsBegin();
  loadSettings(g_settings);
  rebuildCarouselOrder(g_settings);

  // Display init runs before the network, so a fault in here costs the whole
  // device: no screen AND no way in, on hardware whose UART header needs the
  // case opened. The guard below breaks that: we mark RTC memory immediately
  // before touching the panel and clear it immediately after. A boot that
  // finds the mark still set knows the previous attempt never returned —
  // crash, hang, or strap-pin fault — and skips the display entirely, coming
  // up headless but with WiFi and the web portal live. Every gfx* call is
  // null-guarded, so the rest of the firmware treats a missing display as a
  // no-op. Dark but OTA-reachable beats unreachable.
  //
  // Deliberately narrower than g_safeMode: an ordinary crash elsewhere still
  // gets its on-screen reset reason, which is the only diagnostic these units
  // have. Only a fault *inside display init* costs the screen, and only for
  // one boot — the mark is cleared on the way through, so the next boot tries
  // the panel again.
  if (gfxGuardTripped()) {
    Serial.println("[boot] display SKIPPED — previous boot faulted during display init");
    g_gfxSkipped = true;
  } else {
    Serial.println("[boot] display");
    gfxGuardArm();
    gfxBegin(g_settings);
    gfxGuardClear();
    gfxBoot(g_safeMode ? "Crashed" : "SmallTV", FW_VERSION);
  }

  Serial.println("[boot] net");
  netBegin(g_settings, bootProgress);
  // Arm SNTP now that WiFi (STA) is up. Skipped after a crash so a fault in here
  // can't boot-loop before the web server starts (the device then comes up in
  // safe mode, OTA-recoverable, instead of needing UART).
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
    gfxApInfo(g_settings.apSsid.c_str(), g_settings.apPass.c_str(), netIP().c_str(), netMAC().c_str());
  } else if (g_safeMode) {
    // Last boot crashed: show the crash address (persistent) and keep the web
    // server up for OTA recovery — don't enter the render path that crashed.
    gfxCrash(g_epcStr, g_addrStr, netIP().c_str());
  } else {
    // Show which network we joined and how to reach the web UI, long enough to read.
    gfxStaInfo(netSSID().c_str(), netIP().c_str(), g_settings.hostname.c_str(), netMAC().c_str());
    delay(3500);
  }
}

void loop() {
  netLoop();
  webPortalLoop();
  // Before the safe-mode return on purpose: a unit that has already crashed once
  // is exactly the one whose next crash we most need the run-up to.
  platformHealthTick();

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

#if WITH_CALENDAR
  // Weather/AQI keeps itself fresh regardless of which page is showing, so a
  // device with no daemon has data ready when the carousel reaches it. Cheap:
  // it returns immediately unless its own poll period has elapsed AND pushes
  // have gone quiet.
  weatherService(g_settings);
#endif

  // --- link gate: does the DEVICE still have a network? --------------------
  // Deliberately AFTER weatherService(): that fetch is the outbound probe
  // linkState() reads, so it has to keep running while the notice is up --
  // otherwise a "no internet" verdict could never clear itself.
  LinkState link = linkState(g_settings);
  if (link != LINK_OK) {
    if (!g_linkDownSince) g_linkDownSince = millis();
    uint32_t downMs = millis() - g_linkDownSince;
    // `|| g_offlineShown` latches ownership: downMs is a rollover-safe unsigned
    // subtraction, but an outage spanning a full 49.7-day millis() cycle makes it
    // wrap back under the grace threshold. Without the latch the notice would
    // stay on screen while the modes started rendering over it again -- mixed
    // pixels, and no repaint of either. Once the notice owns the screen it keeps
    // it until the link actually returns.
    if (downMs >= OFFLINE_GRACE_MS || g_offlineShown) {
      bool full = !g_offlineShown || link != g_offlineState;
      if (full || (int32_t)(millis() - g_offlineNextMs) >= 0) {
        drawOfflineScreen(link, downMs, full);
        g_offlineShown  = true;
        g_offlineState  = link;
        g_offlineNextMs = millis() + OFFLINE_TICK_MS;
      }
#if WITH_USAGE
      // Pull mode polls the daemon from UsageMode::service(), which the return
      // below skips -- so keep the poll itself alive here. Without this a
      // pull-mode device that put the notice up could never take it down again:
      // it receives no pushes to prove the link, and its only other escape is
      // the weather probe, up to a full weatherPollSec away.
      if (g_settings.usage.usageUrl.length() >= 8) usageService(g_settings);
#endif
      delay(5);
      return;   // the notice owns the screen: no mode render, no carousel advance
    }
  } else {
    g_linkDownSince = 0;
    if (g_offlineShown) {
      // Back online. The notice painted over whatever page was showing, so the
      // active mode must repaint from cached data -- the same path a carousel
      // switch already uses. Restart the dwell FIRST: activeMode() advances the
      // carousel when the dwell has elapsed, and after a long outage it always
      // has, which would skip a page the instant the link returned.
      g_offlineShown = false;
      g_carSwitch = millis();
      DisplayMode* back = activeMode(g_settings);
      if (back) back->wake(g_settings);
    }
  }

  DisplayMode* m = activeMode(g_settings);
  if (m) m->service(g_settings);

  delay(5);
}
