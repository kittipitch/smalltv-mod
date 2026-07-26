#include "CalendarMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"

CalendarMode g_calendarMode;

// Local palette subset (same Anthropic-inspired hex values UsageMode.cpp
// uses — not shared/exported anywhere yet, so each mode defines what it needs).
#define C_ACCENT  0xDBAA   // terra-cotta 0xd97757 — hero line, "when"
#define C_UGREEN  0x7C6B   // sage green 0x788c5d — AQI good band
#define C_DIM     0xB574   // secondary/placeholder text, warm grey

static const char* MONTH3[] = {
  "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};

// ISO8601 "YYYY-MM-DDTHH:MM:SS[+-]HH:MM"/"Z" -> "HH:MM" (offset 11 is fixed
// for any compliant timestamp, so a substring beats a full datetime parser).
static void extractTimeHHMM(const char* iso, char* out, size_t n) {
  if (strlen(iso) < 16 || n < 6) { strlcpy(out, "--:--", n); return; }
  strlcpy(out, iso + 11, 6);   // "HH:MM" + NUL, n>=6 guaranteed above
}

// ISO date "YYYY-MM-DD" -> "Mon DD" for all-day events (no time-of-day to show).
static void extractMonthDay(const char* iso, char* out, size_t n) {
  if (strlen(iso) < 10) { strlcpy(out, "--", n); return; }
  int mon = (iso[5] - '0') * 10 + (iso[6] - '0');
  int day = (iso[8] - '0') * 10 + (iso[9] - '0');
  if (mon < 1 || mon > 12) { strlcpy(out, "--", n); return; }
  snprintf(out, n, "%s %d", MONTH3[mon - 1], day);
}

// Hard-truncate to maxChars, appending ".." when the source is longer (this
// font has no ellipsis glyph). maxChars includes the ".." itself.
static void truncateDots(const char* in, char* out, size_t outCap, int maxChars) {
  int len = (int)strlen(in);
  if (len <= maxChars) { strlcpy(out, in, outCap); return; }
  if (outCap < 4) { strlcpy(out, "..", outCap); return; }
  int keep = maxChars - 2;
  if (keep < 1) keep = 1;
  if ((size_t)(keep + 3) > outCap) keep = (int)outCap - 3;
  memcpy(out, in, keep);
  out[keep] = '.'; out[keep + 1] = '.'; out[keep + 2] = 0;
}

static void drawRow(Arduino_GFX* gfx, int y, uint8_t size, uint16_t color, const char* text) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(14, y);
  gfx->print(text);
}

// ---- render -----------------------------------------------------------------
static void drawCalendar(const CalendarEvent& c, const WeatherData& w) {
  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);

  // Row 0 — eyebrow label (size 2, the one place the size floor allows it —
  // a pure category tag, not information).
  bool connected = c.valid;
  drawRow(gfx, 14, 2, C_DIM, connected ? "NEXT EVENT" : "CALENDAR");

  // Row 1 — hero: time of day (or date for all-day, or a placeholder).
  char hero[12];
  uint16_t heroColor = C_ACCENT;
  if (!connected) {
    strlcpy(hero, "--:--", sizeof(hero));
    heroColor = C_DIM;
  } else if (!c.hasEvent) {
    strlcpy(hero, "--:--", sizeof(hero));
    heroColor = C_DIM;
  } else if (c.allDay) {
    extractMonthDay(c.start, hero, sizeof(hero));
  } else {
    extractTimeHHMM(c.start, hero, sizeof(hero));
  }
  drawRow(gfx, 40, 4, heroColor, hero);

  // Row 2 — title / status text (up to 11 chars at size 3 fit the 212px
  // content width; longer titles truncate with "..").
  char titleBuf[16];
  if (!connected) {
    strlcpy(titleBuf, "No data", sizeof(titleBuf));
  } else if (!c.hasEvent) {
    strlcpy(titleBuf, "No events", sizeof(titleBuf));
  } else {
    truncateDots(c.summary, titleBuf, sizeof(titleBuf), 11);
  }
  drawRow(gfx, 86, 3, connected && c.hasEvent ? C_WHITE : C_DIM, titleBuf);

  // Row 3 — weather (temp + precip degrade together: they come from one API
  // call in practice, so a partial "temp but no precip" reads as noise).
  char wxBuf[24];
  bool wxOk = w.hasTemp || w.hasPrecip;
  if (!wxOk) {
    strlcpy(wxBuf, "Weather --", sizeof(wxBuf));
  } else {
    char t[8] = "", p[8] = "";
    if (w.hasTemp)   snprintf(t, sizeof(t), "%dC", (int)lroundf(w.tempC));
    if (w.hasPrecip) snprintf(p, sizeof(p), "%d%%", w.precipPct);
    if (t[0] && p[0]) snprintf(wxBuf, sizeof(wxBuf), "%s  %s", t, p);
    else strlcpy(wxBuf, t[0] ? t : p, sizeof(wxBuf));
  }
  drawRow(gfx, 140, 3, wxOk ? C_WHITE : C_DIM, wxBuf);

  // Row 4 — AQI, 3-tier band color (the palette has no distinct yellow/amber,
  // so US AQI's 4 bands collapse to 3: good / moderate-to-unhealthy-for-
  // sensitive / unhealthy — a deliberate, documented simplification, not a bug).
  char aqiBuf[16];
  uint16_t aqiColor = C_DIM;
  if (w.hasAqi) {
    snprintf(aqiBuf, sizeof(aqiBuf), "AQI %d", w.aqi);
    aqiColor = (w.aqi <= 50) ? C_UGREEN : (w.aqi <= 150) ? C_ACCENT : C_RED;
  } else {
    strlcpy(aqiBuf, "AQI --", sizeof(aqiBuf));
  }
  drawRow(gfx, 176, 3, aqiColor, aqiBuf);

  // Row 5 — footnote (PM2.5), the one row allowed to just not exist.
  if (w.hasPm25) {
    char pm[16];
    snprintf(pm, sizeof(pm), "PM2.5 %.1f", w.pm25);
    drawRow(gfx, 212, 2, C_DIM, pm);
  }
}

// ---- DisplayMode ------------------------------------------------------------
void CalendarMode::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
  wxRenderedOk_ = 0xFFFFFFFF;
}

void CalendarMode::invalidate(const Settings& s) {
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
  wxRenderedOk_ = 0xFFFFFFFF;
  calendarInit(s);
  calendarForceRefresh();
}

void CalendarMode::service(const Settings& s) {
  calendarService(s);   // weather/AQI on its own poll schedule; calendar arrives via push

  const CalendarEvent& c = calendarGet();
  const WeatherData&   w = weatherGet();
  if (c.lastOkMs != calRenderedOk_) { calRenderedOk_ = c.lastOkMs; needRender_ = true; }
  if (w.lastOkMs != wxRenderedOk_)  { wxRenderedOk_  = w.lastOkMs;  needRender_ = true; }

  if (needRender_) {
    drawCalendar(c, w);
    needRender_ = false;
  }
}
