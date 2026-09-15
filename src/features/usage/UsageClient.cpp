#include "UsageClient.h"
#include "Platform.h"
#include "Net.h"
#include <ArduinoJson.h>
#include <math.h>

static UsageData g_usage;
static uint32_t  g_nextPollMs = 0;
static bool      g_inited = false;

// ---------------------------------------------------------------------------
void usageInit(const Settings& s) {
  (void)s;
  g_usage.clear();
  g_nextPollMs = millis();
  g_inited = true;
}

void usageForceRefresh() { g_nextPollMs = millis(); }

const UsageData& usageGet() { return g_usage; }

bool usageFresh(uint32_t withinMs) {
  return g_usage.valid && (millis() - g_usage.lastOkMs) <= withinMs;
}

// ---- parse: usage contract -------------------------------------------------
// { "s":29, "sr":142, "w":4, "wr":9876, "st":"allowed", "ok":true }
//   s  = 5h utilization %        sr = minutes until 5h reset
//   w  = 7d utilization %        wr = minutes until 7d reset
//   w/wr are OMITTED entirely when the account has no weekly window (the API
//   sends no 7d headers for org-managed accounts) -- absent means "no such
//   window", which is NOT the same as zero and must not render as 0%.
//   st = rate-limit status       ok = false => explicit "no data"
static void usageFilter(JsonDocument& f) {
  f["s"] = true; f["sr"] = true; f["w"] = true;
  f["wr"] = true; f["st"] = true; f["ok"] = true;
}

static bool applyUsageDoc(UsageData& d, JsonDocument& doc) {
  if (doc["ok"].is<bool>() && doc["ok"].as<bool>() == false) return false;
  if (!doc["s"].is<float>() && !doc["s"].is<int>()) return false;   // require at least session %

  d.sessionPct      = constrain(doc["s"].as<float>(), 0.0f, 100.0f);
  // "w" absent => the account has no weekly window (org-managed). Record that
  // instead of letting it default to 0, which the UI would draw as a real 0%.
  d.hasWeekly       = doc["w"].is<float>() || doc["w"].is<int>();
  d.weeklyPct       = constrain(doc["w"] | 0.0f, 0.0f, 100.0f);
  d.sessionResetMin = doc["sr"] | 0;
  d.weeklyResetMin  = doc["wr"] | 0;
  strlcpy(d.status, doc["st"] | "", sizeof(d.status));

  d.valid = true;
  d.error = false;
  d.lastOkMs = millis();
  return true;
}

static bool parseUsage(UsageData& d, Stream& stream) {
  JsonDocument filter; usageFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, stream, DeserializationOption::Filter(filter))) return false;
  return applyUsageDoc(d, doc);
}

// Pushed payload (POST /api/usage): same contract, parsed from a String body.
bool usageApply(const String& body) {
  JsonDocument filter; usageFilter(filter);
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;
  return applyUsageDoc(g_usage, doc);
}

// ---- one HTTP(S) GET + parse (mirrors StockClient::fetchUrl) ----------------
static bool fetchUsage(const Settings& s) {
  const String& url = s.usage.usageUrl;
  if (url.length() < 8) return false;
  bool https = url.startsWith("https://");

  std::unique_ptr<NetClient> client;
  if (https) {
#if !WITH_TLS
    // Built without TLS (see WITH_TLS in config.h). Say so rather than failing
    // silently -- an https:// pull URL is a config mistake on such a build, and
    // the daemon's normal push path does not need it.
    Serial.println(F("[usage] https pull URL, but this build has no TLS -- use http:// or push"));
    return false;
#else
    if (ESP.getFreeHeap() < 20000) return false;   // too little heap for TLS (incl. the 9 KB thunk); skip, don't crash
    client.reset(platformMakeSecureClient(2048));   // LAN / self-hosted endpoint
#endif
  } else {
    client.reset(new WiFiClient());
  }

  HTTPClient http;
  http.setTimeout(s.httpTimeout);
  http.setReuse(false);
  if (!http.begin(*client, url)) return false;
  http.addHeader("Accept", "application/json");

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  bool ok = parseUsage(g_usage, http.getStream());
  http.end();
  return ok;
}

// ---------------------------------------------------------------------------
// Pull only on a link that has held for a while. weatherService() already refuses
// to fetch while disconnected; this path had no guard at all, so a pull-mode unit
// kept opening TLS clients through a Wi-Fi flap -- allocating and blocking while
// the SDK was mid-association, the window in which its own unchecked 212-byte
// allocation failed on f661. Returning before the timer is touched means the poll
// runs as soon as the link settles rather than a whole period later.
// AP (setup) mode never reaches here: loop() returns for NET_AP before any mode
// or the link gate runs, so netUpMs() staying 0 there suppresses nothing new.
#define USAGE_LINK_SETTLE_MS 10000UL

void usageService(const Settings& s) {
  if (!g_inited) usageInit(s);
  if (!netConnected() || netUpMs() < USAGE_LINK_SETTLE_MS) return;
  if ((int32_t)(millis() - g_nextPollMs) < 0) return;

  if (!fetchUsage(s)) g_usage.error = true;   // keep stale data, flag the error

  g_nextPollMs = millis() + (uint32_t)s.usage.pollSec * 1000UL;
}
