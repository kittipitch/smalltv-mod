#!/usr/bin/env python3
"""Status "only when something is offline" never hides the carousel's ONLY page.

Compiles the real carouselHas() from main.cpp with stub data. Owner 2026-10-31: the filter is
carousel-only, and "if only page then it has to show anyway". Run from the repo root.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text()
body = main[main.index("static bool carouselHas("):main.index("// Advance g_carIdx")]
cpp = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#define WITH_CALENDAR 1
enum { MODE_USAGE, MODE_RADAR, MODE_CAL_AGENDA, MODE_CAL_AGENDA2, MODE_CAL_WEATHER, MODE_CAL_HOURLY,
       MODE_CAL_FORECAST, MODE_ZAI, MODE_CODEX, MODE_COPILOT, MODE_ANTIGRAVITY, MODE_STATUS };
struct Settings { bool carouselUsage = false, carouselRadar = false, carouselAgenda = false,
  carouselAgenda2 = false, carouselWeather = false, carouselHourly = false, carouselForecast = false,
  carouselZai = false, carouselCodex = false, carouselCopilot = false, carouselAntigravity = false,
  carouselStatus = true, statusOfflineOnly = true; };
struct DisplayMode { int mode; int modeConst() const { return mode; } };
DisplayMode usage{MODE_USAGE}, status{MODE_STATUS};
DisplayMode* kModes[] = {&usage, &status};
const size_t kModeCount = 2;
struct Valid { bool valid = false; unsigned count = 0; };
Valid none;
const Valid& calendarGet() { return none; }
const Valid& zaiGet() { return none; }
const Valid& codexGet() { return none; }
const Valid& copilotGet() { return none; }
const Valid& antigravityGet() { return none; }
struct UptimeData { bool valid = true; uint32_t ageS = 0, rxMillis = 0; };
UptimeData up;
const UptimeData& uptimeGet() { return up; }
bool down = false;
static bool uptimeHasDown() { return down; }
uint32_t millis() { return 0; }
''' + body + r'''
int main() {
  Settings s;
  // Status the only ticked page, nothing down: it still shows.
  assert(carouselHas(s, &status));
  // Another page in rotation: the filter hides Status until something is down.
  s.carouselUsage = true;
  assert(!carouselHas(s, &status));
  down = true;
  assert(carouselHas(s, &status));
  // Filter off: always in rotation.
  down = false; s.statusOfflineOnly = false;
  assert(carouselHas(s, &status));
  // No uptime data ever: stays out (this unit has no uptime feed).
  s.statusOfflineOnly = true; s.carouselUsage = false; up.valid = false;
  assert(!carouselHas(s, &status));
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(cpp)
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-o", f"{d}/t", f"{d}/t.cpp"], check=True)
    subprocess.run([f"{d}/t"], check=True)
print("test_status_offline_only: OK")
