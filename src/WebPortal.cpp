#include "WebPortal.h"
#include "OfflineScreen.h"   // linkState() for the /api/status "link" field
#include "Platform.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "webui.h"
#include "Net.h"
#include "Gfx.h"
#include "OtaUpdate.h"
#include "UsageClient.h"
#include "Clock.h"
#include "OomTrap.h"
#if WITH_ALBUM
#include "AlbumMode.h"
#endif
#if WITH_CALENDAR
#include "CalendarClient.h"
#endif

// Defined in main.cpp — re-init every mode + force a repaint after a config change.
extern void appInvalidate();
extern const char* appResetReason();   // last reset reason (diagnostics)
extern void appApplyBrightness();   // main.cpp: re-resolve effective brightness now

static WebServerClass server(80);
static Settings*        S = nullptr;
static bool             g_reboot = false;
static uint32_t         g_rebootAt = 0;
static bool             g_selfUpdate = false;   // GitHub self-update requested
static String           g_updateMsg;            // last self-update status/error

static void scheduleReboot(uint32_t inMs) {
  g_reboot = true;
  g_rebootAt = millis() + inMs;
}

// Source-IP filter for the daemon's data-push endpoints only (/api/usage,
// /api/calendar, /api/weather, /api/zai, /api/codex, /api/antigravity,
// /api/openrouter).
// NOT a security boundary (plaintext HTTP, no auth) — just catches a daemon
// accidentally pointed at the wrong device. Empty stored IP (the default)
// means accept a push from anywhere.
// millis() of the last accepted daemon push, 0 = none since boot. Recorded here
// rather than in each *Apply() parser on purpose: ANY request that reaches a
// push endpoint from the right source proves the daemon can still reach us, even
// if its payload turns out to be malformed. main.cpp's link gate uses that as
// proof of link -- see webPortalLastPushMs().
static uint32_t g_lastPushMs = 0;

static bool checkDaemonIp() {
  if (S->daemonIp.length() == 0) { g_lastPushMs = millis(); return true; }
  IPAddress want;
  if (!want.fromString(S->daemonIp)) { g_lastPushMs = millis(); return true; }  // unparsable => don't filter
  if (server.client().remoteIP() != want) return false;
  g_lastPushMs = millis();
  return true;
}

uint32_t webPortalLastPushMs() { return g_lastPushMs; }

static void sendWrongSource() {
  server.send(403, "application/json", "{\"ok\":false,\"error\":\"unexpected source IP\"}");
}

// ---------------------------------------------------------------------------
static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

// Setup-hotspot page. Over the SmallTV-Setup AP the full UI (~58 KB) arrives
// cut off at 2-8 KB (75dd 2026-09-14, f661 2026-09-15), so its script never
// runs and WiFi cannot be saved. This page has no script and stays around
// 1 KB (~1.4 KB with the scan list) -- a few 536-byte TCP segments (this build's
// lwIP MSS; the header is written separately), which have arrived intact over
// the same hotspot. Same colours as the full UI. /?full=1 still serves the
// full UI in AP mode.
static const char SETUP_CSS[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html><meta name=viewport content="width=device-width"><title>SmallTV</title><style>*{box-sizing:border-box;font:16px sans-serif;color:#e6edf3}body{background:#0e1116;max-width:360px;margin:auto;padding:16px}input,button,.b{display:block;width:100%;margin:8px 0;padding:10px;border:1px solid #333;border-radius:8px;background:#171c24;text-align:center}button{background:#3fb950;color:#000}.n{display:flex;justify-content:space-between;background:#171c24;color:#e6edf3}i,small{color:#8b96a5}</style>)HTMLPAGE";

static const char SETUP_FORM_HEAD[] PROGMEM = R"HTMLPAGE(<p>SmallTV WiFi<form method=post action=/wifi><input name=ssid placeholder="Network (2.4 GHz)" maxlength=32 required autocapitalize=none value=")HTMLPAGE";

static const char SETUP_FORM_TAIL[] PROGMEM = R"HTMLPAGE("><input name=pass type=password placeholder=Password maxlength=64><button>Save and connect</button></form><a class=b href="/?scan=1">Scan networks (up to 20 s)</a><small>Saved networks are kept. <a href="/?full=1">Full settings</a></small>)HTMLPAGE";

static const char SETUP_SAVED_HTML[] PROGMEM = R"HTMLPAGE(<p>Saved. The device reboots and joins that network; reconnect to it there.</p>)HTMLPAGE";

// SSIDs come from the air: anyone nearby can name a network "<script>...".
static void htmlEsc(String& out, const String& in) {
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if      (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else               out += c;
  }
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache");
  if (netMode() != NET_AP || server.hasArg("full")) {
    server.send_P(200, "text/html", WEBUI_HTML);
    return;
  }
  // Reserve above the scan worst case once (6 rows of 32-char SSIDs): growing
  // the String mid-scan on a ~24 KB heap is the allocation churn to avoid.
  String p;
  p.reserve(2400);
  p += FPSTR(SETUP_CSS);
  p += FPSTR(SETUP_FORM_HEAD);
  htmlEsc(p, server.arg("s"));            // picked from the scan list
  p += FPSTR(SETUP_FORM_TAIL);
  if (server.hasArg("scan")) {
    // Blocking -- 2.6 s typical, 20 s on the first scan in AP mode (measured
    // on f661) -- so only on an explicit press, never on page load. The list
    // comes after the form: if it is cut off, saving still works. Only the
    // 6 strongest: AP-mode pages have been cut off from 2,002 B, and 6 rows
    // keep even 32-char SSIDs under that (10 rows reached 2,321 B).
    int n = WiFi.scanNetworks();
    p += F("<form action=/>");
    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (!ssid.length()) continue;         // hidden network
      int stronger = 0;                     // ponytail: O(n^2) rank, n <= ~30
      for (int j = 0; j < n; j++)
        if (WiFi.RSSI(j) > WiFi.RSSI(i) || (WiFi.RSSI(j) == WiFi.RSSI(i) && j < i)) stronger++;
      if (stronger >= 6) continue;
      p += F("<button class=n name=s value=\"");
      htmlEsc(p, ssid);
      p += F("\">");
      htmlEsc(p, ssid);
      p += F("<i>");
      p += WiFi.RSSI(i);
      p += F("</i></button>");
    }
    WiFi.scanDelete();
    p += F("</form>");
    if (n <= 0) p += F("<small>No networks found.</small>");
  }
  server.send(200, "text/html", p);
}

static void handleGetConfig() {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, /*includeSecrets=*/false);
  // Which features are compiled in (so a lean build hides the tabs it dropped).
  JsonObject feat = root["features"].to<JsonObject>();
  feat["usage"]  = (bool)WITH_USAGE;
  feat["radar"]  = (bool)WITH_RADAR;
  feat["calendar"] = (bool)WITH_CALENDAR;
  feat["tls"]      = (bool)WITH_TLS;
  // Which chip this build runs on (the UI warns about per-chip limitations).
#if defined(SMALLTV_ESP32C2)
  root["chip"] = "esp32c2";
#elif defined(SMALLTV_ESP32)
  root["chip"] = "esp32";
#else
  root["chip"] = "esp8266";
#endif
  sendJson(doc);
}

static void handleStatus() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["fw"] = FW_NAME;
  o["version"] = FW_VERSION;
  // The BUILD, not the release. FW_VERSION only moves when a tag is cut, so many
  // different images report the same string -- on 2026-09-11 a unit reporting
  // 1.0.0-kitt23 was serving a field that does not exist in that tag, and a full
  // audit went at code the hardware was not running. A trailing '+' means the
  // tree was dirty, so the SHA is where it came from, not what it is.
  o["sha"] = GIT_SHA;
  o["board"] = FW_BOARD;          // esp8266 / sdpro / esp32-c2 / esp32
  // Which features this particular image carries. A slim build (e.g. the SD PRO
  // one) compiles several out, and without this the only way to find out is to
  // poke the endpoints and see what answers.
  {
    String f;
    if (WITH_USAGE)    f += "usage,";
    if (WITH_CALENDAR) f += "calendar,";
    if (WITH_RADAR)    f += "radar,";
    if (WITH_TLS)      f += "tls,";
    if (f.endsWith(",")) f.remove(f.length() - 1);
    o["features"] = f;
  }
  o["repo"] = REPO_URL;
  if (g_updateMsg.length()) o["updateMsg"] = g_updateMsg;
  o["mode"] = (netMode() == NET_AP) ? "ap" : "sta";
  o["connected"] = netConnected();
  o["ssid"] = netSSID();
  o["ip"] = netIP();
  o["rssi"] = netRSSI();
  o["heap"] = ESP.getFreeHeap();
  o["maxblk"] = platformMaxFreeBlock();   // largest contiguous block (TLS handshake needs one)
  #if WITH_CALENDAR && WITH_DEVICE_WEATHER
  // Device-direct weather fetch outcome -- this board has no serial, so the
  // only way to see why the weather page is blank is over HTTP.
  { int fc, aq, aqf; uint32_t ago; weatherFetchDiag(fc, aq, aqf, ago);
    o["wxFc"] = fc; o["wxAq"] = aq; o["wxAqF"] = aqf; o["wxAgoS"] = ago / 1000; }
  #endif
  // What the link gate currently thinks of us -- the same verdict that decides
  // whether the DEVICE OFFLINE notice takes the screen. Curl-able, so an outage
  // can be diagnosed (and this firmware's presence confirmed) without a photo.
  { LinkState ls = linkState(*S);
    o["link"] = (ls == LINK_NO_WIFI) ? "nowifi" : (ls == LINK_NO_INTERNET ? "nointernet" : "ok"); }
  // Link churn and allocation failures, live. Both were invisible on 2026-09-11:
  // f661 flapped for two hours and the SDK's 212-byte allocation failed, and the
  // only evidence left was a crash record. A failure that does NOT crash (lwIP
  // giving up gracefully) now shows here too.
  o["wifiDisc"]  = netDisconnectCount();
  o["wifiDownS"] = netDownMs() / 1000;
  { uint32_t n = oomCount();
    o["oom"] = n;
    if (n) {
      const OomEvent& e = oomRing()[(n - 1) % OOM_RING_LEN];
      char ra[12];
      snprintf(ra, sizeof(ra), "0x%08x", (unsigned)e.ra);
      o["oomRa"]   = ra;
      o["oomSize"] = e.size;
      o["oomHeap"] = e.heap;
      o["oomAgoS"] = (millis() - e.ms) / 1000;
    } }
  o["contstk"] = platformFreeContStack();   // primary stack headroom (ESP8266)
  o["uptime"] = millis() / 1000;
  o["reset"] = appResetReason();
  o["synced"] = clockSynced();
  { String ts = clockTimeStr(); if (ts.length()) o["time"] = ts; }
  o["tz"]        = S->clock.tz;
  o["night"]     = clockNightActive();   // dimming now
  o["nightHeld"] = clockNightHeld();      // in the window but waiting for a fresh NTP sync
  o["clockFresh"] = clockTrusted();       // last NTP sync within the trust window

  sendJson(doc);
}

// Fingerprint of everything network-identity related: the WiFi list and the
// hostname. Changing any of it needs a reboot, because the connection and the
// mDNS registration are established once at boot.
static String netFingerprint(const Settings& s) {
  String f((int)s.wifiCount);
  for (uint8_t i = 0; i < s.wifiCount; i++) {
    f += '\n';
    f += s.wifi[i].ssid;
    f += '\x01';
    f += s.wifi[i].pass;
  }
  f += '\n';
  f += s.hostname;
  return f;
}

static void handlePostConfig() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }

  String oldNet = netFingerprint(*S);
  uint8_t oldRot = S->rotation;
#if WITH_CALENDAR && WITH_DEVICE_WEATHER
  float oldLat = S->calendar.lat, oldLon = S->calendar.lon;
  uint16_t oldPoll = S->calendar.weatherPollSec;
#endif

  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  saveSettings(*S);

  // Live apply (no reboot needed for these)
  clockReapply(*S);         // re-arm SNTP iff the timezone changed
  appApplyBrightness();     // apply effective brightness (respects night/auto/manual)
  if (S->rotation != oldRot) gfxSetRotation(S->rotation);
  gfxSetTone(S->toneR, S->toneG, S->toneB, S->toneSat);
  appInvalidate();          // re-init every mode + repaint (covers mode/URL/symbol changes)
#if WITH_CALENDAR && WITH_DEVICE_WEATHER
  // A new location invalidates the readings AND the pushed city that captions
  // them, and must be fetched now rather than at the old deadline.
  if (S->calendar.lat != oldLat || S->calendar.lon != oldLon) weatherLocationChanged();
  else if (S->calendar.weatherPollSec != oldPoll) weatherCadenceChanged();
#endif

  bool wifiChanged = netFingerprint(*S) != oldNet;

  JsonDocument res;
  res["ok"] = true;
  res["reboot"] = wifiChanged;
  sendJson(res);

  if (wifiChanged) scheduleReboot(800);
}

// POST /wifi from SETUP_HTML (form-encoded). Adds or updates ONE network by
// SSID and keeps every other saved network and the list order. Unlike the
// /api/config wifi array (authoritative: a missing row is a deletion), this
// can never remove a network -- a unit that fell into AP mode away from home
// must not lose its home network by saving the local one.
static void handleWifiSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    server.send(400, "text/plain", "network name 1-32 characters, password up to 64");
    return;
  }
  uint8_t i = 0;
  while (i < S->wifiCount && S->wifi[i].ssid != ssid) i++;
  if (i == S->wifiCount) {
    if (S->wifiCount >= MAX_WIFI_NETS) {
      server.send(409, "text/plain", "saved-network list is full; remove one under /?full=1");
      return;
    }
    S->wifi[i].ssid = ssid;
    S->wifi[i].pass = "";
    S->wifiCount++;
  }
  if (pass.length()) S->wifi[i].pass = pass;   // blank keeps the stored password
  saveSettings(*S);
  String p = FPSTR(SETUP_CSS);
  p += FPSTR(SETUP_SAVED_HTML);
  server.send(200, "text/html", p);
  scheduleReboot(800);
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 25; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["enc"] = !platformScanIsOpen(i);
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

#ifdef WITH_CRASHTEST
// ---- deliberate crash, BUILD-FLAG GATED -- never in a production image ------
// Only env:smalltv_crashtest defines WITH_CRASHTEST. This exists to prove the
// RTC crash log end to end on real hardware: that custom_crash_callback()
// survives a genuine exception without faulting inside the postmortem handler,
// that the stack words reach RTC memory, and that platformCrashLogRead()
// accepts the record on the next boot instead of rejecting it as stale.
//
// The pointer is a volatile global rather than a literal 0 so the compiler
// cannot constant-fold the store into an `ill` instruction -- that would raise
// IllegalInstruction and exercise a DIFFERENT exception path than the
// LoadStore fault we are reproducing.
//
// noinline keeps a named frame on the stack, so addr2line on the captured
// return addresses should name crashTestNullStore() -- which is the point: it
// proves the recovered addresses resolve to the real caller.
static volatile uint32_t* g_crashTestPtr = nullptr;

static void __attribute__((noinline)) crashTestNullStore() {
  *g_crashTestPtr = 0xDEADBEEF;
}

// SYS-context fault. An os_timer callback runs on the SDK's own stack, NOT the
// cont stack that loop() and the web server run on -- and the two live in
// different address ranges (postmortem passes stack_end = 0x3fffffb0 for SYS,
// g_pcont->stack_end for cont). The real 2026-09-10 crash came from SDK
// context, so a test that only ever faults inside handleClient() proves the
// easy half and stays silent on the half that matters. Run ?ctx=sys FIRST.
static os_timer_t g_crashTestTimer;
static void crashTestSysCb(void*) { *g_crashTestPtr = 0xDEADBEEF; }

static void handleCrashTest() {
  bool sys = server.hasArg("ctx") && server.arg("ctx") == "sys";
  // ?oom=N: inject N synthetic allocation failures first (distinct ra/size so the
  // decoded order is checkable), so the crash record's OOM ring is exercised.
  if (server.hasArg("oom")) {
    long n = server.arg("oom").toInt();
    for (long i = 0; i < n && i < 8; i++) oomTestInject(0x40201000UL + (uint32_t)i, 100 + (size_t)i);
  }
  server.send(200, "application/json", "{\"ok\":true,\"crashing\":true}");
  delay(150);            // let the response leave the socket before we fault
  if (sys) {
    os_timer_setfn(&g_crashTestTimer, crashTestSysCb, nullptr);
    os_timer_arm(&g_crashTestTimer, 200, 0);
    return;              // fault arrives from SYS context, not from here
  }
  crashTestNullStore();
}
#endif

static void handleFactory() {
  factoryReset(*S);
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true}");
  scheduleReboot(400);
}

// Full settings backup.
//
// SECURITY (fixed 2026-09-15): this used to stream the persisted /config.json
// verbatim, and saveSettings() writes that file with includeSecrets=true — so
// an unauthenticated GET returned every WiFi password in clear text. Confirmed
// live on a real unit sitting on a third-party IoT network: `curl
// http://<ip>/api/export` yielded staPass and each wifi[].pass.
//
// The old comment justified it as "anyone on the LAN can already read them via
// the browser's WiFi tab". That is not true and may never have been:
// /api/config goes through settingsToJson(..., includeSecrets=false) and emits
// only staPassSet/passSet booleans. Export was the single cleartext path out.
//
// Now: masked by default, exactly like /api/config. Secrets are included only
// when the caller is on the device's OWN access point (NET_AP) — that is the
// setup hotspot you join while physically next to the device, which is the
// same trust domain as typing the passwords in, and is not reachable from a
// routed network. On a normal STA network the backup omits passwords; restoring
// such a backup needs the WiFi credentials re-entered, which is the correct
// trade against handing them to anyone who can reach port 80.
static void handleExport() {
  const bool allowSecrets = (netMode() == NET_AP);
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  settingsToJson(*S, root, allowSecrets);
  root["secretsIncluded"] = allowSecrets;   // so a restore knows what it has
  String out;
  serializeJson(doc, out);
  server.sendHeader("Content-Disposition", "attachment; filename=smalltv-config.json");
  server.send(200, "application/json", out);
}

// Restore a backup: apply everything, persist, reboot (WiFi/hostname may change).
static void handleImport() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "bad json");
    return;
  }
  settingsApplyJson(*S, doc.as<JsonObjectConst>());
  saveSettings(*S);
  server.send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
  scheduleReboot(800);
}

// No-op success: the only refresh-on-demand consumer was the removed stock
// feature. Still answers ok:true so an older web UI or script calling this
// endpoint keeps working instead of getting a 404.
static void handleRefresh() {
  server.send(200, "application/json", "{\"ok\":true}");
}

// Check the newest GitHub release against the running version.
static void handleCheckUpdate() {
  OtaLatest r = otaCheckLatest(*S);
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  o["current"] = FW_VERSION;
  o["ok"] = r.ok;
  o["latest"] = r.tag;
  o["newer"] = r.newer;
  if (!r.ok) o["error"] = r.error;
  sendJson(doc);
}

// Trigger the self-update. The actual (blocking) download runs from the loop so
// this response returns first; on success the device reboots into the new image.
static void handleSelfUpdate() {
  g_selfUpdate = true;
  g_updateMsg = "starting...";
  server.send(200, "application/json", "{\"ok\":true}");
}

// Push endpoint: the daemon POSTs the usage payload here when the device can't
// reach it (Wi-Fi client isolation). Body is the {s,sr,w,wr,st,ok} contract.
// Filtered by the daemon-IP setting (empty = accept from anywhere) — not a
// security boundary, just catches a daemon accidentally pointed at the wrong
// device from silently overwriting what's on screen.
static void handleUsagePush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_USAGE
  bool ok = usageApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Push endpoint: clawdmeter-daemon's --calendar feature POSTs the next-event
// payload here. Same daemon-IP filter as handleUsagePush above.
static void handleCalendarPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = calendarApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleWeatherPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = weatherApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleZaiPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = zaiApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleCodexPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = codexApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleAntigravityPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = antigravityApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleOpenrouterPush() {
  if (!checkDaemonIp()) { sendWrongSource(); return; }
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "no body"); return; }
#if WITH_CALENDAR
  bool ok = openrouterApply(server.arg("plain"));
#else
  bool ok = false;
#endif
  server.send(ok ? 200 : 400, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ---- OTA ------------------------------------------------------------------
static void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : platformUpdateError().c_str());
  if (ok) scheduleReboot(1200);
}

#if WITH_ALBUM
// Photo push. Unlike every other /api/* push this must NOT go through
// server.arg("plain"): the body is 115,200 bytes and buffering it into a String
// on a chip with ~26 KB of free heap is an instant OOM. It uses the multipart
// upload path instead, exactly like the OTA below, so the frame is consumed a
// TCP segment at a time and drawn straight to the panel. The daemon therefore
// has to POST multipart/form-data, not a raw body.
static bool s_albumAuthed = false;

static void handleAlbumUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    // The source check has to happen here: the done-handler runs after the
    // whole body has already been received and acted on.
    s_albumAuthed = checkDaemonIp();
    if (s_albumAuthed) albumRxBegin();
  } else if (!s_albumAuthed) {
    return;                       // wrong source: swallow the body, touch nothing
  } else if (up.status == UPLOAD_FILE_WRITE) {
    albumRxChunk(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    albumRxEnd(true);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    albumRxEnd(false);
  }
  yield();
}

// The reply is the synchronisation channel: "next" is how many seconds until
// this page is on screen again, so the daemon can sleep exactly that long
// instead of polling us. -1 means the album page is not in the rotation.
static void handleAlbumDone() {
  if (!s_albumAuthed) { sendWrongSource(); return; }
  char buf[64];
  snprintf(buf, sizeof(buf), "{\"ok\":true,\"w\":%d,\"h\":%d,\"next\":%d}",
           ALBUM_W, ALBUM_H, albumSecsToNextSlot());
  server.send(200, "application/json", buf);
}
#endif

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
#if defined(SMALLTV_ESP8266)
    WiFiUDP::stopAll();   // free UDP sockets so the OTA has max contiguous flash/heap
#endif
    uint32_t maxSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSpace)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.end();
  }
  yield();
}

// ---- captive portal -------------------------------------------------------
static void handleNotFound() {
  if (netMode() == NET_AP) {
    // Redirect everything to the config page so the captive portal pops.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------------------
void webPortalBegin(Settings& settings) {
  S = &settings;

  // If the last boot ran a queued GitHub update and failed, surface why
  // (success reboots into the new image before we ever get here).
  g_updateMsg = otaTakeBootResult();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/wifi", HTTP_POST, handleWifiSave);     // SETUP_HTML's form (AP mode)
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
#ifdef WITH_CRASHTEST
  server.on("/api/crashtest", HTTP_POST, handleCrashTest);   // gated: see above
#endif
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/import", HTTP_POST, handleImport);
  server.on("/api/checkupdate", HTTP_GET, handleCheckUpdate);
  server.on("/api/selfupdate", HTTP_POST, handleSelfUpdate);
  server.on("/api/usage", HTTP_POST, handleUsagePush);   // daemon pushes usage here
  server.on("/api/calendar", HTTP_POST, handleCalendarPush);   // daemon pushes calendar here
  server.on("/api/weather", HTTP_POST, handleWeatherPush);      // daemon pushes weather/AQI here
  server.on("/api/zai", HTTP_POST, handleZaiPush);               // daemon pushes z.ai quota here
  server.on("/api/codex", HTTP_POST, handleCodexPush);           // daemon pushes Codex quota here
  server.on("/api/antigravity", HTTP_POST, handleAntigravityPush); // daemon pushes Antigravity quota here
  server.on("/api/openrouter", HTTP_POST, handleOpenrouterPush); // daemon pushes OpenRouter spend here
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
#if WITH_ALBUM
  server.on("/api/album", HTTP_POST, handleAlbumDone, handleAlbumUpload);   // daemon pushes a 240x240 RGB565 frame here
#endif

  // Common captive-portal probe endpoints
  server.on("/generate_204", handleNotFound);
  server.on("/gen_204", handleNotFound);
  server.on("/hotspot-detect.html", handleNotFound);
  server.on("/connecttest.txt", handleNotFound);
  server.onNotFound(handleNotFound);

  server.begin();
}

void webPortalLoop() {
  server.handleClient();

  // Run the GitHub self-update outside the request handler so the browser gets its
  // response first.
  if (g_selfUpdate) {
    g_selfUpdate = false;
#if defined(SMALLTV_ESP8266)
    // RAM-tight chip: verify there is something to install, then queue the
    // download for the next boot (otaBootUpdate in setup(), ~45 KB free) and
    // reboot. A failure there lands back in g_updateMsg via otaTakeBootResult.
    OtaLatest r = otaCheckLatest(*S);
    if (!r.ok)         g_updateMsg = "check failed: " + r.error;
    else if (!r.newer) g_updateMsg = "already up to date (" FW_VERSION ")";
    else if (otaRequestBootUpdate(r.tag.c_str())) {
      g_updateMsg = "updating...";
      scheduleReboot(400);
    } else {
      g_updateMsg = F("could not queue update (storage error)");
    }
#else
    // ESP32 targets: mbedTLS has the RAM to download in place; blocks while it
    // runs and reboots into the new image on success.
    String err = otaUpdateFromGitHub(*S);
    g_updateMsg = err.length() ? err : "updating...";
#endif
  }
}

bool webPortalRebootDue() {
  return g_reboot && (int32_t)(millis() - g_rebootAt) >= 0;
}
