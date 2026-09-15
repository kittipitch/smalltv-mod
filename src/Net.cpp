#include "Net.h"
#include "Platform.h"
#include <DNSServer.h>

static NetMode     g_mode = NET_AP;
static DNSServer   g_dns;
static String      g_hostname;
static String      g_apSsid;
static const Settings* g_cfg = nullptr;  // for runtime failover between saved networks
static int8_t      g_curNet = -1;        // settings index of the joined network
static uint32_t    g_downSince = 0;      // 0 = connected; else millis() the drop began
static uint32_t    g_upSince = 0;        // 0 = down; else millis() the link was first seen up
static uint32_t    g_discCount = 0;      // up -> down transitions seen by netLoop() since boot
// Multi-SSID failover dwell. Doubles after every rotation so a long outage costs
// a handful of forced associations per hour, not one every 45 s; reset on connect.
#define ROT_DWELL_MIN_MS  120000UL
#define ROT_DWELL_MAX_MS  600000UL
static uint32_t    g_rotDwellMs = ROT_DWELL_MIN_MS;
static uint32_t    g_nextRotMs  = 0;

static void startAP(const Settings& s) {
  g_mode = NET_AP;
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (s.apPass.length() >= 8) {
    WiFi.softAP(s.apSsid.c_str(), s.apPass.c_str());
  } else {
    WiFi.softAP(s.apSsid.c_str());           // open AP (WPA2 needs >=8 chars)
  }
  g_apSsid = s.apSsid;
  // Captive portal: answer every DNS query with our own IP.
  g_dns.setErrorReplyCode(DNSReplyCode::NoError);
  g_dns.start(53, "*", apIP);
}

void netBegin(const Settings& s, void (*onProgress)(const char*)) {
  g_cfg = &s;
  g_hostname = s.hostname.length() ? s.hostname : String(DEFAULT_HOSTNAME);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  platformSetHostname(g_hostname.c_str());

  if (s.wifiCount == 0) {
    if (onProgress) onProgress("No WiFi saved");
    startAP(s);
    return;
  }

  WiFi.mode(WIFI_STA);

  // Try order: scan once (blocking is fine here, only the boot screen is up)
  // and put the networks the scan can see first, IN CONFIG ORDER. Unseen ones
  // (hidden SSIDs or currently out of range) go last, also in config order,
  // with a shorter timeout each.
  //
  // The list order IS the priority: entry 0 is the network the owner wants,
  // the rest are fallbacks. It used to order the seen ones by RSSI, which made
  // the choice a coin flip whenever two saved SSIDs live on the SAME radio --
  // one test unit had two saved SSIDs from the SAME access point, and after an
  // OTA it came up on the backup one at -52 dBm rather than the primary.
  // Signal strength is not a useful tiebreak between two SSIDs of one radio,
  // and across real APs the owner's stated preference should still win.
  uint8_t order[MAX_WIFI_NETS];
  bool    seen[MAX_WIFI_NETS];
  if (s.wifiCount == 1) {
    order[0] = 0;
    seen[0] = true;
  } else {
    for (uint8_t i = 0; i < s.wifiCount; i++) seen[i] = false;
    if (onProgress) onProgress("Scanning...");
    int found = WiFi.scanNetworks();
    for (int a = 0; a < found; a++)
      for (uint8_t i = 0; i < s.wifiCount; i++)
        if (WiFi.SSID(a) == s.wifi[i].ssid) seen[i] = true;
    WiFi.scanDelete();

    // Seen entries first, then unseen, each group in config order. Two stable
    // passes, so every index lands in `order` exactly once.
    uint8_t k = 0;
    for (uint8_t i = 0; i < s.wifiCount; i++) if (seen[i])  order[k++] = i;
    for (uint8_t i = 0; i < s.wifiCount; i++) if (!seen[i]) order[k++] = i;
  }

  for (uint8_t k = 0; k < s.wifiCount; k++) {
    const WifiCred& n = s.wifi[order[k]];
    if (onProgress) {
      char msg[48];
      snprintf(msg, sizeof(msg), "WiFi: %s", n.ssid.c_str());
      onProgress(msg);
    }
    WiFi.begin(n.ssid.c_str(), n.pass.c_str());

    uint32_t budget = seen[order[k]] ? 15000 : 8000;
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < budget) {
      delay(200);
      yield();
    }

    if (WiFi.status() == WL_CONNECTED) {
      g_curNet = (int8_t)order[k];
      g_mode = NET_STA;
      if (MDNS.begin(g_hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
#if WITH_USAGE
        // Discoverable usage-push service so the clawdmeter daemon can find and
        // push to every SmallTV on the LAN (no hardcoded host). TXT carries the
        // device id, firmware version, and the push path.
        MDNS.addService("clawdmeter", "tcp", 80);
        MDNS.addServiceTxt("clawdmeter", "tcp", "id",   g_hostname.c_str());
        MDNS.addServiceTxt("clawdmeter", "tcp", "ver",  FW_VERSION);
        MDNS.addServiceTxt("clawdmeter", "tcp", "path", "/api/usage");
#endif
      }
      if (onProgress) onProgress(WiFi.localIP().toString().c_str());
      return;
    }
    WiFi.disconnect();
    delay(100);
  }

  if (onProgress) onProgress("WiFi failed -> AP");
  startAP(s);
}

void netLoop() {
  if (g_mode == NET_AP) {
    g_dns.processNextRequest();
    return;
  }
  // STA: keep mDNS alive and let the SDK own reconnection. Never scan here -- it
  // would block the display loop and the web server.
  //
  // There used to be a WiFi.reconnect() every 10 s while down. In core 3.1.2 that
  // is wifi_station_disconnect() + wifi_station_connect(), and WiFi.status()
  // reports an association still IN PROGRESS as WL_DISCONNECTED -- so the nudge
  // aborted the SDK's own attempt (setAutoReconnect(true), netBegin) and forced a
  // fresh one. Every forced association re-enters the closed SDK's
  // ieee80211_setup_ratetable, which calls pvPortZalloc(212) without a NULL check
  // and memcpy's into the result. On 2026-09-11 f661 flapped for two hours and
  // one of those allocations failed: exc 29, memcpy(NULL). The nudge could never
  // make an association succeed sooner; it only added attempts. Two retry loops
  // fighting is now one. (journal/crashes/f661_2026-09-11_exc29_beacon.md)
  platformMdnsUpdate();
  uint32_t now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    if (!g_upSince) g_upSince = now ? now : 1;
    g_downSince  = 0;
    g_rotDwellMs = ROT_DWELL_MIN_MS;
    return;
  }
  // Counted by polling, deliberately not with WiFi.onStationModeDisconnected():
  // that adapter builds a String copy of the SSID on every event, in SDK context,
  // right before the SDK re-associates -- heap churn at exactly the wrong moment.
  if (g_upSince) { g_upSince = 0; g_discCount++; }
  if (!g_downSince) {
    g_downSince = now ? now : 1;
    g_nextRotMs = now + g_rotDwellMs;
  }

  // Single network: nothing to do. The SDK keeps retrying on its own.
  if (!g_cfg || g_cfg->wifiCount <= 1) return;
  if ((int32_t)(now - g_nextRotMs) < 0) return;

  // Several saved networks: the SDK only knows the one it was configured with, so
  // moving on is ours to do -- once per dwell window, not every 10 s.
  // wifiCount cannot shrink under g_curNet here: handlePostConfig() reboots
  // whenever the saved network list changes (netFingerprint), and g_curNet = -1
  // (never joined) simply rotates to entry 0.
  g_curNet = (int8_t)((g_curNet + 1) % g_cfg->wifiCount);
  WiFi.begin(g_cfg->wifi[g_curNet].ssid.c_str(), g_cfg->wifi[g_curNet].pass.c_str());
  if (g_rotDwellMs < ROT_DWELL_MAX_MS) g_rotDwellMs *= 2;
  if (g_rotDwellMs > ROT_DWELL_MAX_MS) g_rotDwellMs = ROT_DWELL_MAX_MS;
  g_nextRotMs = now + g_rotDwellMs;
}

uint32_t netDisconnectCount() { return g_discCount; }
uint32_t netDownMs() { return g_downSince ? millis() - g_downSince : 0; }
uint32_t netUpMs()   { return g_upSince   ? millis() - g_upSince   : 0; }

NetMode netMode()      { return g_mode; }
bool    netConnected() { return g_mode == NET_STA && WiFi.status() == WL_CONNECTED; }

String netIP() {
  return (g_mode == NET_AP) ? WiFi.softAPIP().toString()
                            : WiFi.localIP().toString();
}

String netSSID() {
  return (g_mode == NET_AP) ? g_apSsid : WiFi.SSID();
}

int netRSSI() {
  return (g_mode == NET_STA) ? WiFi.RSSI() : 0;
}

String netMAC() {
  return WiFi.macAddress();
}
