#include "CalendarMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "WeatherIcons.h"

CalendarAgendaMode  g_calendarAgendaMode;
CalendarAgendaMode2 g_calendarAgendaMode2;
CalendarWeatherMode g_calendarWeatherMode;

// Local palette subset (same Anthropic-inspired hex values UsageMode.cpp
// uses — not shared/exported anywhere yet, so each mode defines what it needs).
#define C_ACCENT  0xDBAA   // terra-cotta 0xd97757 — hero line, "when"
#define C_UGREEN  0x7C6B   // sage green 0x788c5d — AQI good band
#define C_DIM     0xB574   // secondary/placeholder text, warm grey
#define C_SKY     0x5D9C   // muted blue — cloud/rain icon strokes
#define C_STORM   0xFE01   // bright caution-sign yellow (~#FFC107) — thunderstorm icon. WMO
                            // codes >=95 are all thunderstorm variants, no separate "plain
                            // storm" category exists; rain (51-82) already stays C_SKY/blue,
                            // unchanged. Deliberately more saturated than this palette's usual
                            // muted tones (see C_DIM/C_SKY/C_ACCENT) -- explicit request to read
                            // as a caution/warning color, not the dark theme's normal restraint.
#define C_PANEL   0x18E3   // card fill 0x1f1f1e -- same gray card UsageMode's meters use

// Muted 6-band US AQI colors (desaturated to match this palette's dark theme,
// not the harsh saturated colors AirNow-style widgets use).
#define C_AQI_GOOD             0x7C6B   // #788c5d sage green    (0-50)
#define C_AQI_MODERATE         0xBCC9   // #b89b4a muted gold    (51-100)
#define C_AQI_SENSITIVE        0xC3C8   // #c07840 muted orange  (101-150)
#define C_AQI_UNHEALTHY        0xB289   // #b5524a muted red     (151-200)
#define C_AQI_VERY_UNHEALTHY   0x8B53   // #8a6a9c muted purple  (201-300)
#define C_AQI_HAZARDOUS        0x69C7   // #6b3a3a muted maroon  (301+)

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

// Same as drawRow but horizontally centered on cx -- this font is a fixed
// 6px-per-char advance at setTextSize(1), so width = strlen(text)*6*size
// with no need for gfx->getTextBounds().
static void drawRowCentered(Arduino_GFX* gfx, int cx, int y, uint8_t size, uint16_t color, const char* text) {
  int w = (int)strlen(text) * 6 * size;
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->setCursor(cx - w / 2, y);
  gfx->print(text);
}

// Weather-code category, per Open-Meteo's WMO table (current.weather_code):
// https://open-meteo.com/en/docs — codes 0..99, grouped for icon purposes.
enum WxCat { WX_CLEAR, WX_CLOUD, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM, WX_UNKNOWN };

static const char* wxLabel(WxCat cat) {
  switch (cat) {
    case WX_CLEAR: return "Clear";
    case WX_CLOUD: return "Cloudy";
    case WX_FOG:   return "Fog";
    case WX_RAIN:  return "Rain";
    case WX_SNOW:  return "Snow";
    case WX_STORM: return "Storm";
    case WX_UNKNOWN:
    default:       return "--";
  }
}

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

// Real icon art (erikflowers/weather-icons, SIL OFL 1.1) baked into flash as
// 1-bit PROGMEM masks -- see WeatherIcons.h. Replaced the earlier hand-drawn
// vector shapes (circle+rays/roundrect+circles) per explicit request for
// nicer icons; baked-in rather than fetched live, so it doesn't reopen the
// device-side network/heap complexity that got weather itself moved to the
// daemon. Drawn via the library's own gfx->drawBitmap(mask,w,h,color) --
// the PROGMEM-safe 1-bit path (pgm_read_byte), same mechanism this codebase
// already uses for font glyphs. An earlier RGB565-array version crashed
// on-device (LoadStoreError reading a uint16_t PROGMEM array via
// pgm_read_word) -- byte-wide reads don't hit that fault.

// True if this ISO8601 date's Y/M/D matches `now` (device local time).
static bool isSameLocalDay(const char* iso, const struct tm& now) {
  if (strlen(iso) < 10) return false;
  int y  = (iso[0]-'0')*1000 + (iso[1]-'0')*100 + (iso[2]-'0')*10 + (iso[3]-'0');
  int mo = (iso[5]-'0')*10 + (iso[6]-'0');
  int d  = (iso[8]-'0')*10 + (iso[9]-'0');
  return y == now.tm_year + 1900 && mo == now.tm_mon + 1 && d == now.tm_mday;
}

static void drawWeatherIcon(Arduino_GFX* gfx, int cx, int cy, WxCat cat) {
  const uint8_t* mask;
  uint16_t color;
  switch (cat) {
    case WX_CLEAR: mask = kWxIconClear; color = C_ACCENT; break;
    case WX_CLOUD: mask = kWxIconCloud; color = C_SKY;    break;
    case WX_FOG:   mask = kWxIconFog;   color = C_DIM;    break;
    case WX_RAIN:  mask = kWxIconRain;  color = C_SKY;    break;
    case WX_SNOW:  mask = kWxIconSnow;  color = C_WHITE;  break;
    case WX_STORM: mask = kWxIconStorm; color = C_STORM;  break;
    case WX_UNKNOWN:
    default:
      gfx->drawCircle(cx, cy, WX_ICON_SIZE * 3 / 10, C_DIM);
      gfx->setTextSize(3);
      gfx->setTextColor(C_DIM);
      gfx->setCursor(cx - 6, cy - 12);
      gfx->print("?");
      return;
  }
  gfx->drawBitmap(cx - WX_ICON_SIZE / 2, cy - WX_ICON_SIZE / 2, mask, WX_ICON_SIZE, WX_ICON_SIZE, color);
}

// ---- render (3 independent modes, each its own carousel entry) -------------
// Agenda reuses the gray card panel look from UsageMode.cpp's drawMeter()
// (fillRoundRect at x=8, w=224, radius 8, C_PANEL) per explicit request --
// "steal that gray frame" -- one card per upcoming event, up to
// EVENTS_PER_PAGE stacked on one screen exactly like the 5h/7d meter cards.
// With CAL_MAX_EVENTS raised to 6, events 4-6 get their own carousel entry
// (CalendarAgendaMode2, "page" 1 here) rather than an internal auto-flip
// timer within this mode -- see CalendarMode.h's header comment for why
// that first attempt (a PAGE_DWELL_MS timer) didn't reliably show page 2 at
// all (needed PAGE_DWELL_MS < the carousel's own carouselSec, not
// guaranteed) and was replaced with a real second mode instead.
#define EVENTS_PER_PAGE 3

static void drawAgendaPage(Arduino_GFX* gfx, const CalendarEvent& c, uint8_t page) {
  gfx->fillScreen(C_BLACK);
  const int x = 8, w = 224;
  bool connected = c.valid;
  uint8_t total = connected ? c.count : 0;

  if (total == 0) {
    const int top = 20, h = 200;
    gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);
    gfx->setTextSize(2);
    gfx->setTextColor(C_DIM, C_PANEL);
    gfx->setCursor(x + 14, top + 12);
    gfx->print(connected ? "NEXT EVENT" : "CALENDAR");
    gfx->setTextSize(3);
    gfx->setTextColor(C_DIM, C_PANEL);
    gfx->setCursor(x + 14, top + 90);
    gfx->print(connected ? "No events" : "No data");
    return;
  }

  uint8_t startIdx = page * EVENTS_PER_PAGE;
  if (startIdx >= total) startIdx = 0;   // stale page index after a shrinking push
  uint8_t n = total - startIdx;
  if (n > EVENTS_PER_PAGE) n = EVENTS_PER_PAGE;

  struct tm now;
  bool haveNow = clockNow(now);

  const int top0 = 6, gap = 6, bottom = 234;
  const int h = (bottom - top0 - gap * (n - 1)) / n;
  for (uint8_t i = 0; i < n; i++) {
    int top = top0 + i * (h + gap);
    gfx->fillRoundRect(x, top, w, h, 8, C_PANEL);

    const CalendarEventItem& ev = c.items[startIdx + i];
    // Date/time text tints to the source Google Calendar's own color when
    // the daemon sent one (multiple calendars merged into one agenda --
    // this is how you tell which calendar an event came from at a
    // glance), falling back to the existing fixed accent otherwise.
    uint16_t eventColor = ev.hasColor ? ev.color : C_ACCENT;

    // Date, left-aligned: "Today" when it matches the device's local date,
    // else "Mon DD" -- explicit request, was previously just the bare time.
    char dateBuf[12];
    if (haveNow && isSameLocalDay(ev.start, now)) strlcpy(dateBuf, "Today", sizeof(dateBuf));
    else extractMonthDay(ev.start, dateBuf, sizeof(dateBuf));
    gfx->setTextSize(2);
    gfx->setTextColor(eventColor, C_PANEL);
    gfx->setCursor(x + 12, top + 8);
    gfx->print(dateBuf);

    // Time, right-aligned on the same row. Size-2 glyphs are 12px wide.
    char timeBuf[8];
    if (ev.allDay) strlcpy(timeBuf, "All day", sizeof(timeBuf));
    else extractTimeHHMM(ev.start, timeBuf, sizeof(timeBuf));
    int timeX = x + w - 12 - (int)strlen(timeBuf) * 12;
    gfx->setTextColor(eventColor, C_PANEL);
    gfx->setCursor(timeX, top + 8);
    gfx->print(timeBuf);

    // Card usable width is w-24 (12px margin each side) = 200px; size-2 glyphs
    // are 12px wide, so 16 chars is the hard ceiling before writing past the
    // card's right edge -- was 20, overflowed the frame border.
    char titleBuf[24];
    truncateDots(ev.summary, titleBuf, sizeof(titleBuf), 16);
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE, C_PANEL);
    gfx->setCursor(x + 12, top + h - 32);
    gfx->print(titleBuf);
  }
}

// Combined weather + air quality on one screen (was 2 separate sub-pages,
// lumped together per explicit request). Compact layout to fit both in the
// 240px screen height: label, smaller icon, temp+rain on one line, then
// AQI + PM2.5.
static void drawWeatherPage(Arduino_GFX* gfx, const WeatherData& w) {
  gfx->fillScreen(C_BLACK);
  // City name doubles as the page header -- the weather/AQI content below
  // already makes clear what kind of page this is, so a separate "WEATHER"
  // label is redundant once a city is known. Falls back to "WEATHER" only
  // when no city has arrived yet (e.g. daemon not yet updated/restarted).
  // City is reverse-geocoded by the daemon (same free lookup the web UI's
  // "Use my location" button already does) -- truncated to fit, not sent
  // through any AI/LLM abbreviation (would add cost/latency for a string
  // that barely ever changes).
  if (w.hasCity) {
    char cityBuf[20];
    truncateDots(w.city, cityBuf, sizeof(cityBuf), 18);
    drawRow(gfx, 10, 2, C_DIM, cityBuf);
  } else {
    drawRow(gfx, 10, 2, C_DIM, "WEATHER");
  }

  bool wxOk = w.hasTemp || w.hasPrecip;
  WxCat cat = wxCategory(w.weatherCode, w.hasWeatherCode);
  drawWeatherIcon(gfx, 120, 54, wxOk ? cat : WX_UNKNOWN);

  // Current-condition word + raw WMO code, centered directly under the icon
  // (icon bottom is cy=54 + WX_ICON_SIZE/2=20 = 74). Independent of rain
  // probability below -- e.g. "Clear" above "Rain 60%" is a real, expected
  // combination from Open-Meteo's separate weather_code/
  // precipitation_probability fields, not a bug.
  char cond[20] = "--";
  if (wxOk && w.hasWeatherCode) snprintf(cond, sizeof(cond), "%s (%d)", wxLabel(cat), w.weatherCode);
  else if (wxOk) strlcpy(cond, wxLabel(cat), sizeof(cond));
  drawRowCentered(gfx, 120, 80, 2, C_DIM, cond);

  char t[10] = "--";
  // Degree sign is 0xF8 in this font's glyph table (CP437 layout, not
  // Latin-1/0xB0 -- verified by rendering the actual glyph bitmap before
  // trusting the byte value).
  if (w.hasTemp) snprintf(t, sizeof(t), "%d\xF8" "C", (int)lroundf(w.tempC));
  drawRow(gfx, 116, 3, wxOk ? C_WHITE : C_DIM, t);

  char p[16] = "Rain --";
  if (w.hasPrecip) snprintf(p, sizeof(p), "Rain %d%%", w.precipPct);
  drawRow(gfx, 148, 3, w.hasPrecip ? C_SKY : C_DIM, p);

  // Full 6-band US AQI color scale, muted to match this palette's dark theme
  // (not AirNow's harsh saturated colors) -- see C_AQI_* above.
  char aqiBuf[16];
  uint16_t aqiColor = C_DIM;
  if (w.hasAqi) {
    snprintf(aqiBuf, sizeof(aqiBuf), "AQI %d", w.aqi);
    aqiColor = (w.aqi <= 50)  ? C_AQI_GOOD :
               (w.aqi <= 100) ? C_AQI_MODERATE :
               (w.aqi <= 150) ? C_AQI_SENSITIVE :
               (w.aqi <= 200) ? C_AQI_UNHEALTHY :
               (w.aqi <= 300) ? C_AQI_VERY_UNHEALTHY : C_AQI_HAZARDOUS;
  } else {
    strlcpy(aqiBuf, "AQI --", sizeof(aqiBuf));
  }
  drawRow(gfx, 180, 3, aqiColor, aqiBuf);

  char pm[16] = "PM 2.5 --";
  if (w.hasPm25) {
    // Drop the ".0" when the source value is a whole number (Open-Meteo
    // often reports e.g. 12.0) -- keep one decimal otherwise.
    if (fabsf(w.pm25 - roundf(w.pm25)) < 0.05f)
      snprintf(pm, sizeof(pm), "PM 2.5 %d", (int)lroundf(w.pm25));
    else
      snprintf(pm, sizeof(pm), "PM 2.5 %.1f", w.pm25);
  }
  drawRow(gfx, 212, 2, C_DIM, pm);
}

// ---- DisplayMode: Agenda -----------------------------------------------
void CalendarAgendaMode::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
}

void CalendarAgendaMode::invalidate(const Settings& s) {
  (void)s;
  // Just force a redraw -- don't clear cal/weather data here. Both arrive
  // purely via daemon push (no device-side re-fetch to trigger), so wiping
  // them on every settings save would blank the screen until the next push
  // lands, for no benefit.
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
}

void CalendarAgendaMode::service(const Settings& s) {
  (void)s;
  const CalendarEvent& c = calendarGet();
  if (c.lastOkMs != calRenderedOk_) { calRenderedOk_ = c.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawAgendaPage(gfx, c, 0);
    needRender_ = false;
  }
}

// ---- DisplayMode: Agenda page 2 (events 4-6) ----------------------------
void CalendarAgendaMode2::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
}

void CalendarAgendaMode2::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  calRenderedOk_ = 0xFFFFFFFF;
}

void CalendarAgendaMode2::service(const Settings& s) {
  (void)s;
  const CalendarEvent& c = calendarGet();
  if (c.lastOkMs != calRenderedOk_) { calRenderedOk_ = c.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawAgendaPage(gfx, c, 1);
    needRender_ = false;
  }
}

// ---- DisplayMode: Weather -----------------------------------------------
void CalendarWeatherMode::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  wxRenderedOk_ = 0xFFFFFFFF;
}

void CalendarWeatherMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  wxRenderedOk_ = 0xFFFFFFFF;
}

void CalendarWeatherMode::service(const Settings& s) {
  (void)s;
  const WeatherData& w = weatherGet();
  if (w.lastOkMs != wxRenderedOk_) { wxRenderedOk_ = w.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawWeatherPage(gfx, w);
    needRender_ = false;
  }
}
