#!/usr/bin/env python3
"""A unit that fell to the setup page with networks saved must try them again by itself (owner 2026-11-01): a failed
join at boot used to need a person. It waits while someone is connected to the page, so typing is never interrupted.
Compiles the helper from Net.cpp on the host. Run from the repo root."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
net = (root / "src/Net.cpp").read_text()
code = net[net.index("// ---- ap retry"):net.index("// ---- end ap retry")]
src = r'''
#include <cassert>
#include <cstdint>
''' + code + r'''
int main() {
  assert(kApRetryMs == 180000);
  assert(!apShouldRetry(0, 0, 999999));              // nothing saved: the setup page is where it should stay
  assert(!apShouldRetry(2, 0, kApRetryMs - 1));      // too soon
  assert(apShouldRetry(2, 0, kApRetryMs));           // saved networks, nobody there, time is up
  assert(apShouldRetry(1, 0, kApRetryMs + 5));
  assert(!apShouldRetry(2, 1, 999999));              // someone is on the setup page: never restart under them
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)
loop = net[net.index("void netLoop()"):]
loop = loop[:loop.index("// STA: keep mDNS")]
assert "apShouldRetry(" in loop and "softAPgetStationNum()" in loop and "ESP.restart()" in loop, "netLoop must use it"
assert "g_apIdleSince = millis()" in loop, "the wait restarts while someone is connected"
print("test_ap_retry: OK")
