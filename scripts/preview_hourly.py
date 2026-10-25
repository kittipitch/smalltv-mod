#!/usr/bin/env python3
"""Render the next-3-hours weather page AND the 3-day forecast page from source, side by side.

Source-derived like the other previews: the palette, WxCat/wxCategory/wxLabel, drawWeatherIcon,
the page functions, WeatherIcons.h and the WeatherData structs are cut verbatim out of src/; only
the hardware surface is a host stub. Output: a PNG sheet (hourly fixtures + the forecast page for
design comparison) in a fresh /tmp directory."""
from pathlib import Path
import re
import subprocess
import tempfile

from PIL import Image


def function(source, signature):
    # the DEFINITION, not a forward declaration: its "{" comes before any ";"
    start = -1
    pos = 0
    while True:
        pos = source.index(signature, pos)
        nxt_brace = source.index("{", pos)
        nxt_semi = source.index(";", pos)
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
gfxh = (root / "src/Gfx.h").read_text()
gfx_cpp = (root / "src/Gfx.cpp").read_text()
data_h = (root / "src/features/calendar/CalendarData.h").read_text()
icons_h = (root / "src/features/calendar/WeatherIcons.h").read_text()
cm = (root / "src/features/calendar/CalendarMode.cpp").read_text()
output = Path(tempfile.mkdtemp(prefix="hourly-preview-", dir="/tmp"))

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
  void drawFastHLine(int x,int y,int w,uint16_t c) { writeFillRect(x,y,w,1,c); }
  void drawCircle(int x0,int y0,int r,uint16_t c) {   // midpoint circle, preview-only
    int x=r, y=0, err=1-r;
    while (x>=y) {
      int px[8]={x,y,-y,-x,-x,-y,y,x}, py[8]={y,x,x,y,-y,-x,-x,-y};
      for (int k=0;k<8;k++) writePixel(x0+px[k], y0+py[k], c);
      y++; if (err<0) err+=2*y+1; else { x--; err+=2*(y-x)+1; }
    }
  }
  void drawFastVLine(int x,int y,int h,uint16_t c) { writeFillRect(x,y,1,h,c); }
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
static void hour(WeatherData& w, int i, const char* t, int code, int temp, int precip, int aqi) {
  HourFc& h = w.hc[i];
  strlcpy(h.time, t, sizeof(h.time)); h.code = code; h.temp = temp; h.precip = precip;
  h.aqi = aqi; h.hasAqi = aqi >= 0;
  h.hasIsDay = true; h.isDay = (t[0]=='0' && t[1]>='6') || (t[0]=='1' && t[1]<'9');
}
int main(int, char** argv) {
  std::string dir = argv[1];
  const char* names[4] = {"hourly-live", "hourly-mixed", "hourly-noaqi", "forecast"};
  for (int i = 0; i < 4; i++) {
    WeatherData w; w.clear(); w.valid = true;
    if (i == 0) { w.hcCount = 3; hour(w,0,"19:00",1,30,25,54); hour(w,1,"20:00",1,29,17,62); hour(w,2,"21:00",1,28,8,66); }
    if (i == 1) { w.hcCount = 3; hour(w,0,"06:00",95,24,94,112); hour(w,1,"07:00",63,23,100,155); hour(w,2,"08:00",3,-5,40,34); }
    if (i == 2) { w.hcCount = 3; hour(w,0,"12:00",45,18,5,-1); hour(w,1,"13:00",71,2,60,-1); hour(w,2,"14:00",2,35,0,301); }
    if (i == 3) {
      w.fcCount = 3; w.hasForecast = true;
      const char* dn[3] = {"Tmr","Tue","Wed"}; int cd[3] = {95,1,63}; int hi[3]={30,33,31}; int lo[3]={24,25,24}; int pr[3]={94,20,100}; int aq[3]={71,66,58};
      for (int k = 0; k < 3; k++) { strlcpy(w.fc[k].day, dn[k], 6); w.fc[k].code=cd[k]; w.fc[k].hi=hi[k]; w.fc[k].lo=lo[k]; w.fc[k].precip=pr[k]; w.fc[k].aqi=aq[k]; w.fc[k].hasAqi=true; }
    }
    Arduino_GFX gfx; gfx.page = i;
    if (i < 3) drawHourlyPage(&gfx, w); else drawForecastPage(&gfx, w);
    emit(gfx, dir + "/" + names[i] + ".ppm");
  }
  return 0;
}
'''


cpp = STUB
for name in ("writeFillEllipseHelper", "fillRoundRect", "writeEllipseHelper", "drawRoundRect", "fillCircle"):
    cpp += function(gfx_source, f"void Arduino_GFX::{name}(").replace("Arduino_GFX::", "")
draw_char = function(gfx_source, "void Arduino_GFX::drawChar(")
default = draw_char[draw_char.index("  {", draw_char.index("else // glcdfont")):-2]
cpp += "void drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg) {\n"
cpp += "int16_t block_w,block_h,curX,curY,curH;\n" + default + "\n}\n};\n"
cpp += "#define min(a,b) ((a)<(b)?(a):(b))\n#define max(a,b) ((a)>(b)?(a):(b))\n"
cpp += function(gfx_cpp, "int gfxTextW(") + "\n" + function(gfx_cpp, "uint8_t gfxFitSize(") + "\n"
cpp += "\n".join(l for l in gfxh.splitlines() if l.startswith("#define C_")) + "\n"
for m in re.finditer(r"^#define (C_\w+)\s+(0x[0-9A-Fa-f]+)", cm, re.M):
    cpp += f"#undef {m.group(1)}\n#define {m.group(1)} {m.group(2)}\n"
icons = "\n".join(l for l in icons_h.splitlines() if not l.startswith(("#include", "#pragma")))
cpp += icons + "\n"
start = data_h.index("#define WX_CITY_LEN")
wd = data_h.index("struct WeatherData {")
end = data_h.index("\n};", wd) + 3
cpp += data_h[start:end] + "\n"
cpp += cm[cm.index("enum WxCat"):cm.index("enum WxCat") + cm[cm.index("enum WxCat"):].index("\n") + 1]
for sig in ("static const char* wxLabel(", "static bool wxHeavy(", "static WxCat wxCategory(", "static void drawWeatherIcon(",
            "static void drawHourlyEmpty(", "static void drawHourlyPage(", "static void drawForecastEmpty(",
            "static void drawForecastPage("):
    cpp += function(cm, sig) + "\n"
cpp += MAIN
work = output
(work / "preview.cpp").write_text(font + cpp)
subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-const-variable",
                "-Wno-unused-parameter", str(work / "preview.cpp"), "-o", str(work / "preview")], check=True)
subprocess.run([str(work / "preview"), str(work)], capture_output=True, check=True)
names = ["hourly-live", "hourly-mixed", "hourly-noaqi", "forecast"]
imgs = []
for n in names:
    im = Image.open(work / f"{n}.ppm")
    im.save(work / f"{n}.png")
    imgs.append(im)
sheet = Image.new("RGB", (960, 240))
for i, im in enumerate(imgs):
    sheet.paste(im, (i * 240, 0))
sheet.resize((1920, 480), Image.Resampling.NEAREST).save(work / "hourly-vs-forecast-2x.png")
print(work)
