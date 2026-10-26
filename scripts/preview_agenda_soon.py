#!/usr/bin/env python3
"""Render the agenda page's "starts within the hour" highlight (bell + optional 1px frame) from the
real firmware source, in several device-clock scenarios, and assert the pixels.

Reuses preview_agenda.py's host harness (hardware stub, GFX font and roundrect routines, and the
page's own functions incl. the soon helpers and the bell, all taken verbatim from CalendarMode.cpp):
that script is exec'd up to the point where it would compile, and a new main() renders the scenarios. Prints the artifact directory. No firmware build or device is involved."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
agenda_src = (root / "scripts/preview_agenda.py").read_text()
cm = (root / "src/features/calendar/CalendarMode.cpp").read_text()

ns = {"__file__": str(root / "scripts/preview_agenda.py")}
sys.argv = ["preview_agenda.py"]            # default (weekdays on) harness run configuration
exec(compile(agenda_src[:agenda_src.index("host_source = output")], "preview_agenda_head", "exec"), ns)
cpp = ns["cpp"]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


# (the base harness already carries the stub members and the firmware's soon helpers)

# ---- new main --------------------------------------------------------------------------------
cpp = cpp[:cpp.index("int main(int argc,char** argv)")] + r'''
struct Scenario { const char* name; int hour, min, mday; bool frame; bool tomorrowEarly; };
int main(int argc, char** argv) {
  (void)argc;
  const Scenario sc[] = {
    {"A-soon-frame-on",      14,  0,  8, true,  false},   // event 14:30, 30 min away
    {"B-soon-frame-off",     14,  0,  8, false, false},   // bell only
    {"C-across-midnight",    23, 30,  8, true,  true },   // first card is Tomorrow 00:15
    {"D-running-1-min-in",   14, 31,  8, true,  false},   // 14:30-15:30 started a minute ago: still marked
    {"E-exactly-60-min",     13, 30,  8, true,  false},   // diff == 60: NOT "less than an hour"
    {"F-running-30-min-in",  15,  0,  8, true,  false},   // mid-event: bell persists
    {"G-after-end",          15, 35,  8, true,  false},   // ended 5 min ago: cleared
  };
  for (int i = 0; i < 7; i++) {
    previewNow = {};
    previewNow.tm_year = 126; previewNow.tm_mon = 9; previewNow.tm_mday = sc[i].mday;
    previewNow.tm_hour = sc[i].hour; previewNow.tm_min = sc[i].min;
    CalendarEvent c = {}; c.valid = true; c.count = 3;
    if (sc[i].tomorrowEarly) {
      event(c, 0, "Early start",       "2026-10-09T00:15:00", "2026-10-09T01:00:00", false);
    } else {
      event(c, 0, "Today meeting",     "2026-10-08T14:30:00", "2026-10-08T15:30:00", false);
    }
    event(c, 1, "Tomorrow meeting",    "2026-10-09T09:30:00", "2026-10-09T10:30:00", false);
    event(c, 2, "Same-month trip",     "2026-10-12", "2026-10-15", true);
    Settings s; s.calendar.showWeekday = true; s.calendar.soonFrame = sc[i].frame;
    Arduino_GFX gfx; gfx.page = i;
    drawAgendaPage(&gfx, s, c, 0);
    std::ofstream image(std::string(argv[1]) + "/" + sc[i].name + ".ppm", std::ios::binary);
    image << "P6\n240 240\n255\n";
    for (uint16_t p : gfx.pixels) {
      char rgb[3] = {char(((p>>11)&31)*255/31), char(((p>>5)&63)*255/63), char((p&31)*255/31)};
      image.write(rgb, 3);
    }
  }
  return 0;
}
'''
output = Path(tempfile.mkdtemp(prefix="agenda-soon-", dir="/tmp"))
(output / "preview.cpp").write_text(cpp)
subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-parameter",
                str(output / "preview.cpp"), "-o", str(output / "preview")], check=True)
subprocess.run([str(output / "preview"), str(output)], capture_output=True, check=True)

names = ["A-soon-frame-on", "B-soon-frame-off", "C-across-midnight", "D-running-1-min-in", "E-exactly-60-min", "F-running-30-min-in", "G-after-end"]
imgs = {}
for n in names:
    im = Image.open(output / f"{n}.ppm").convert("RGB")
    im.save(output / f"{n}.png")
    imgs[n] = im

PANEL, YELLOW = (24, 28, 24), (255, 255, 0)
FRAME = (222, 218, 213)   # 0xDEDA expanded exactly as the harness converts RGB565


def yellow(im, box):
    return sum(1 for x in range(box[0], box[2]) for y in range(box[1], box[3]) if im.getpixel((x, y)) == YELLOW)


def frame_px(im, top):   # the card's top edge row, well inside the straight part
    return [im.getpixel((x, top)) for x in range(40, 200, 10)]


BELL_PIXELS = 29 * 4      # 29 lit cells x 2x2 px
card1 = (130, 8, 158, 34)  # the gap left of the HH:MM on card 1 (time starts x=160)
a, b, c, d, e, f, g = (imgs[n] for n in names)

# A: bell (exactly the bell's pixels) + frame on card 1; the other cards untouched
assert yellow(a, card1) == BELL_PIXELS, ("A bell pixels", yellow(a, card1))
assert all(p == FRAME for p in frame_px(a, 6)), ("A frame top edge", frame_px(a, 6))
assert yellow(a, (130, 86, 158, 112)) == 0, "A: card 2 (tomorrow 09:30) must have no bell"
assert all(p == PANEL for p in frame_px(a, 84)), "A: card 2 must have no frame"
# B: frame box off -> bell only
assert yellow(b, card1) == BELL_PIXELS, "B bell must still be drawn"
assert all(p == PANEL for p in frame_px(b, 6)), ("B: no frame when the checkbox is off", frame_px(b, 6))
# C: 00:15 tomorrow seen at 23:30 is 45 minutes away -> highlighted
assert yellow(c, card1) == BELL_PIXELS and all(p == FRAME for p in frame_px(c, 6)), "C: across midnight"
# D and F: the event is RUNNING -> the bell persists through it; E: exactly 60 min pre-start, G: after the end
assert yellow(d, card1) == BELL_PIXELS and all(p == FRAME for p in frame_px(d, 6)), "D: running keeps the bell"
assert yellow(f, card1) == BELL_PIXELS and all(p == FRAME for p in frame_px(f, 6)), "F: mid-event keeps the bell"
for tag, im in (("E", e), ("G", g)):
    assert yellow(im, card1) == 0, (tag, "no bell")
    assert all(p == PANEL for p in frame_px(im, 6)), (tag, "no frame")
# the highlight must not disturb the rest of card 1: date text (x 18..100) and time (x 158..222)
for region in ((18, 12, 100, 32), (158, 12, 224, 32), (18, 40, 224, 62)):
    ra = [a.getpixel((x, y)) for x in range(region[0], region[2]) for y in range(region[1], region[3])]
    rd = [d.getpixel((x, y)) for x in range(region[0], region[2]) for y in range(region[1], region[3])]
    assert ra == rd, ("card 1 text must be identical with and without the highlight", region)

sheet = Image.new("RGB", (240 * 7, 270), (20, 20, 20))
dr = ImageDraw.Draw(sheet)
labels = ["A: 30 min away, frame on", "B: frame box off (bell only)", "C: 23:30 -> 00:15 tomorrow",
          "D: running, 1 min in", "E: exactly 60 min away", "F: running, 30 min in", "G: 5 min after end"]
for i, n in enumerate(names):
    sheet.paste(imgs[n], (i * 240, 30))
    dr.text((i * 240 + 6, 8), labels[i], fill=(230, 230, 230))
sheet.resize((sheet.width * 2 // 2 * 1, sheet.height)).save(output / "soon-scenarios.png")
Image.open(output / "soon-scenarios.png").resize((sheet.width * 2, 540), Image.Resampling.NEAREST).save(output / "soon-scenarios-2x.png")
(output / "report.md").write_text("# Agenda soon highlight preview\n\nPASS: A bell+frame, B bell only, C across midnight, D/E none; card text unchanged.\n")
print(output)
