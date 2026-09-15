#include "config.h"          // FIRST: WITH_* live here and the guards below need them
#include "OfflineScreen.h"
#include "Gfx.h"
#include "Net.h"
#include "WebPortal.h"       // webPortalLastPushMs(): any daemon push proves the link
#include <Arduino_GFX_Library.h>

#if WITH_USAGE
#include "UsageClient.h"     // usageFresh() -- -iquote src/features/usage
#endif
#if WITH_CALENDAR && WITH_DEVICE_WEATHER
#include "CalendarClient.h"  // weatherFetchDiag() -- -iquote src/features/calendar
#endif

// ---------------------------------------------------------------------------
LinkState linkState(const Settings& s) {
  // 1. The link itself: no probe needed, if we are not associated we are
  //    certainly offline. (The AP fallback never reaches here -- loop() returns
  //    on netMode() == NET_AP and leaves the setup screen up.)
  if (!netConnected()) return LINK_NO_WIFI;

  // 2. A daemon that is still reaching us proves the link better than anything
  //    we could probe ourselves. Take ANY accepted push, not just the Claude
  //    one: a device whose usage feed alone has died is still demonstrably on
  //    the network while calendar/weather/z.ai pushes keep landing, and calling
  //    that "DEVICE OFFLINE" would be a lie the daemon could disprove.
  uint32_t pushMs = webPortalLastPushMs();
  if (pushMs && (millis() - pushMs) < LINK_PUSH_ALIVE_MS) return LINK_OK;

#if WITH_USAGE
  // Pull mode (usageUrl set) never receives a push, so the poll's own freshness
  // stands in for one. Same staleness window the usage page uses, so the two
  // agree by construction and the usage page can never mark itself stale while this
  // function still believes the daemon is alive.
  if (usageFresh((uint32_t)s.usage.pollSec * 1000UL * 2UL + USAGE_STALE_GRACE_MS))
    return LINK_OK;
#endif

#if WITH_CALENDAR && WITH_DEVICE_WEATHER
  // 3. Nothing from the daemon, so fall back to our own outbound fetch as the
  //    probe. weatherService() suppresses itself while pushes arrive, which is
  //    exactly why this is only consulted HERE -- after step 2 has established
  //    the daemon is already quiet. And because that suppression path rolls
  //    g_wxNextMs forward (CalendarClient.cpp), the first real attempt lands
  //    within a loop or two of the pushes stopping, not a full period later.
  int fc = 0, aq = 0, aqf = 0;
  uint32_t agoMs = 0;
  weatherFetchDiag(fc, aq, aqf, agoMs);

  // fc == 0 means never attempted: booting, or lat/lon unset so weatherService()
  // returns before trying. Unknown is not offline -- say nothing.
  if (fc == 0) return LINK_OK;

  // "Not 200" is NOT the same as "no internet", and treating it that way was a
  // real defect (codex, b6780be pre-flash audit): two HTTP 429/500 answers, or
  // two 200s whose bodies failed to parse (-100), all PROVE the device reached
  // the internet -- yet each would have seized the entire screen with DEVICE
  // OFFLINE after 30 s. Anything that came back from a server is evidence FOR
  // the link, not against it.
  //
  // So the verdict requires both halves to have failed WITHOUT ever receiving
  // an HTTP response -- and, since the two halves are separate hostnames and
  // both are now always attempted, those are two genuinely independent probes.
  //   -1   connection failed (DNS or connect never completed)
  //   -4   not connected
  //   -5   connection lost
  //   -11  read timeout: connected, but the server never answered
  // Everything else means reachable (any positive code, -100 parse failure or
  // whole-request deadline, -105 reached-but-empty body), local (-8 too little
  // RAM), or not attempted (0, -101, -102, -103, -106 date-rollover skip).
  //
  // -104 is deliberately ABSENT and must stay absent. It was the fail-fast skip
  // code, written into aq WITHOUT an air-quality attempt whenever the forecast
  // half failed; counting it here made ONE host failure satisfy a test that is
  // supposed to need TWO independent probes, so a forecast-host-only outage
  // seized the screen on an online device (codex, ac302d9 re-audit). The skip
  // itself is now gone from CalendarClient.cpp -- the air-quality GET always
  // runs, so aq always carries evidence from its own separate host. If a skip is
  // ever reintroduced there, its code must NOT be added to this list.
  //
  // -5 and -11 were REMOVED 2026-09-11. Traced in ESP8266HTTPClient.cpp: both
  // come out of handleHeaderResponse(), which runs only after connect()
  // succeeded and the request went out. A completed TCP handshake to a public
  // host IS internet, so counting them as "no response" turned a slow
  // Open-Meteo (>3 s to first header byte, well within normal) into a DEVICE
  // OFFLINE verdict on a demonstrably online device.
  //
  // -4 was removed on the same grounds 2026-09-11, after the same trace was
  // applied to it rather than assumed: HTTPC_ERROR_NOT_CONNECTED has exactly two
  // call sites in ESP8266HTTPClient.cpp, writeToPrint() (line 655) and
  // handleHeaderResponse() (line 983), and BOTH run only after connect()
  // succeeded -- they report a peer that accepted the connection and then hung
  // up, which is still proof we reached the internet. In this GET path a genuine
  // connect failure is always -1, so -1 alone is the honest test.
  auto noResponse = [](int code) {
    return code == -1;
  };

  // A failure also only counts while it is current: an old failure with no
  // attempt since means the probe itself stopped running, which proves nothing.
  // (weatherNotePush() additionally zeroes these the moment a push lands, so a
  // pre-push failure can no longer outlive the proof that we are online.)
  // Window comes from weatherService() itself, not from period*2. Once the
  // fetch backs off after repeated failures, attempts are further apart than
  // any fixed multiple of the poll period -- so a hard-coded window expires
  // mid-outage and the verdict silently flips back to LINK_OK while the device
  // is still offline.
  // Require a STREAK. The two halves are separate hostnames but share one DNS
  // resolver and one provider, so a single cycle failing both is a router
  // hiccup or a brownout, not two independent proofs. Worse, the first failure
  // also starts the backoff: at the default 600 s poll that scheduled the next
  // attempt 20 minutes out, so one bad cycle hid the usage page behind DEVICE
  // OFFLINE for twenty minutes on a working device.
  if (noResponse(fc) && noResponse(aq) && weatherFailStreak() >= 2 &&
      agoMs <= weatherAttemptWindowMs())
    return LINK_NO_INTERNET;
#endif

  return LINK_OK;
}

// ---------------------------------------------------------------------------
// "12s" / "34m" / "2h 05m". Deliberately short: these sit under a headline in
// the built-in 6x8 font, where anything longer stops fitting at text size 2.
static void fmtAge(char* out, size_t n, uint32_t ms) {
  uint32_t sec = ms / 1000UL;
  if (sec < 60) {
    snprintf(out, n, "%us", (unsigned)sec);
  } else if (sec < 3600) {
    snprintf(out, n, "%um", (unsigned)(sec / 60));
  } else {
    snprintf(out, n, "%uh %02um", (unsigned)(sec / 3600), (unsigned)((sec / 60) % 60));
  }
}

// Shared body for both notices. Two stacked words at text size 3 rather than one
// line: "DEVICE OFFLINE" is 14 chars = 252px at size 3, wider than the 240px
// panel, and dropping to size 2 to fit would make the headline smaller than the
// data it replaces.
//
// `full` draws everything; otherwise only the bottom line is repainted, so the
// caller can tick this every loop without flicker and without a full-screen
// blit fighting the SPI bus.
static void drawNotice(const char* word1, const char* word2, uint16_t titleColor,
                       const char* reason, const char* tailLabel, uint32_t ageMs,
                       bool full) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;

  if (full) {
    gfx->fillScreen(C_BLACK);
    gfxDrawCentered(word1, 62, 3, titleColor);
    gfxDrawCentered(word2, 96, 3, titleColor);
    if (reason && reason[0]) gfxDrawCentered(reason, 140, 2, C_GRAY);
  }

  char age[16];
  fmtAge(age, sizeof(age), ageMs);
  char tail[32];
  snprintf(tail, sizeof(tail), "%s %s", tailLabel, age);

  // Past the first minute fmtAge only changes once a minute, so repaint on the
  // rendered STRING changing rather than on every tick -- otherwise the caller's
  // 5 s cadence blits this band ~11 times per minute for no visible difference.
  //
  // LOAD-BEARING: one cache shared by BOTH notices, which is only safe because
  // every entry into either one passes full=true (main.cpp's gate sets
  // full = !g_offlineShown || state changed; UsageMode sets full = !showingDaemonOff_),
  // and full bypasses the compare below. A future caller that ticks with
  // full=false on first entry would have its opening paint silently skipped --
  // give the notices separate cache slots before adding one.
  static char s_lastTail[32] = "";
  if (!full && strcmp(tail, s_lastTail) == 0) return;
  strlcpy(s_lastTail, tail, sizeof(s_lastTail));

  // Clear just this line's band before redrawing: the elapsed text shrinks
  // ("10m" after "9m 59s"-worth of width) and the opaque-glyph overwrite alone
  // would leave the tail of the longer previous string on screen.
  gfx->fillRect(0, 172, TFT_WIDTH, 18, C_BLACK);
  gfxDrawCentered(tail, 174, 2, C_DGRAY);
}

void drawOfflineScreen(LinkState st, uint32_t downMs, bool full) {
  const char* reason = (st == LINK_NO_WIFI) ? "no wifi" : "no internet";
  drawNotice("DEVICE", "OFFLINE", C_RED, reason, "offline", downMs, full);
  // On LINK_NO_INTERNET we are still associated, so the address we are
  // associated AT is the one fact that makes the screen actionable -- it says
  // which network to go look at. The AP-info screen prints IP/MAC for the same
  // reason. Nothing to print when the association itself is gone.
  if (full && st == LINK_NO_INTERNET) {
    String ip = netIP();
    if (ip.length()) gfxDrawCentered(ip.c_str(), 202, 2, C_DGRAY);
  }
}

