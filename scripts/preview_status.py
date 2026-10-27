#!/usr/bin/env python3
"""Render the Status (Better Stack uptime) page from source with the installed GFX glyphs.

Source-derived like preview_antigravity.py and preview_hourly.py: cut the page functions
out of src/features/calendar/StatusMode.cpp, compile on the host with an Arduino_GFX stub,
render the 14 design situations (generic target names) to PNGs in a fresh /tmp directory.
With STATUS_DESIGN_DIR set to a folder of reference situation_N.png files, also print the
differing pixel count per situation and fail on any difference.
"""
from pathlib import Path
import subprocess
import tempfile
import sys
from PIL import Image


def function(source, signature):
    # Match DEFINITION: signature line ending in '{' (before any ';')
    pos = 0
    start = -1
    while True:
        pos = source.index(signature, pos)
        nxt_brace = source.index("{", pos)
        try:
            nxt_semi = source.index(";", pos)
        except ValueError:
            nxt_semi = len(source)
        if nxt_brace < nxt_semi:
            start = pos
            break
        pos += 1
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
data_h = (root / "src/features/calendar/CalendarData.h").read_text()
status_cpp = (root / "src/features/calendar/StatusMode.cpp").read_text()
import os
design_dir = Path(os.environ["STATUS_DESIGN_DIR"]) if os.environ.get("STATUS_DESIGN_DIR") else None

output = Path(tempfile.mkdtemp(prefix="status-preview-", dir="/tmp"))

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
  void drawFastHLine(int x,int y,int w,uint16_t c) { writeFillRect(x,y,w,1,c); }
  void drawFastVLine(int x,int y,int h,uint16_t c) { writeFillRect(x,y,1,h,c); }
  void fillScreen(uint16_t color) { std::fill(pixels.begin(),pixels.end(),color); }
  void setTextSize(int s) { textsize_x=textsize_y=s; }
  void setTextColor(uint16_t c) { textcolor=textbgcolor=c; }
  void setTextColor(uint16_t c,uint16_t bg) { textcolor=c; textbgcolor=bg; }
  void setCursor(int x,int y) { cursor_x=x; cursor_y=y; }
  void print(char c) { char text[2]={c,0}; print(text); }
  void print(const char* text) {
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

MAIN = r'''
void emit(Arduino_GFX& gfx, const std::string& path) {
  std::ofstream image(path, std::ios::binary);
  image << "P6\n240 240\n255\n";
  for (uint16_t p : gfx.pixels) {
    char rgb[3] = {char(((p>>11)&31)*255/31), char(((p>>5)&63)*255/63), char((p&31)*255/31)};
    image.write(rgb, 3);
  }
}

static void addTarget(UptimeData& u, const char* name, const char* kind, const char* st,
                      const char* cause = nullptr, int sinceS = -1) {
  if (u.count >= 8) return;
  UptimeTarget& t = u.targets[u.count++];
  strlcpy(t.name, name, sizeof(t.name));
  if (strcmp(kind, "beat") == 0) t.kind = UPTIME_KIND_BEAT;
  else if (strcmp(kind, "site") == 0) t.kind = UPTIME_KIND_SITE;
  else t.kind = UPTIME_KIND_NONE;

  if (strcmp(st, "down") == 0) t.status = UPTIME_STATUS_DOWN;
  else if (strcmp(st, "pending") == 0) t.status = UPTIME_STATUS_PENDING;
  else if (strcmp(st, "paused") == 0) t.status = UPTIME_STATUS_PAUSED;
  else t.status = UPTIME_STATUS_UP;

  if (cause) {
    t.hasCause = true;
    strlcpy(t.cause, cause, sizeof(t.cause));
  } else {
    t.hasCause = false;
    t.cause[0] = 0;
  }

  if (sinceS >= 0) {
    t.hasSinceS = true;
    t.sinceS = (uint32_t)sinceS;
  } else {
    t.hasSinceS = false;
    t.sinceS = 0;
  }
}

int main(int, char** argv) {
  std::string dir = argv[1];
  const char* G1 = "status.example.org/web01";
  const char* G2 = "status.example.org/api02";
  const int M = 60;
  const int AGE = 20;

  for (int sit = 1; sit <= 14; sit++) {
    Arduino_GFX gfx;
    UptimeData u;
    u.clear();
    u.valid = true;
    uint32_t effAge = AGE;
    bool nodata = false;

    if (sit == 1) {
      addTarget(u, G2, "site", "up", nullptr, 137 * M - AGE);
      addTarget(u, G1, "site", "up", nullptr, 113 * M - AGE);
      addTarget(u, "backup-nas-1", "beat", "up", nullptr, 22 * M - AGE);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M - AGE);
    } else if (sit == 2) {
      addTarget(u, "backup-nas-1", "beat", "down", "Missed heartbeat", 16 * M - AGE);
      addTarget(u, "office-desk01", "beat", "down", "Missed heartbeat", 16 * M - AGE);
      addTarget(u, G1, "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, G2, "site", "up", nullptr, 137 * M - AGE);
    } else if (sit == 3) {
      addTarget(u, "web-01", "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, "backup-nas-1", "beat", "down", "Missed heartbeat", 16 * M - AGE);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M - AGE);
      addTarget(u, "example.org/build-server-rack3-node07-lab", "site", "up", nullptr, 2 * 3600);
    } else if (sit == 4) {
      addTarget(u, G1, "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, G2, "site", "up", nullptr, 137 * M - AGE);
      addTarget(u, "backup-nas-1", "beat", "up", nullptr, 22 * M - AGE);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M - AGE);
    } else if (sit == 5) {
      addTarget(u, "nas", "beat", "up", nullptr, 45 - AGE);
      addTarget(u, "printer", "beat", "up", nullptr, 12 * M - AGE);
      addTarget(u, G1, "site", "up", nullptr, 135 * M - AGE);
      addTarget(u, G2, "site", "up", nullptr, 3 * 86400);
    } else if (sit == 6) {
      addTarget(u, "nas", "beat", "down", "Missed heartbeat", 45 - AGE);
      addTarget(u, "web-01", "site", "down", "Status 404", 12 * M - AGE);
      addTarget(u, "api-02", "site", "down", "Connection timeout", 135 * M - AGE);
      addTarget(u, "nas", "beat", "down", "Missed heartbeat", 3 * 86400);
    } else if (sit == 7) {
      effAge = 14 * M;
      addTarget(u, G2, "site", "up", nullptr, 137 * M);
      addTarget(u, G1, "site", "up", nullptr, 113 * M);
      addTarget(u, "backup-nas-1", "beat", "up", nullptr, 22 * M);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M);
    } else if (sit == 8) {
      effAge = 14 * M;
      addTarget(u, "backup-nas-1", "beat", "down", "Missed heartbeat", 16 * M);
      addTarget(u, G1, "site", "up", nullptr, 113 * M);
      addTarget(u, G2, "site", "up", nullptr, 137 * M);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M);
    } else if (sit == 9) {
      addTarget(u, "office-desk01", "beat", "down", "Missed heartbeat", 45 * M - AGE);
      addTarget(u, G1, "site", "pending");
      addTarget(u, "backup-nas-1", "beat", "paused");
    } else if (sit == 10) {
      addTarget(u, "backup-nas-1", "beat", "down", "Missed heartbeat", 16 * M - AGE);
      addTarget(u, "office-desk01", "beat", "down", "Missed heartbeat", 16 * M - AGE);
      addTarget(u, G1, "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, G2, "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, "printer", "beat", "down", "Missed heartbeat", 5 * M - AGE);
      addTarget(u, "nas-home", "beat", "up", nullptr, 60 * M - AGE);
    } else if (sit == 11) {
      addTarget(u, G1, "site", "down", "Status 502", 3 * M - AGE);
      addTarget(u, G2, "site", "up", nullptr, 137 * M - AGE);
      addTarget(u, "backup-nas-1", "beat", "up", nullptr, 22 * M - AGE);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 22 * M - AGE);
      addTarget(u, "printer", "beat", "up", nullptr, 5 * M);
      addTarget(u, "nas-home", "beat", "paused");
      addTarget(u, "google.com", "site", "up", nullptr, 167 * M);
      addTarget(u, "nas-backup-01", "beat", "pending");
    } else if (sit == 12) {
      addTarget(u, "backup-nas-1", "beat", "down");
      addTarget(u, "office-desk01", "beat", "down");
      addTarget(u, G1, "site", "down");
      addTarget(u, G2, "site", "up");
    } else if (sit == 13) {
      addTarget(u, "example.org/build-server-rack3-node07-lab", "site", "down", "Unexpected response body length", 7 * M - AGE);
      addTarget(u, "printer", "beat", "down", "Connection refused", 40 * M - AGE);
      addTarget(u, "office-desk01", "beat", "up", nullptr, 75 * M - AGE);
      addTarget(u, "status.example.org/", "site", "up", nullptr, 8 * 3600);
    } else if (sit == 14) {
      u.clear();
      effAge = 0;
      nodata = true;
    }

    drawStatusPage(&gfx, u, effAge, nodata);
    emit(gfx, dir + "/situation_" + std::to_string(sit) + ".ppm");
  }
  return 0;
}
'''

# Build C++ test code
cpp = STUB
for name in ("writeFillEllipseHelper", "fillRoundRect", "writeEllipseHelper", "drawRoundRect"):
    cpp += function(gfx_source, f"void Arduino_GFX::{name}(").replace("Arduino_GFX::", "")
draw_char = function(gfx_source, "void Arduino_GFX::drawChar(")
default = draw_char[draw_char.index("  {", draw_char.index("else // glcdfont")):-2]
cpp += "void drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg) {\n"
cpp += "int16_t block_w,block_h,curX,curY,curW,curH; (void)curW;\n" + default + "\n}\n};\n"

# Extract CalendarData.h uptime structs
start = data_h.index("enum UptimeKind")
cpp += data_h[start:] + "\n"

# Extract StatusMode.cpp definitions and helper functions
# Defines from StatusMode.cpp
for line in status_cpp.splitlines():
    if line.startswith(("#define C_", "#define STATUS_", "static const char CH_")):
        cpp += line + "\n"

# Extracted functions
functions_to_extract = (
    "statusAdv",
    "statusRunW",
    "statusDrawRunChar",
    "statusDrawRun",
    "statusDrawText",
    "statusDrawRight",
    "statusDrawArrow",
    "statusSanitise",
    "statusLabelFor",
    "statusDurLong",
    "statusDurShort",
    "isWordBound",
    "statusCauseCode",
    "statusDrawFrame",
    "statusDrawEmpty",
    "statusDrawRow",
    "statusDrawOverflow",
    "statusDrawNoData",
    "drawStatusSlot",
    "drawStatusPage"
)

for fn in functions_to_extract:
    sig = f" {fn}("
    m = [l for l in status_cpp.splitlines() if sig in l and not l.strip().endswith(";")]
    assert m, f"Function {fn} not found in StatusMode.cpp"
    decl = m[0].strip()
    cpp += function(status_cpp, decl[:decl.find("(") + 1]) + "\n"

cpp += MAIN

work = output
(work / "preview.cpp").write_text(font + cpp)
subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-const-variable",
                "-Wno-unused-parameter", str(work / "preview.cpp"), "-o", str(work / "preview")], check=True)
subprocess.run([str(work / "preview"), str(work)], check=True)

total_diff = 0
results = []
for sit in range(1, 15):
    im = Image.open(work / f"situation_{sit}.ppm")
    im.save(work / f"situation_{sit}.png")
    if design_dir is None:
        continue
    ref = Image.open(design_dir / f"situation_{sit}.png")
    assert im.size == ref.size, f"Size mismatch for situation {sit}"
    diff = 0
    w, h = im.size
    im_px = im.load()
    ref_px = ref.load()
    for y in range(h):
        for x in range(w):
            if im_px[x, y] != ref_px[x, y]:
                diff += 1
    total_diff += diff
    results.append((sit, diff))
    print(f"Situation {sit:2d}: {diff} differing pixels")

if design_dir is None:
    print(f"rendered 14 situations into {work} (set STATUS_DESIGN_DIR to diff against reference PNGs)")
    sys.exit(0)
print(f"Total differing pixels across 14 situations: {total_diff}")
if total_diff == 0:
    print("SUCCESS: 0 differing pixels on all 14 situations!")
else:
    sys.exit(1)
