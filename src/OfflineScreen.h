// OfflineScreen.h — "is it us, or is it the daemon?" link classification, plus
// the centered full-screen notice shown while the DEVICE itself is offline.
//
// Why this exists: two completely different outages used to look identical on
// screen. The usage page falls back to the full-screen mascot whenever its data
// goes stale — and it went stale both when the WiFi dropped (device's fault) and
// when the daemon simply stopped pushing (host's fault). A 2026-09-08 outage was
// the second kind for 54 minutes while the daemon was healthy the whole time and
// only the device had left the network; nothing on screen said so.
//
// The device can already tell the two apart with what it has:
//   - netConnected()      — association state, instant and unambiguous
//   - usageFresh()        — a daemon still reaching us proves the link outright
//   - weatherFetchDiag()  — CalendarClient's device-direct Open-Meteo fallback
//                           doubles as an outbound-reachability probe
// so it should say which one it is instead of showing the same animation for
// both. See linkState() for the ordering and why each step is trusted.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "Settings.h"

// How recently a daemon push must have landed to count as proof of a live link.
// Generous next to the daemon's 30 s push cadence: the point is to never accuse
// a working device of being offline, not to detect a stalled feed quickly (the
// usage page's own staleness gate does that, on its own timescale).
#define LINK_PUSH_ALIVE_MS  120000UL

enum LinkState : uint8_t {
  LINK_OK = 0,       // associated, and nothing we can see says the internet is gone
  // Not associated: the link dropped and netLoop() is nudging it back. NOT the
  // AP fallback -- loop() returns on netMode() == NET_AP before the gate ever
  // runs, and the setup screen (SSID + password + URL) is the right thing to
  // show there anyway.
  LINK_NO_WIFI,
  LINK_NO_INTERNET,  // associated, but our own outbound fetches are failing
};

// Classify the device's own connectivity. Deliberately conservative: anything
// unproven reports LINK_OK, so a quiet daemon or an un-probeable device never
// gets accused of being offline.
LinkState linkState(const Settings& s);

// Full-screen centered "DEVICE OFFLINE" notice. `downMs` is how long we have
// been in this state. `full` repaints the whole screen (entry / state change);
// otherwise only the elapsed line is touched, so this can be called every loop.
void drawOfflineScreen(LinkState st, uint32_t downMs, bool full);

