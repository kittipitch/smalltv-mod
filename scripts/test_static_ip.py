#!/usr/bin/env python3
"""Optional fixed address per saved network, plus the guard that keeps a wrong one from stranding the unit: a static
join reports connected at once, so it never reaches the setup page. If the gateway stays silent, the unit moves to
DHCP until the next reboot. Compiles the pure helpers on the host, checks the wiring in the source, and runs the web
form's validator under node. Run from the repo root."""
import shutil
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
net = (root / "src/Net.cpp").read_text()
settings = (root / "src/Settings.cpp").read_text()
portal = (root / "src/WebPortal.cpp").read_text()
ui = (root / "src/webui.h").read_text()

ip_code = settings[settings.index("// ---- static ip"):settings.index("// ---- end static ip")]
guard_code = net[net.index("// ---- static guard"):net.index("// ---- end static guard")]
src = r'''
#include <cassert>
#include <cstdint>
''' + ip_code + guard_code + r'''
// Addresses in the raw form IPAddress converts to: first octet in the low byte.
static uint32_t ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}
int main() {
  const uint32_t m24 = ip(255, 255, 255, 0), m23 = ip(255, 255, 254, 0);
  assert(staticIpValid(ip(10, 0, 0, 5), ip(10, 0, 0, 1), m24));
  assert(staticIpValid(ip(10, 0, 0, 16), ip(10, 0, 1, 1), m23));          // /23: both halves are one subnet
  assert(!staticIpValid(ip(10, 0, 0, 16), ip(10, 0, 1, 1), m24));         // /24: different subnets
  assert(!staticIpValid(ip(10, 0, 0, 5), ip(10, 0, 0, 5), m24));          // address is the gateway
  assert(!staticIpValid(0, ip(10, 0, 0, 1), m24));
  assert(!staticIpValid(ip(10, 0, 0, 5), 0, m24));
  assert(!staticIpValid(ip(10, 0, 0, 5), ip(10, 0, 0, 1), 0));
  assert(!staticIpValid(ip(10, 0, 0, 5), ip(10, 0, 0, 1), ip(255, 0, 255, 0)));   // holes in the mask
  assert(!staticIpValid(ip(10, 0, 0, 0), ip(10, 0, 0, 1), m24));          // network address
  assert(!staticIpValid(ip(10, 0, 0, 255), ip(10, 0, 0, 1), m24));        // broadcast address

  assert(kStaticGuardMs == 300000 && kStaticGuardPollMs == 20000 && kFallbackRestartMs == 300000);
  assert(!guardShouldFall(kStaticGuardMs - 1));
  assert(guardShouldFall(kStaticGuardMs));
  assert(!fallbackShouldRestart(kFallbackRestartMs - 1));
  assert(fallbackShouldRestart(kFallbackRestartMs));
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)

# Every join goes through applyIp, or a rotation would carry the previous network's address onto the next one.
assert net.count("WiFi.begin(") == net.count("applyIp(") - 1 == 3, "applyIp before every WiFi.begin"
for b in net.split("WiFi.begin(")[:-1]:
    assert "applyIp(" in b[-120:], "applyIp immediately before WiFi.begin"
# A DHCP unit must not change behaviour: the switch back only runs if static was applied.
apply_ip = net[net.index("static void applyIp"):]
apply_ip = apply_ip[:apply_ip.index("\n}\n")]
assert "g_ipStatic" in apply_ip and "g_staticOff" in apply_ip

loop = net[net.index("void netLoop()"):]
up = loop[loop.index("if (WiFi.status() == WL_CONNECTED)"):]
up = up[:up.index("if (g_upSince) { g_upSince = 0;")]
assert "g_gwSeen = now" in up.split("g_rotDwellMs")[0], "the silence is timed from each (re)connect, not across an outage"
assert "guardShouldFall(" in up and "gatewayAnswers(" in up, "guard runs only while connected"
down = loop[loop.index("if (g_upSince) { g_upSince = 0;"):]
assert "fallbackShouldRestart(" in down and "ESP.restart()" in down, "DHCP fallback must not strand the unit either"
assert down.index("fallbackShouldRestart(") < down.index("g_cfg->wifiCount <= 1"), "applies to single-network units too"

# Load: empty clears, a missing key keeps (an old cached page must not wipe it), invalid never applies.
load = settings[settings.index('if (root["wifi"].is<JsonArrayConst>())'):settings.index('else if (root["staSsid"]')]
assert "staticIpValid(" in load and "old[i].ip" in load
assert '"ip"' in settings[settings.index('JsonArray wf ='):settings.index("// Legacy mirror")]
# A changed address needs the reboot that a changed network list already gets.
fp = portal[portal.index("static String netFingerprint"):portal.index("static void handlePostConfig")]
assert ".ip" in fp and ".gw" in fp and ".mask" in fp and ".dns" in fp
save = portal[portal.index("static void handleWifiSave"):]
assert ".ip = 0" in save[:save.index("saveSettings")], "a new row must not inherit an old address"
assert 'o["ipMode"]' in portal, "the fallback must be visible"

# The web form refuses a bad entry before it can reach the unit.
fn = ui[ui.index("function ipOk("):ui.index("// ---- end ipOk")]
js = fn + r'''
var a=require('assert');
a(ipOk('','','',''));                                   // blank = DHCP
a(ipOk('10.0.0.5','10.0.0.1','255.255.255.0',''));      // DNS optional
a(ipOk('10.0.0.16','10.0.1.1','255.255.254.0','9.9.9.9'));
a(!ipOk('10.0.0.5','','',''));                          // incomplete
a(!ipOk('10.0.0.16','10.0.1.1','255.255.255.0',''));    // different subnets
a(!ipOk('10.0.0.5','10.0.0.5','255.255.255.0',''));
a(!ipOk('10.0.0.5','10.0.0.1','255.0.255.0',''));
a(!ipOk('10.0.0.256','10.0.0.1','255.255.255.0',''));
a(!ipOk('10.0.0.255','10.0.0.1','255.255.255.0',''));
a(!ipOk('10.0.0.5','10.0.0.1','255.255.255.0','x'));
a(!ipOk('','10.0.0.1','255.255.255.0',''));             // gateway without an address
'''
assert shutil.which("node"), "node is needed to check the form validator"
subprocess.run(["node", "-e", js], check=True)
assert "ipOk(" in ui[ui.index("function collectWifi"):ui.index("function scanPick")]
print("test_static_ip: OK")
