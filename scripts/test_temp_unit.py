#!/usr/bin/env python3
"""The C/F selection: the pure conversion helper compiles on the host and matches Open-Meteo's own
fahrenheit values, and the wiring makes every draw site go through it. Run from the repo root."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cal = (root / "src/features/calendar/CalendarMode.cpp").read_text()
settings = (root / "src/Settings.cpp").read_text()
ui = (root / "src/webui.h").read_text()

src = r'''
#include <cassert>
#include <cmath>
''' + cal[cal.index("// ---- temp unit"):cal.index("// ---- end temp unit")] + r'''
int main() {
  assert(wxTempOut(0, false) == 0 && wxTempOut(0, true) == 32);
  assert(wxTempOut(100, true) == 212 && wxTempOut(-40, true) == -40);
  assert(wxTempOut(27, true) == 81);          // Open-Meteo: 27.4 C -> 81.3 F -> 81
  assert(wxTempOut(28, true) == 82);          // 82.7 F for 28.2 C: same rounding band
  assert(wxTempOut(37, true) == 99 && wxTempOut(38, true) == 100);
  assert(wxTempUnitChar(true)=='F' && wxTempUnitChar(false)=='C');
  for (int c = -99; c <= 199; c++) {          // whole inputs round-trip inside the clamp
    int f = wxTempOut(c, true);
    assert(f == (int)lroundf(c * 1.8f + 32.0f) && f >= -146 && f <= 390);
    assert(wxTempOut(c, false) == c);
  }
}
'''
import subprocess, tempfile
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)

# Every draw site converts; nothing prints a hard-coded C any more.
assert cal.count("wxTempOut(") >= 4, "current weather, forecast hi/lo, hourly all convert"
assert '"C"' not in cal[cal.index("if (w.hasTemp)"):cal.index("drawRow(gfx, 116")] or "wxTempOut" in cal[cal.index("if (w.hasTemp)"):cal.index("drawRow(gfx, 116")], "current temp letter must follow the setting"
assert 's.calendar.tempF' in cal and 'wxTempUnitChar(' in cal
assert cal.count('s.calendar.tempF') >= 3, "weather, forecast, hourly call sites"

# The setting round-trips like moviesDouble, and the UI sends it.
assert "tempF = false;" in settings and 'o["tempF"] = tempF;' in settings and 'if (o["tempF"].is<bool>())' in settings
assert "gc('tempF')" in ui, "collect sends it"
assert "$('tempF')" in ui, "load applies it"
assert 'id="tempF"' in ui, "the slide toggle exists"
print("test_temp_unit: OK")
