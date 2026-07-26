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
#define C_SKY     0x5D9C   // muted blue — cloud/rain icon strokes

// How long each of the 3 sub-pages (agenda/weather/AQI) stays on screen
// before auto-rotating to the next, independent of the main mode carousel.
static const uint32_t PAGE_DWELL_MS = 4000;

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

// Weather-code category, per Open-Meteo's WMO table (current.weather_code):
// https://open-meteo.com/en/docs — codes 0..99, grouped for icon purposes.
enum WxCat { WX_CLEAR, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM, WX_UNKNOWN };

static WxCat wxCategory(int code, bool has) {
  if (!has) return WX_UNKNOWN;
  if (code == 0 || code == 1) return WX_CLEAR;
  if (code == 2 || code == 3) return WX_CLOUD;
  if (code == 45 || code == 48) return WX_FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WX_RAIN;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
  if (code >= 95) return WX_STORM;
  return WX_UNKNOWN;
}

// Small vector icon, centered at (cx,cy), radius-scaled by r. Deliberately
// simple shapes (circle/arcs/lines) — no bitmap assets, no image fetch; see
// CalendarMode.h's header comment for why.
static void drawWeatherIcon(Arduino_GFX* gfx, int cx, int cy, int r, WxCat cat) {
  switch (cat) {
    case WX_CLEAR:
      gfx->fillCircle(cx, cy, r * 6 / 10, C_ACCENT);
      for (int i = 0; i < 8; i++) {
        float a = i * (2 * PI / 8);
        int x0 = cx + (int)(cosf(a) * r * 0.75f), y0 = cy + (int)(sinf(a) * r * 0.75f);
        int x1 = cx + (int)(cosf(a) * r), y1 = cy + (int)(sinf(a) * r);
        gfx->drawLine(x0, y0, x1, y1, C_ACCENT);
      }
      break;
    case WX_FOG:
      for (int i = -1; i <= 1; i++)
        gfx->drawFastHLine(cx - r, cy + i * (r / 2), r * 2, C_DIM);
      break;
    case WX_STORM:
    case WX_RAIN:
      gfx->fillRoundRect(cx - r, cy - r / 3, r * 2, r, r / 3, C_DIM);
      gfx->fillCircle(cx - r / 2, cy - r / 3, r / 2, C_DIM);
      gfx->fillCircle(cx + r / 2, cy - r / 2, r * 6 / 10, C_DIM);
      for (int i = -1; i <= 1; i++)
        gfx->drawLine(cx + i * (r / 2), cy + r / 2, cx + i * (r / 2) - 3, cy + r, C_SKY);
      if (cat == WX_STORM)
        gfx->drawLine(cx, cy + r / 3, cx + r / 3, cy + r, C_ACCENT);
      break;
    case WX_SNOW:
      gfx->fillRoundRect(cx - r, cy - r / 3, r * 2, r, r / 3, C_DIM);
      gfx->fillCircle(cx - r / 2, cy - r / 3, r / 2, C_DIM);
      gfx->fillCircle(cx + r / 2, cy - r / 2, r * 6 / 10, C_DIM);
      for (int i = -1; i <= 1; i++)
        gfx->fillCircle(cx + i * (r / 2), cy + r, 2, C_WHITE);
      break;
    case WX_CLOUD:
      gfx->fillRoundRect(cx - r, cy - r / 3, r * 2, r, r / 3, C_SKY);
      gfx->fillCircle(cx - r / 2, cy - r / 3, r / 2, C_SKY);
      gfx->fillCircle(cx + r / 2, cy - r / 2, r * 6 / 10, C_SKY);
      break;
    case WX_UNKNOWN:
    default:
      gfx->drawCircle(cx, cy, r * 6 / 10, C_DIM);
      gfx->setTextSize(3);
      gfx->setTextColor(C_DIM);
      gfx->setCursor(cx - 6, cy - 12);
      gfx->print("?");
      break;
  }
}

// ---- render (3 sub-pages, rotated on a timer by CalendarMode::service) ------
static void drawAgendaPage(Arduino_GFX* gfx, const CalendarEvent& c) {
  gfx->fillScreen(C_BLACK);
  bool connected = c.valid;
  drawRow(gfx, 14, 2, C_DIM, connected ? "NEXT EVENT" : "CALENDAR");

  char hero[12];
  uint16_t heroColor = C_ACCENT;
  if (!connected || !c.hasEvent) {
    strlcpy(hero, "--:--", sizeof(hero));
    heroColor = C_DIM;
  } else if (c.allDay) {
    extractMonthDay(c.start, hero, sizeof(hero));
  } else {
    extractTimeHHMM(c.start, hero, sizeof(hero));
  }
  drawRow(gfx, 50, 4, heroColor, hero);

  char titleBuf[16];
  if (!connected) {
    strlcpy(titleBuf, "No data", sizeof(titleBuf));
  } else if (!c.hasEvent) {
    strlcpy(titleBuf, "No events", sizeof(titleBuf));
  } else {
    truncateDots(c.summary, titleBuf, sizeof(titleBuf), 11);
  }
  drawRow(gfx, 110, 3, connected && c.hasEvent ? C_WHITE : C_DIM, titleBuf);
}

static void drawWeatherPage(Arduino_GFX* gfx, const WeatherData& w) {
  gfx->fillScreen(C_BLACK);
  drawRow(gfx, 14, 2, C_DIM, "WEATHER");

  bool wxOk = w.hasTemp || w.hasPrecip;
  WxCat cat = wxCategory(w.weatherCode, w.hasWeatherCode);
  drawWeatherIcon(gfx, 106, 90, 34, wxOk ? cat : WX_UNKNOWN);

  char t[10] = "--";
  if (w.hasTemp) snprintf(t, sizeof(t), "%dC", (int)lroundf(w.tempC));
  drawRow(gfx, 150, 4, wxOk ? C_WHITE : C_DIM, t);

  if (w.hasPrecip) {
    char p[16];
    snprintf(p, sizeof(p), "Rain %d%%", w.precipPct);
    drawRow(gfx, 196, 3, C_SKY, p);
  }
}

static void drawAqiPage(Arduino_GFX* gfx, const WeatherData& w) {
  gfx->fillScreen(C_BLACK);
  drawRow(gfx, 14, 2, C_DIM, "AIR QUALITY");

  // 3-tier band color (the palette has no distinct yellow/amber, so US AQI's
  // 4 bands collapse to 3: good / moderate-to-unhealthy-for-sensitive /
  // unhealthy — a deliberate, documented simplification, not a bug).
  char aqiBuf[16];
  uint16_t aqiColor = C_DIM;
  if (w.hasAqi) {
    snprintf(aqiBuf, sizeof(aqiBuf), "%d", w.aqi);
    aqiColor = (w.aqi <= 50) ? C_UGREEN : (w.aqi <= 150) ? C_ACCENT : C_RED;
  } else {
    strlcpy(aqiBuf, "--", sizeof(aqiBuf));
  }
  drawRow(gfx, 60, 5, aqiColor, aqiBuf);

  if (w.hasPm25) {
    char pm[16];
    snprintf(pm, sizeof(pm), "PM2.5 %.1f", w.pm25);
    drawRow(gfx, 150, 3, C_DIM, pm);
  } else {
    drawRow(gfx, 150, 3, C_DIM, "PM2.5 --");
  }
}

// ---- DisplayMode ------------------------------------------------------------
void CalendarMode::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
  wxRenderedOk_ = 0xFFFFFFFF;
  page_ = PAGE_AGENDA;
  nextPageMs_ = millis() + PAGE_DWELL_MS;
}

void CalendarMode::invalidate(const Settings& s) {
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
  wxRenderedOk_ = 0xFFFFFFFF;
  page_ = PAGE_AGENDA;
  nextPageMs_ = millis() + PAGE_DWELL_MS;
  calendarInit(s);
  calendarForceRefresh();
}

void CalendarMode::service(const Settings& s) {
  calendarService(s);   // weather/AQI on its own poll schedule; calendar arrives via push

  const CalendarEvent& c = calendarGet();
  const WeatherData&   w = weatherGet();
  if (c.lastOkMs != calRenderedOk_) { calRenderedOk_ = c.lastOkMs; needRender_ = true; }
  if (w.lastOkMs != wxRenderedOk_)  { wxRenderedOk_  = w.lastOkMs;  needRender_ = true; }

  if ((int32_t)(millis() - nextPageMs_) >= 0) {
    page_ = (Page)((page_ + 1) % PAGE_COUNT);
    nextPageMs_ = millis() + PAGE_DWELL_MS;
    needRender_ = true;
  }

  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) {
      switch (page_) {
        case PAGE_AGENDA:  drawAgendaPage(gfx, c); break;
        case PAGE_WEATHER: drawWeatherPage(gfx, w); break;
        case PAGE_AQI:     drawAqiPage(gfx, w); break;
        default: break;
      }
    }
    needRender_ = false;
  }
}
