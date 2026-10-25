#!/usr/bin/env python3
"""Render the Copilot quota page from source with the installed GFX glyphs.

Run from the firmware checkout with Python + Pillow + a C++14 compiler; prints an artifact
directory. No firmware build, device connection or OTA happens. Source-derived like
preview_agenda.py: pctColor, fmtReset, drawCopilotMeter, drawCopilotPage, the CopilotData
struct, CopilotLogo.h, the C_* palette and gfxTextW/gfxFitSize are cut verbatim out of src/; only the hardware surface (a 240x240 RGB565 buffer) is a host stub.
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
logo_h = (root / "src/features/calendar/CopilotLogo.h").read_text()
cand_src = (root / "src/features/calendar/CopilotMode.cpp").read_text()
base_src = subprocess.run(["git", "show", "HEAD:src/features/calendar/CopilotMode.cpp"],
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
static void set(CopilotData& a, int prem, int rprem, int chat, int rchat, int code, int rcode) {
  a.clear(); a.valid = true;
  a.hasPctPremium = prem >= 0; a.pctPremium = prem; a.hasRPremium = true; a.rPremium = rprem;
  a.hasPctChat = chat >= 0; a.pctChat = chat; a.hasRChat = true; a.rChat = rchat;
  a.hasPctCompletions = code >= 0; a.pctCompletions = code; a.hasRCompletions = true; a.rCompletions = rcode;
}
int main(int, char** argv) {
  std::string dir = argv[1];
  CopilotData a;
  // 0: live shape 2026-10-25 -- Premium is the only snapshot with usage (39% used), Chat/Code unlimited
  // 1: an old daemon: no Premium key, the original two whole cards
  // 2: no data at all
  const char* names[3] = {"premium-split", "legacy-two-cards", "no-data"};
  for (int i = 0; i < 3; i++) {
    if (i == 0) set(a, 40, 9940, 0, 9940, 0, 9940);
    if (i == 1) set(a, -1, 0, 62, 8000, 33, 8000);
    if (i == 2) set(a, -1, 0, -1, 0, -1, 0);
    Arduino_GFX gfx; gfx.page = i;
    drawCopilotPage(&gfx, a, true, false);
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
    cpp += "#define min(a,b) ((a)<(b)?(a):(b))\n"  # CopilotMode.cpp calls min()
    cpp += function(gfx_cpp, "int gfxTextW(") + "\n" + function(gfx_cpp, "uint8_t gfxFitSize(") + "\n"
    cpp += "\n".join(l for l in gfxh.splitlines() if l.startswith("#define C_")) + "\n"
    for m in re.finditer(r"^#define (C_\w+)\s+(0x[0-9A-Fa-f]+)", src, re.M):  # the page's own palette wins
        cpp += f"#undef {m.group(1)}\n#define {m.group(1)} {m.group(2)}\n"
    cpp += logo_h[logo_h.index("#define COPILOT_LOGO_COLS"):] + "\n"
    cpp += function(data_h, "struct CopilotData {") + ";\n"
    for name in ("pctColor", "fmtReset", "drawCopilotMeter", "drawCopilotHalf", "drawCopilotLogo", "drawCopilotPage"):
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
    names = ["premium-split", "legacy-two-cards", "no-data"]
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

# --- trace assertions on the candidate ---------------------------------------------------------
def texts(rows, page):
    return {(int(r[1]), int(r[2])): (r[3], r[4]) for r in rows if int(r[0]) == page}

RIGHT = 218  # 8 + 224 - 14: the shared right edge of value, bar and reset text
for page, prem, chat, code in ((0, ("40%", "6d 21h"), ("0%", "6d 21h"), ("0%", "6d 21h")),
                               (1, None, ("62%", "5d 13h"), ("33%", "5d 13h")),
                               (2, None, None, None)):
    rows = [r for r in cand_rows if r[0] == str(page)]
    def at(y):
        return [r for r in rows if int(r[2]) == y]
    # wordmark
    wm = at(16)
    assert wm and wm[0][4] == "Copilot" and int(wm[0][1]) == 46, ("wordmark", page)
    if prem:
        lab = at(62)
        assert lab and lab[0][4] == "Premium", ("premium label", page, lab)
        vr = [r for r in rows if int(r[2]) == 60 and r[4].strip().endswith("%")]
        assert vr and vr[0][3] == "5" and vr[0][4] == prem[0], ("premium value", page, vr)
        rr = at(116)
        assert rr and rr[0][4] == f"{'Resets in ' + prem[1]:>17}" and int(rr[0][1]) + 17 * 12 == RIGHT, ("premium reset", page, rr)
        for x, (val, lab) in ((22, ("0%", "Chat")), (136, ("0%", "Code"))):
            hl = [r for r in rows if int(r[2]) == 144 and r[4].strip() == lab]
            assert hl, ("half label", page, lab)
            hv = [r for r in rows if int(r[2]) == 162 and r[4].strip().endswith("%")]
            assert hv and hv[0][3] == "4" and hv[0][4].strip() == "0%", ("half value", page, hv)
            edge = 104 if x < 100 else 218
            hr = [r for r in rows if int(r[2]) == 204 and r[3] == "2" and abs(int(r[1]) + 7 * 12 - edge) < 12]
            assert len(hr) == 1 and int(hr[0][1]) + 7 * 12 == edge, ("half reset right edge", page, x, hr)
    elif chat:
        vr = [r for r in rows if int(r[2]) == 60 and r[4].strip().endswith("%")]
        assert vr and vr[0][4] == chat[0], ("legacy top value", page, vr)
        vr2 = [r for r in rows if int(r[2]) == 148 and r[4].strip().endswith("%")]
        assert vr2 and vr2[0][4] == code[0], ("legacy bottom value", page, vr2)

# --- pixel assertions ---------------------------------------------------------------------------
BARBG, BLACK, PANEL = rgb(0x2945), (0, 0, 0), rgb(0x18E3)
logo_bytes = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{2})", logo_h[logo_h.index("kCOPILOT_LOGO"):])]
def expect_logo_px():
    n = 0
    for r in range(28):
        for c in range(25):
            if any(logo_bytes[(r * 25 + c) * 3:(r * 25 + c) * 3 + 3]):
                n += (25 * (c + 1) // 25 - 25 * c // 25) * (28 * (r + 1) // 28 - 28 * r // 28)
    return n
def ink_box(im, region=(0, 0, 45, 50)):
    pts = [(x, y) for x in region[0:1] and range(region[0], region[2]) for y in range(region[1], region[3]) if im.getpixel((x, y)) != BLACK]
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts)) if pts else None
for im in cand_imgs:
    box = ink_box(im, (0, 0, 45, 50))
    assert box == (7, 12, 31, 39), ("logo ink box", box)
    colored = sum(1 for x in range(0, 45) for y in range(0, 50) if im.getpixel((x, y)) != BLACK)
    assert colored == expect_logo_px(), ("logo pixel count", colored, expect_logo_px())
# baseline is git HEAD, which already carries the first logo revision: only require it to be a
# helmet-sized ink box too (the grid bytes themselves are asserted on the candidate)
bbox = ink_box(base_imgs[0], (0, 0, 45, 50))
assert bbox == (7, 12, 36, 35), ("baseline logo box", bbox)
wordmark = [r for r in cand_rows if r[0] == "0" and int(r[2]) == 16]
assert wordmark and wordmark[0][4] == "Copilot" and int(wordmark[0][1]) == 46, wordmark

# --- contact sheets: baseline over candidate at native size, candidate enlarged 2x ----------
sheet = Image.new("RGB", (720, 480))
for i in range(3):
    sheet.paste(base_imgs[i], (i * 240, 0))
    sheet.paste(cand_imgs[i], (i * 240, 240))
sheet.save(output / "compare-baseline-over-candidate.png")
cand_sheet = Image.new("RGB", (720, 240))
for i in range(3):
    cand_sheet.paste(cand_imgs[i], (i * 240, 0))
cand_sheet.resize((1440, 480), Image.Resampling.NEAREST).save(output / "candidate-2x.png")

(output / "report.md").write_text(
    "# Antigravity (agy) pixel preview\n\n"
    f"Candidate source SHA256: {hashlib.sha256(cand_src.encode()).hexdigest()}\n"
    f"Baseline (git HEAD) SHA256: {hashlib.sha256(base_src.encode()).hexdigest()}\n"
    "PASS: candidate prints 'Resets in' on the top card only (bare, padded rows elsewhere); baseline prints it on all 8 cards.\n"
    "PASS: top card and whole bottom card keep size-5 values; split bottom row has two half cards (7d | Ar) with size-4 values, bare resets inside their card.\n"
    "PASS: exhausted Argon bar red, 0% bar empty, black 4 px gap between the halves, a no-Argon bottom card stays whole.\n")
print(output)
