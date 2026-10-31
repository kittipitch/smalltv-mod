#!/usr/bin/env python3
"""Movies "twice as long" box: the real carouselDwellMs() doubles the Movies page only when it is ticked, and the
web UI loads and saves the box. Run from the repo root."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text()
fn = main[main.index("static uint32_t carouselDwellMs("):]
fn = fn[:fn.index("\n}\n") + 3]
cpp = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#define WITH_MOVIES 1
enum { MODE_USAGE, MODE_MOVIES, MODE_ZAI };
struct Settings { unsigned carouselSec = 10; bool moviesDouble = false; };
struct DisplayMode { int mode; int modeConst() const { return mode; } };
DisplayMode usage{MODE_USAGE}, movies{MODE_MOVIES}, zai{MODE_ZAI};
DisplayMode* kModes[] = {&usage, &movies, &zai};
''' + fn + r'''
int main() {
  Settings s;
  assert(carouselDwellMs(s, 1) == 10000);   // Movies: the interval by default
  assert(carouselDwellMs(s, 0) == 20000);   // Claude usage is always double
  s.moviesDouble = true;
  assert(carouselDwellMs(s, 1) == 20000);   // ticked: Movies twice as long
  assert(carouselDwellMs(s, 2) == 10000);   // no other page changes
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(cpp)
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"], check=True)
    subprocess.run([f"{d}/t"], check=True)
ui = (root / "src/webui.h").read_text()
assert 'id="moviesDouble"' in ui, "no checkbox"
assert "sc('moviesDouble'" in ui and "moviesDouble:gc('moviesDouble')" in ui, "checkbox not loaded/saved"
st = (root / "src/Settings.cpp").read_text()
assert 'root["moviesDouble"]' in st and 'root["moviesDouble"].is<bool>()' in st, "setting not exported/imported"
print("test_movies_double: OK")
