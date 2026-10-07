#!/usr/bin/env python3
"""Render the current agenda function with the installed GFX glyphs and metrics.

Run with Python and Pillow installed; prints a unique artifact directory.
No firmware build or device connection is performed.
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
source_path = root / "src/features/calendar/CalendarMode.cpp"
source = source_path.read_text()
library = root / ".pio/libdeps/smalltv/GFX Library for Arduino/src"
gfx_source = (library / "Arduino_GFX.cpp").read_text()
output = Path(tempfile.mkdtemp(prefix="agenda-preview-", dir="/tmp"))
report = output / "report.md"
report.write_text("# Agenda pixel preview\n\n" +
                  f"Source: {source_path}\nSHA256: {hashlib.sha256(source.encode()).hexdigest()}\n" +
                  "Renderer and date helpers extracted verbatim; GFX font and roundrect routines reused.\n")

# Only the hardware, clock, and Print sink are replaced. No layout copy exists.
cpp = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cassert>
#define pgm_read_byte(p) (*(p))
struct String : std::string {
  using std::string::string;
  String(std::string s): std::string(s) {}
  int length() const { return (int)size(); }
  String substring(int start, int end) const { return substr(start, end-start); }
  void trim() {
    auto first = find_first_not_of(" \t\r\n");
    if (first == npos) { clear(); return; }
    *this = substr(first, find_last_not_of(" \t\r\n") - first + 1);
  }
  int toInt() const { return std::atoi(c_str()); }
};
struct Settings { struct { String ids, colorIds; } calendar; };
tm previewNow = {};
bool previewClockReady = true;
bool clockNow(tm& now) { now = previewNow; return previewClockReady; }
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
  void writeFillRect(int x,int y,int w,int h,uint16_t color) {
    for(int yy=std::max(0,y); yy<std::min(240,y+h); ++yy)
      for(int xx=std::max(0,x); xx<std::min(240,x+w); ++xx) pixels[yy*240+xx]=color;
  }
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
font = (library / "font/glcdfont.h").read_text()
cpp = font + cpp
for name in ("writeFillEllipseHelper", "fillRoundRect"):
    cpp += function(gfx_source, f"void Arduino_GFX::{name}(").replace("Arduino_GFX::", "")
draw_char = function(gfx_source, "void Arduino_GFX::drawChar(")
default = draw_char[draw_char.index("  {", draw_char.index("else // glcdfont")):-2]
cpp += "void drawChar(int16_t x,int16_t y,unsigned char c,uint16_t color,uint16_t bg) {\n"
cpp += "int16_t block_w,block_h,curX,curY,curH;\n" + default + "\n}\n};\n"
data = (root / "src/features/calendar/CalendarData.h").read_text()
cpp += data[data.index("#define CAL_TITLE_LEN"):data.index("// Filled either by a daemon push")]
cpp += (root / "src/features/calendar/CalendarWeekday.h").read_text().replace("#pragma once", "")
cpp += "\n#define C_BLACK 0x0000\n#define C_WHITE 0xffff\n#define C_ACCENT 0xDBAA\n#define C_DIM 0xB574\n#define C_PANEL 0x18E3\n#define EVENTS_PER_PAGE 3\n"
cpp += source[source.index("static const uint16_t kGCalPalette"):source.index("// Muted 6-band US AQI")]
cpp += source[source.index("static const char* MONTH3"):source.index("static void drawRow(")]
for name in ("isSameLocalDay", "parseIsoDate", "decCalendarDay", "formatDayRange", "drawAgendaPage"):
    match = re.search(r"static (?:bool|void) " + name + r"\(", source)
    cpp += function(source, match.group()) + "\n"
cpp += r'''
void event(CalendarEvent& c,int index,const char* title,const char* start,const char* end,bool allDay) {
  auto& e=c.items[index];
  strlcpy(e.summary,title,sizeof(e.summary)); strlcpy(e.start,start,sizeof(e.start));
  strlcpy(e.end,end,sizeof(e.end)); e.hasEnd=true; e.allDay=allDay;
}
int main(int argc,char** argv) {
  (void)argc;
  previewNow.tm_year=126; previewNow.tm_mon=9; previewNow.tm_mday=8;
  CalendarEvent c={}; c.valid=true; c.count=6;
  event(c,0,"Today meeting","2026-10-08T14:30:00","2026-10-08T15:30:00",false);
  event(c,1,"Tomorrow meeting","2026-10-09T09:30:00","2026-10-09T10:30:00",false);
  event(c,2,"Same-month trip","2026-10-12","2026-10-15",true);
  event(c,3,"Cross-month trip","2026-10-31T10:00:00","2026-11-02T17:00:00",false);
  event(c,4,"Future meeting","2026-11-04T09:30:00","2026-11-04T10:30:00",false);
  event(c,5,"Future all-day","2026-11-05","2026-11-06",true);
  Settings s;
  for(int page=0;page<2;++page) {
    Arduino_GFX gfx; gfx.page=page;
    drawAgendaPage(&gfx,s,c,page);
    std::ofstream image(std::string(argv[1])+"/page"+std::to_string(page+1)+".ppm",std::ios::binary);
    image<<"P6\n240 240\n255\n";
    for(uint16_t p:gfx.pixels) {
      char rgb[3]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};
      image.write(rgb,3);
    }
  }
  // Exercise the actual renderer at calendar/DST boundaries and without RTC.
  const char* dates[] = {"2027-01-01", "2028-02-29", "2026-03-09", "2026-11-02"};
  const int years[] = {126,128,126,126}, months[] = {11,1,2,10}, days[] = {31,28,8,1};
  setenv("TZ","America/New_York",1); tzset();
  for(int i=0;i<4;++i) {
    previewNow={}; previewNow.tm_year=years[i]; previewNow.tm_mon=months[i]; previewNow.tm_mday=days[i];
    CalendarEvent edge={}; edge.valid=true; edge.count=1;
    event(edge,0,"Boundary",dates[i],dates[i],false);
    std::ostringstream trace; auto* old=std::cout.rdbuf(trace.rdbuf());
    Arduino_GFX gfx; drawAgendaPage(&gfx,s,edge,0); std::cout.rdbuf(old);
    assert(trace.str().find("\tTomorrow\n")!=std::string::npos);
  }
  previewClockReady=false;
  CalendarEvent edge={}; edge.valid=true; edge.count=1;
  event(edge,0,"No clock","2026-10-08","2026-10-09",true);
  std::ostringstream unsynced; auto* old=std::cout.rdbuf(unsynced.rdbuf());
  Arduino_GFX gfx; drawAgendaPage(&gfx,s,edge,0); std::cout.rdbuf(old);
  assert(unsynced.str().find("\tTh\n")!=std::string::npos);
  assert(unsynced.str().find("\tToday\n")==std::string::npos);
}
'''
host_source = output / "preview.cpp"
host_source.write_text(cpp)
binary = output / "preview"
subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", str(host_source), "-o", str(binary)], check=True)
trace = subprocess.run([str(binary), str(output)], text=True, capture_output=True, check=True).stdout
(output / "trace.tsv").write_text(trace)
rows = [line.split("\t", 4) for line in trace.splitlines()]
labels = [r[4] for r in rows]
assert labels == ["Today", "14:30", "Today meeting", "Tomorrow", "09:30", "Tomorrow meeting",
                  "Mo", "Oct 12-14", "Same-month trip", "Sa", "Oct 31-Nov 2", "Cross-month trip",
                  "We", "Nov 4", "09:30", "Future meeting", "Th", "Nov 5", "Future all-day"], labels
for page, date in (("0", "Oct 12-14"), ("1", "Oct 31-Nov 2"),
                   ("1", "Nov 4"), ("1", "Nov 5")):
    i = next(i for i, r in enumerate(rows) if r[0] == page and r[4] == date)
    assert int(rows[i-1][1]) == 20 and int(rows[i][1]) == 50
for row in rows:
    if row[4] in ("14:30", "09:30"):
        assert int(row[1]) == 160
assert all(int(r[1]) + len(r[4])*6*int(r[3]) <= 220 for r in rows)
contact = Image.new("RGB", (480, 240))
for page in (1, 2):
    image = Image.open(output / f"page{page}.ppm")
    image.save(output / f"page{page}.png")
    contact.paste(image, ((page-1)*240, 0))
contact.resize((960, 480), Image.Resampling.NEAREST).save(output / "both-pages.png")
with report.open("a") as file:
    file.write("PASS: exact print trace; six date/weekday pairs; two HH:MM labels only; no row overflows.\n")
    file.write("PASS: Today/Tomorrow without weekday, future all-day, same-month all-day range (exclusive end), cross-month timed range, future timed.\n")
print(output)
