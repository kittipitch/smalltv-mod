#!/usr/bin/env python3
"""Run the real mode selector and dropdown builder with a fake clock/display."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.cpp").read_text()
selector = main[main.index("// Standalone Agenda"):main.index("static Settings g_settings;")]
cpp = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
enum { MODE_USAGE, MODE_CAL_AGENDA, MODE_CAL_AGENDA2, MODE_CAROUSEL };
struct Settings { int mode = MODE_CAL_AGENDA; unsigned carouselSec = 5;
                  bool carouselAgenda2 = false; };
struct DisplayMode { int mode, wakes = 0; int modeConst() const { return mode; }
                     void wake(const Settings&) { ++wakes; } };
DisplayMode first{MODE_CAL_AGENDA}, second{MODE_CAL_AGENDA2}, usage{MODE_USAGE};
DisplayMode* kModes[] = {&first, &second, &usage};
const size_t kModeCount = 3;
size_t g_carOrder[] = {0, 1, 2}, g_carIdx = 0;
uint32_t g_carSwitch = 0, now = 100;
uint32_t millis() { return now; }
struct Calendar { bool valid = true; unsigned count = 6; } events;
const Calendar& calendarGet() { return events; }
bool carouselHas(const Settings& s, const DisplayMode* m) {
  return m != &second || (s.carouselAgenda2 && events.count > 3);
}
void carouselNext(const Settings&) {}
'''
cpp += selector + r'''
int main() {
  Settings s;
  assert(activeMode(s) == &first);  // Carousel checkbox must not disable solo page 2.
  now = 5099; assert(activeMode(s) == &first);
  now = 5100; assert(activeMode(s) == &second); assert(second.wakes == 1);
  now = 5101; assert(activeMode(s) == &second); assert(second.wakes == 1);
  now = 10099; assert(activeMode(s) == &second);
  now = 10100; assert(activeMode(s) == &first); assert(first.wakes == 1);
  now = 15100; assert(activeMode(s) == &second); assert(second.wakes == 2);
  events.count = 3;
  now = 15101; assert(activeMode(s) == &first); assert(first.wakes == 2);
  now = 50000; assert(activeMode(s) == &first);
  events.count = 6;
  now = 50001; assert(activeMode(s) == &first);
  now = 55001; assert(activeMode(s) == &second);
  s.mode = MODE_USAGE; assert(activeMode(s) == &usage);
  s.mode = MODE_CAL_AGENDA; now = 90000; assert(activeMode(s) == &first);
  now = 95000; assert(activeMode(s) == &second);
  // Unsigned elapsed arithmetic must survive millis() rollover.
  s.mode = MODE_USAGE; activeMode(s);
  s.mode = MODE_CAL_AGENDA; now = UINT32_MAX - 15; assert(activeMode(s) == &first);
  now = 4983; assert(activeMode(s) == &first);
  now = 4984; assert(activeMode(s) == &second);
}
'''
with tempfile.TemporaryDirectory(prefix="agenda-rotation-") as tmp:
    source = Path(tmp) / "test.cpp"
    source.write_text(cpp)
    binary = Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

ui = (root / "src/webui.h").read_text()
select = re.search(r'<select id="mode".*?</select>', ui, re.S).group()
assert 'value="agenda2"' not in select
js = r'''
const assert = require('node:assert/strict');
const sel = {value:'agenda', options:[], set innerHTML(html) {
  this.options = [...html.matchAll(/value="([^"]+)"/g)].map(m => ({value:m[1]}));
}};
const elements = {mode:sel, carouselRow:{style:{}}, carouselList:{style:{}}, carouselLbl:{}};
function $(id) { return elements[id]; }
var carOrder = ['usage','agenda','weather'];
var CAR_MODES = carOrder.map(id => ({id, label:id}));
'''
js += ui[ui.index("function rebuildModeSelect()"):ui.index("function renderCarouselList(")]
js += ui[ui.index("function modeChanged()"):ui.index("function loadConfig()")]
js += r'''
rebuildModeSelect();
assert.deepEqual(sel.options.map(o => o.value), ['usage','agenda','weather','carousel']);
assert.equal(sel.value, 'agenda');
sel.value = 'agenda2'; rebuildModeSelect(); assert.equal(sel.value, 'agenda');
modeChanged(); assert.equal(elements.carouselRow.style.display, 'block');
assert.equal(elements.carouselList.style.display, 'none');
sel.value = 'carousel'; modeChanged(); assert.equal(elements.carouselList.style.display, 'block');
'''
subprocess.run(["node"], input=js, text=True, check=True)
print("PASS: equal page dwell, redraws, data changes, rollover, and mode dropdown")
