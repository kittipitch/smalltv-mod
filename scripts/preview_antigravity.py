#!/usr/bin/env python3
"""Render the Antigravity (agy) quota page from source with the installed GFX glyphs.

Run from the firmware checkout with Python + Pillow + a C++14 compiler; prints an artifact
directory. No firmware build, device connection or OTA happens. Source-derived like
preview_agenda.py: pctColor, fmtReset, truncateDots, drawAntigravityMeter, drawAntigravityPage,
the AntigravityData struct, the icon array, the C_* palette and gfxTextW/gfxFitSize are cut
verbatim out of src/; only the hardware surface (a 240x240 RGB565 buffer) is a host stub.
Renders the BASELINE (git HEAD's AntigravityMode.cpp) and the CANDIDATE (the working tree) with the
same fixtures, so the change is visible side by side. Assertions run on the print trace and pixels.
"""
from pathlib import Path
import hashlib
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
data_h = (root / "src/features/calendar/CalendarData.h").read_text()
icon_h = (root / "src/features/calendar/AntigravityIcon.h").read_text()
logo_h = (root / "src/features/calendar/AgyLogo.h").read_text()
cand_src = (root / "src/features/calendar/AntigravityMode.cpp").read_text()
base_src = subprocess.run(["git", "show", "HEAD:src/features/calendar/AntigravityMode.cpp"],
                          cwd=root, text=True, capture_output=True, check=True).stdout
output = Path(tempfile.mkdtemp(prefix="agy-preview-", dir="/tmp"))

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
  // Same byte/bit walk as Arduino_GFX::drawBitmap(x,y,bitmap,w,h,color).
  void drawBitmap(int16_t x,int16_t y,const uint8_t* bitmap,int16_t w,int16_t h,uint16_t color) {
    int16_t byteWidth=(w+7)/8; uint8_t byte=0;
    for(int16_t j=0;j<h;j++,y++) for(int16_t i=0;i<w;i++) {
      if(i&7) byte<<=1; else byte=pgm_read_byte(&bitmap[j*byteWidth+i/8]);
      if(byte&0x80) writePixel(x+i,y,color);
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
static void set(AntigravityData& a, int pPro, const char* lPro, int rPro, int pFl, const char* lFl, int rFl,
                int pAr = -1, const char* lAr = "7d", int rAr = 0, const char* tFl = "", const char* tAr = "") {
  a.clear(); a.valid = true;
  a.hasPctPro = true; a.pctPro = pPro; a.hasLabelPro = true; strlcpy(a.labelPro, lPro, sizeof(a.labelPro));
  a.hasRPro = true; a.rPro = rPro;
  a.hasPctFlash = true; a.pctFlash = pFl; a.hasLabelFlash = true; strlcpy(a.labelFlash, lFl, sizeof(a.labelFlash));
  a.hasRFlash = true; a.rFlash = rFl;
  if (pAr >= 0) {
    a.hasPctArgon = true; a.pctArgon = pAr; a.hasLabelArgon = true; strlcpy(a.labelArgon, lAr, sizeof(a.labelArgon));
    a.hasRArgon = true; a.rArgon = rAr;
    a.hasTagFlash = true; strlcpy(a.tagFlash, tFl, sizeof(a.tagFlash));
    a.hasTagArgon = true; strlcpy(a.tagArgon, tAr, sizeof(a.tagArgon));
  }
}
int main(int, char** argv) {
  std::string dir = argv[1];
  AntigravityData a;
  // 0: the live account 2026-10-19: Gemini 5h and 7d untouched, Argon weekly exhausted
  // 1: an account with no Argon group: the bottom card stays whole ("7d")
  // 2: all three partly used
  // 3: the same partial use with the "grow bars right to left" setting on (growRight)
  const char* names[4] = {"live-argon-exhausted", "no-argon-7d", "partial-use", "partial-grow-right"};
  for (int i = 0; i < 4; i++) {
    if (i == 0) set(a, 0, "5h", 297, 0, "7d", 8280, 100, "7d", 8280, "Fl", "Ar");
    if (i == 1) set(a, 0, "5h", 297, 0, "7d", 10077);
    if (i >= 2) set(a, 62, "5h", 212, 12, "7d", 9600, 33, "7d", 9120, "Fl", "Ar");
    Arduino_GFX gfx; gfx.page = i;
    drawAntigravityPage(&gfx, a, true, i == 3);
    emit(gfx, dir + "/" + names[i] + ".ppm");
  }
  return 0;
}
'''


def build(src, tag):
    cpp = STUB
    for name in ("writeFillEllipseHelper", "fillRoundRect", "writeEllipseHelper", "drawRoundRect", "fillCircle"):
        cpp += function(gfx_source, f"void Arduino_GFX::{name}(").replace("Arduino_GFX::", "")
    draw_char = function(gfx_source, "void Arduino_GFX::drawChar(")
    default = draw_char[draw_char.index("  {", draw_char.index("else // glcdfont")):-2]
    cpp += "void drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg) {\n"
    cpp += "int16_t block_w,block_h,curX,curY,curH;\n" + default + "\n}\n};\n"
    cpp += function(gfx_cpp, "int gfxTextW(") + "\n" + function(gfx_cpp, "uint8_t gfxFitSize(") + "\n"
    cpp += "\n".join(l for l in gfxh.splitlines() if l.startswith("#define C_")) + "\n"
    for m in re.finditer(r"^#define (C_\w+)\s+(0x[0-9A-Fa-f]+)", src, re.M):  # the page's own palette wins
        cpp += f"#undef {m.group(1)}\n#define {m.group(1)} {m.group(2)}\n"
    cpp += icon_h[icon_h.index("#define ANTIGRAVITY_ICON_SIZE"):icon_h.index("};", icon_h.index("kAntigravityIcon")) + 2] + "\n"  # the baseline still draws this
    cpp += logo_h[logo_h.index("#define AGY_LOGO_COLS"):] + "\n"
    cpp += function(data_h, "struct AntigravityData {") + ";\n"
    for name in ("pctColor", "fmtReset", "truncateDots", "drawAntigravityMeter", "drawAntigravityHalf", "drawAgyLogo", "drawAntigravityPage"):
        if f"static void {name}(" not in src and f"static uint16_t {name}(" not in src:
            continue  # the baseline (git HEAD) has no half card
        cpp += function(src, re.search(r"static (?:uint16_t|void) " + name + r"\(", src).group()) + "\n"
    cpp += MAIN
    work = output / tag
    work.mkdir()
    (work / "preview.cpp").write_text(font + cpp)
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-const-variable", str(work / "preview.cpp"),
                    "-o", str(work / "preview")], check=True)
    trace = subprocess.run([str(work / "preview"), str(work)], text=True, capture_output=True, check=True).stdout
    (work / "trace.tsv").write_text(trace)
    rows = [l.split("\t", 4) for l in trace.splitlines()]
    names = ["live-argon-exhausted", "no-argon-7d", "partial-use", "partial-grow-right"]
    imgs = []
    for n in names:
        im = Image.open(work / f"{n}.ppm")
        im.save(work / f"{n}.png")
        imgs.append(im)
    return rows, imgs


def rgb(p):
    return ((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31


base_rows, base_imgs = build(base_src, "baseline")
cand_rows, cand_imgs = build(cand_src, "candidate")

# --- assertions on the candidate trace -------------------------------------------------------
def texts(rows, page):
    return {(int(r[1]), int(r[2])): (r[3], r[4]) for r in rows if int(r[0]) == page}

RIGHT = 8 + 224 - 14  # 218: the right edge of the big value, the bar and every reset text
for page, top_card, bottom in (
    (0, ("5h", " 0%", "4h 57m"), ("split", ("7d", " 0%", "5d 18h", "Fl"), ("7d", "99%", "5d 18h", "Ar"))),
    (1, ("5h", " 0%", "4h 57m"), ("whole", ("7d", " 0%", "6d 23h"))),
    (2, ("5h", "62%", "3h 32m"), ("split", ("7d", "12%", "6d 16h", "Fl"), ("7d", "33%", "6d 8h", "Ar"))),
    (3, ("5h", "62%", "3h 32m"), ("split", ("7d", "12%", "6d 16h", "Fl"), ("7d", "33%", "6d 8h", "Ar"))),
):
    t = texts(cand_rows, page)
    lab, val, rst = top_card
    assert t.get((22, 62)) == ("2", f"{lab:<10}"), ("top label", page, t.get((22, 62)))
    assert t.get((128, 60)) == ("5", val), ("top value", page, t.get((128, 60)))
    top_text = f"{'Resets in ' + rst:>17}"
    assert t.get((RIGHT - 17 * 12, 116)) == ("2", top_text), ("top 'Resets in', right aligned", page, t.get((RIGHT - 17 * 12, 116)))
    if bottom[0] == "whole":
        lab, val, rst = bottom[1]
        assert t[(22, 150)] == ("2", f"{lab:<10}"), t.get((22, 150))
        assert t[(128, 148)] == ("5", val), (page, t.get((128, 148)))
        assert t[(RIGHT - 7 * 12, 204)] == ("2", f"{rst:>7}"), (page, t.get((RIGHT - 7 * 12, 204)))
    else:
        for x, (lab, val, rst, tag) in ((8, bottom[1]), (122, bottom[2])):
            assert t[(x + 14, 144)] == ("2", f"{lab:<4}"), ("half label", page, x, t.get((x + 14, 144)))
            assert t[(x + 110 - 14 - 4 * 12, 144)] == ("2", f"{tag:>4}"), ("half model tag at the right", page, x)
            assert t[(x + 110 - 14 - 3 * 24, 162)] == ("4", f"{val:>3}"), ("half value size 4, right aligned", page, x)
            assert t[(x + 110 - 14 - 7 * 12, 204)] == ("2", f"{rst:>7}"), ("half reset, right aligned", page, x)
        # the right half's reset text ends exactly where the top card's does: the h under the m
        assert 122 + 110 - 14 == RIGHT, "right half and top card share the right edge"
assert sum("Resets in" in r[4] for r in cand_rows) == 4, "'Resets in' only on the top card of each of the 4 pages"
assert sum("Resets in" in r[4] for r in base_rows) == 8, "baseline should still print 'Resets in' on all 8 cards"

# --- pixel assertions ------------------------------------------------------------------------
BARBG, BLACK, PANEL = rgb(0x2945), (0, 0, 0), rgb(0x18E3)
live, nargon, part, grow = cand_imgs
red = sum(1 for x in range(140, 218) for y in range(138 + 56, 138 + 64) if live.getpixel((x, y)) == (255, 0, 0))
assert red > 60, ("exhausted Argon bar should be red", red)
assert all(live.getpixel((x, 138 + 60)) == BARBG for x in range(26, 100)), "0% 7d half-bar must draw no fill"
assert all(live.getpixel((x, y)) == BLACK for x in (119, 120, 121) for y in range(140, 218)), "gap between the halves must be black"
assert live.getpixel((60, 150)) == PANEL and live.getpixel((180, 150)) == PANEL, "both halves have a panel"
assert nargon.getpixel((120, 150)) == PANEL, "no-Argon bottom card must stay whole (panel across the middle)"
# 12% of the 82 px left track fills x=22..30; 33% of the right track fills x=136..162
assert part.getpixel((24, 138 + 60)) != BARBG and part.getpixel((60, 138 + 60)) == BARBG, "12% left half bar"
assert part.getpixel((150, 138 + 60)) != BARBG and part.getpixel((200, 138 + 60)) == BARBG, "33% right half bar"
# grow-right setting: the fill is anchored to the RIGHT end of every bar, small cards included
# (top bar 22..217, 62% -> 121 px: x=97..217; left half bar 22..103, 12% -> 9 px: x=95..103;
#  right half bar 136..217, 33% -> 27 px: x=191..217)
for (x_fill, x_empty, y, what) in ((200, 40, 50 + 52 + 6, "top bar"), (99, 60, 138 + 60, "left half bar"), (205, 150, 138 + 60, "right half bar")):
    assert grow.getpixel((x_fill, y)) != BARBG, ("grow-right fill at the right end", what)
    assert grow.getpixel((x_empty, y)) == BARBG, ("grow-right: left end empty", what)
    assert part.getpixel((x_fill, y)) == BARBG or what == "top bar", ("left-anchored version must differ", what)

# --- header logo: same size as the old icon's ink (30x28 px at x 7..36, y 12..39) -----------------
def ink_box(im, region=(0, 0, 56, 50)):
    pts = [(x, y) for x in range(region[0], region[2]) for y in range(region[1], region[3]) if im.getpixel((x, y)) != BLACK]
    return min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)
old = ink_box(base_imgs[0])
assert old == (7, 12, 36, 39), ("old icon ink box", old)
logo_bytes = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", logo_h[logo_h.index("kAgyLogo"):])]
COLS, ROWS, LW, LH, LX, LY = 12, 13, 26, 28, 9, 12
assert len(logo_bytes) == COLS * ROWS * 3
def q565(c):
    return rgb(((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3))
expect_px = 0
for r in range(ROWS):
    for c in range(COLS):
        if any(logo_bytes[(r * COLS + c) * 3:(r * COLS + c) * 3 + 3]):
            expect_px += ((c + 1) * LW // COLS - c * LW // COLS) * ((r + 1) * LH // ROWS - r * LH // ROWS)
for im in cand_imgs:
    box = ink_box(im)
    assert box == (LX, LY, LX + LW - 1, LY + LH - 1), ("new logo ink box", box)
    assert box[3] - box[1] == old[3] - old[1] and box[1] == old[1], "same height and same top as the old icon"
    assert old[0] <= box[0] and box[2] <= old[2], "inside the old icon's width"
    colored = sum(1 for x in range(0, 56) for y in range(0, 50) if im.getpixel((x, y)) != BLACK)
    assert colored == expect_px, ("logo pixel count", colored, expect_px)
    apex = (LX + (5 * LW // COLS) + 1, LY + 1)
    assert im.getpixel(apex) == q565(tuple(logo_bytes[5 * 3:5 * 3 + 3])), "apex cell colour"
    assert all(im.getpixel((x, y)) == BLACK for x in range(0, 56) for y in range(40, 50)), "gap above the cards must be black"

# --- contact sheets: baseline over candidate at native size, candidate enlarged 2x ----------
sheet = Image.new("RGB", (960, 480))
for i in range(4):
    sheet.paste(base_imgs[i], (i * 240, 0))
    sheet.paste(cand_imgs[i], (i * 240, 240))
sheet.save(output / "compare-baseline-over-candidate.png")
cand_sheet = Image.new("RGB", (480, 480))  # 2 x 2: live | no-Argon, partial | partial with grow-right
for i in range(4):
    cand_sheet.paste(cand_imgs[i], ((i % 2) * 240, (i // 2) * 240))
cand_sheet.resize((960, 960), Image.Resampling.NEAREST).save(output / "candidate-2x.png")

(output / "report.md").write_text(
    "# Antigravity (agy) pixel preview\n\n"
    f"Candidate source SHA256: {hashlib.sha256(cand_src.encode()).hexdigest()}\n"
    f"Baseline (git HEAD) SHA256: {hashlib.sha256(base_src.encode()).hexdigest()}\n"
    "PASS: candidate prints 'Resets in' on the top card only (bare, padded rows elsewhere); baseline prints it on all 8 cards.\n"
    "PASS: top card and whole bottom card keep size-5 values; split bottom row has two half cards (7d | Ar) with size-4 values, bare resets inside their card.\n"
    "PASS: exhausted Argon bar red, 0% bar empty, black 4 px gap between the halves, a no-Argon bottom card stays whole.\n")
print(output)
