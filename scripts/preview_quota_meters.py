#!/usr/bin/env python3
"""Render the Claude, z.ai and Codex quota CARDS (not whole pages: no mascot or header) from source.

Run from the firmware checkout (Python + Pillow + a C++14 compiler + the installed GFX library). Source-
derived like preview_agenda.py: each file's palette, fmtReset, its colour function and its meter function
are cut verbatim out of src/; only the hardware surface is a host stub. Renders git HEAD (baseline) and the
working tree (candidate) with the same fixtures and prints an artifact directory. Checks, from the print
trace and pixels: the reset text is right-aligned in a fixed 17-char field ending 14 px inside the card, and
a data-only redraw (full=false) with a SHORT reset text leaves no pixel of a previous LONG one behind.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


root = Path(__file__).resolve().parents[1]
library = root / ".pio/libdeps/smalltv/GFX Library for Arduino/src"
gfx_source = (library / "Arduino_GFX.cpp").read_text()
font = (library / "font/glcdfont.h").read_text()
gfxh = (root / "src/Gfx.h").read_text()
gfx_cpp = (root / "src/Gfx.cpp").read_text()
output = Path(tempfile.mkdtemp(prefix="quota-preview-", dir="/tmp"))

# page: (file, colour function, meter function, call(top, label, pct, resetMins, full) as C++)
PAGES = {
    "claude": ("src/features/usage/UsageMode.cpp", "barColor", "drawMeter",
               lambda top, lab, pct, rm, full: f'drawMeter(&gfx, {top}, "{lab}", {pct}.0f, {rm}, {full}, false, true);'),
    "zai": ("src/features/calendar/ZaiMode.cpp", "pctColor", "drawZaiMeter",
            lambda top, lab, pct, rm, full: f'drawZaiMeter(&gfx, {top}, "{lab}", true, {pct}, true, {rm}, {full}, false);'),
    "codex": ("src/features/calendar/CodexMode.cpp", "pctColor", "drawCodexMeter",
              lambda top, lab, pct, rm, full: f'drawCodexMeter(&gfx, {top}, "{lab}", true, {pct}, true, {rm}, {full}, false);'),
}

STUB = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#define pgm_read_byte(p) (*(p))
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
class Arduino_GFX {
public:
  std::vector<uint16_t> pixels = std::vector<uint16_t>(240*240);
  int cursor_x=0, cursor_y=0, textsize_x=1, textsize_y=1, text_pixel_margin=0;
  int _min_text_x=0, _min_text_y=0, _max_text_x=239, _max_text_y=239;
  uint16_t textcolor=0xffff, textbgcolor=0xffff;
  int page=0;
  void startWrite() {} void endWrite() {}
  void writePixelPreclipped(int x,int y,uint16_t color) {
    if (x>=0 && y>=0 && x<240 && y<240) pixels[y*240+x]=color;
  }
  void writePixel(int x,int y,uint16_t color) { writePixelPreclipped(x,y,color); }
  void writeFillRect(int x,int y,int w,int h,uint16_t color) {
    for(int yy=std::max(0,y); yy<std::min(240,y+h); ++yy)
      for(int xx=std::max(0,x); xx<std::min(240,x+w); ++xx) pixels[yy*240+xx]=color;
  }
  void fillRect(int x,int y,int w,int h,uint16_t color) { writeFillRect(x,y,w,h,color); }
  void writeFastHLine(int x,int y,int w,uint16_t c) { writeFillRect(x,y,w,1,c); }
  void writeFastVLine(int x,int y,int h,uint16_t c) { writeFillRect(x,y,1,h,c); }
  void fillScreen(uint16_t color) { std::fill(pixels.begin(),pixels.end(),color); }
  void setTextSize(int s) { textsize_x=textsize_y=s; }
  void setTextColor(uint16_t c) { textcolor=textbgcolor=c; }
  void setTextColor(uint16_t c,uint16_t bg) { textcolor=c; textbgcolor=bg; }
  void setCursor(int x,int y) { cursor_x=x; cursor_y=y; }
  void print(char c) { char text[2]={c,0}; print(text); }
  void print(const char* text) {
    std::cout<<page<<"\t"<<cursor_x<<"\t"<<cursor_y<<"\t"<<textsize_x<<"\t"<<text<<"\n";
    while (*text) {
      unsigned char c=*text++;
      if(c=='\n') { cursor_x=0; cursor_y+=8*textsize_y; continue; }
      if(c=='\r') continue;
      if(cursor_x+textsize_x*6-1>239) { cursor_x=0; cursor_y+=8*textsize_y; }
      drawChar(cursor_x,cursor_y,c,textcolor,textbgcolor);
      cursor_x+=6*textsize_x;
    }
  }
'''


def build(page, src, tag):
    fname, colour, meter, call = PAGES[page]
    cpp = STUB
    for name in ("writeFillEllipseHelper", "fillRoundRect", "writeEllipseHelper", "drawRoundRect", "fillCircle"):
        cpp += function(gfx_source, f"void Arduino_GFX::{name}(").replace("Arduino_GFX::", "")
    draw_char = function(gfx_source, "void Arduino_GFX::drawChar(")
    default = draw_char[draw_char.index("  {", draw_char.index("else // glcdfont")):-2]
    cpp += "void drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg) {\n"
    cpp += "int16_t block_w,block_h,curX,curY,curH;\n" + default + "\n}\n};\n"
    cpp += "#define min(a,b) ((a)<(b)?(a):(b))\n#define max(a,b) ((a)>(b)?(a):(b))\n"  # Arduino's macros
    cpp += function(gfx_cpp, "int gfxTextW(") + "\n" + function(gfx_cpp, "uint8_t gfxFitSize(") + "\n"
    cpp += "\n".join(l for l in gfxh.splitlines() if l.startswith("#define C_")) + "\n"
    for m in re.finditer(r"^#define (C_\w+)\s+(0x[0-9A-Fa-f]+)", src, re.M):
        cpp += f"#undef {m.group(1)}\n#define {m.group(1)} {m.group(2)}\n"
    for name, kind in (("fmtReset", "void"), (colour, "uint16_t"), (meter, "void")):
        cpp += function(src, re.search(rf"static {kind} {name}\(", src).group()) + "\n"
    cpp += "int main(int, char** argv) {\n  std::string dir = argv[1];\n"
    # fixture 0: 62% / 12% with a short and the LONGEST reset text; fixture 1: stale-pixel check.
    cpp += "  { Arduino_GFX gfx; gfx.page = 0; gfx.fillScreen(0);\n    " + call(50, "5h", 62, 297, "true") + "\n    " + call(138, "7d", 12, 43000, "true") + "\n"
    cpp += '''    std::ofstream im(dir + "/cards.ppm", std::ios::binary); im << "P6\\n240 240\\n255\\n";
    for (uint16_t p : gfx.pixels) { char c[3] = {char(((p>>11)&31)*255/31), char(((p>>5)&63)*255/63), char((p&31)*255/31)}; im.write(c, 3); } }
'''
    # long text, then a data-only redraw with "now": must equal a fresh full draw of "now"
    cpp += "  Arduino_GFX a; a.page = 1; a.fillScreen(0);\n  " + call(138, "7d", 12, 43000, "true").replace("&gfx", "&a") + "\n  " + call(138, "7d", 12, 0, "false").replace("&gfx", "&a") + "\n"
    cpp += "  Arduino_GFX b; b.page = 2; b.fillScreen(0);\n  " + call(138, "7d", 12, 0, "true").replace("&gfx", "&b") + "\n"
    cpp += '  int diff = 0; for (size_t i = 0; i < a.pixels.size(); i++) diff += a.pixels[i] != b.pixels[i];\n  std::cerr << "STALEDIFF " << diff << "\\n";\n  return diff != 0;\n}\n'
    work = output / f"{page}-{tag}"
    work.mkdir()
    (work / "preview.cpp").write_text(font + cpp)
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-const-variable",
                    str(work / "preview.cpp"), "-o", str(work / "preview")], check=True)
    run = subprocess.run([str(work / "preview"), str(work)], text=True, capture_output=True)
    rows = [l.split("\t", 4) for l in run.stdout.splitlines()]
    return rows, Image.open(work / "cards.ppm"), run.returncode, run.stderr.strip()


results = {}
for page, (fname, *_rest) in PAGES.items():
    cand = (root / fname).read_text()
    base = subprocess.run(["git", "show", f"HEAD:{fname}"], cwd=root, text=True, capture_output=True, check=True).stdout
    b_rows, b_img, b_rc, b_err = build(page, base, "baseline")
    c_rows, c_img, c_rc, c_err = build(page, cand, "candidate")
    # candidate: the reset text is "Resets in X" right-aligned in 17 chars, ending at x=218
    reset_rows = [r for r in c_rows if r[0] == "0" and int(r[2]) in (116, 204)]
    assert len(reset_rows) == 2, (page, reset_rows)
    for r, rs in zip(sorted(reset_rows, key=lambda r: int(r[2])), ("4h 57m", "29d 20h")):
        assert r[4] == f"{'Resets in ' + rs:>17}" and r[3] == "2", (page, r)
        assert int(r[1]) + 17 * 12 == 218, ("right edge 218", page, r)
    # baseline: left aligned at x=22 (the thing being changed)
    assert [int(r[1]) for r in b_rows if r[0] == "0" and int(r[2]) in (116, 204)] == [22, 22], page
    assert c_rc == 0 and "STALEDIFF 0" in c_err, (page, "stale pixels after a long->short redraw", c_err)
    results[page] = (b_img, c_img)

sheet = Image.new("RGB", (720, 480))
for i, page in enumerate(PAGES):
    sheet.paste(results[page][0], (i * 240, 0))
    sheet.paste(results[page][1], (i * 240, 240))
sheet.save(output / "quota-cards-baseline-over-candidate.png")
Image.new("RGB", (720, 240)).save(output / "_unused.png")
(output / "report.md").write_text("# Quota card preview (claude, z.ai, codex)\n\n"
    "PASS: candidate 'Resets in X' is right-aligned in a 17-char field ending at x=218 on every page (baseline: left at x=22).\n"
    "PASS: after a long reset text, a data-only redraw with a short one equals a fresh draw (no stale pixels).\n")
print(output)
