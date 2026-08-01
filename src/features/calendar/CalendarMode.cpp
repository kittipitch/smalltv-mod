#include "CalendarMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"
#include "Clock.h"
#include "WeatherIcons.h"

CalendarAgendaMode   g_calendarAgendaMode;
CalendarAgendaMode2  g_calendarAgendaMode2;
CalendarWeatherMode  g_calendarWeatherMode;
CalendarForecastMode g_calendarForecastMode;

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

// EPA/WHO UV Index reference chart, exact hex-to-RGB565 conversion (user
// supplied the real 10-band chart -- not muted like C_AQI_*, since this is
// meant to read as the actual standard scale, not a reinterpretation).
// Ranges are half-open [lo, hi), matched to the same-rounded integer uv
// value the display already computes.
#define C_UV_0_1    0x4DA0   // #4eb400 -- Low        0 to 2
#define C_UV_2      0xA660   // #a0ce00 -- Low         2 to 3
#define C_UV_3      0xF720   // #f7e400 -- Moderate    3 to 4
#define C_UV_4      0xFDA0   // #f8b600 -- Moderate    4 to 5
#define C_UV_5      0xFC20   // #f88700 -- Moderate    5 to 6
#define C_UV_6      0xFAC0   // #f85900 -- High        6 to 7
#define C_UV_7      0xE961   // #e82c0e -- High        7 to 8
#define C_UV_8      0xD803   // #d8001d -- Very High   8 to 9
#define C_UV_9      0xF813   // #ff0099 -- Very High   9 to 10
#define C_UV_10     0xB27F   // #b54cff -- Extreme    10 to 11
#define C_UV_11PLUS 0x9C7F   // #998cff -- Extreme    11 or more

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

  // UVI shares the condition row (same size 2, centered) rather than the
  // temp row or a row of its own -- the page is already floor-to-ceiling
  // packed (PM 2.5 row ends at y=228 on a 240px panel). Drawn as a second
  // segment, not appended into cond's own string, so it keeps its own
  // EPA/WHO-chart severity color (C_UV_*, the exact reference chart's
  // saturated values, not a reuse of C_AQI_*'s muted palette) independent
  // of the condition text's C_DIM. Centered as one unit: total width
  // computed first, then both segments drawn from that shared start x --
  // drawRowCentered() only supports a single color, so this is done by
  // hand instead. Worst case: cond max "Cloudy (99)" (11 chars, 132px at
  // size 2) + uvBuf max " UVI 20" (7 chars, 84px) = 216px, inside the
  // 226px budget (240px screen, drawRow's real x=14 origin, not assumed).
  char uvBuf[10] = "";
  uint16_t uvColor = C_DIM;
  if (w.hasUvIndex) {
    // Same UB guard as tempC/pm25: constrain() before lroundf(), since
    // uvIndex is equally an unvalidated daemon push.
    int uv = (int)lroundf(constrain(w.uvIndex, 0.0f, 20.0f));
    snprintf(uvBuf, sizeof(uvBuf), " UVI %d", uv);
    uvColor = (uv <= 1)  ? C_UV_0_1 :
              (uv == 2) ? C_UV_2 :
              (uv == 3) ? C_UV_3 :
              (uv == 4) ? C_UV_4 :
              (uv == 5) ? C_UV_5 :
              (uv == 6) ? C_UV_6 :
              (uv == 7) ? C_UV_7 :
              (uv == 8) ? C_UV_8 :
              (uv == 9) ? C_UV_9 :
              (uv == 10) ? C_UV_10 : C_UV_11PLUS;
  }
  int condW = gfxTextW(cond, 2);
  int uvW = uvBuf[0] ? gfxTextW(uvBuf, 2) : 0;
  int condRowX = 120 - (condW + uvW) / 2;
  if (condRowX < 0) condRowX = 0;
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(condRowX, 80);
  gfx->print(cond);
  if (uvBuf[0]) {
    gfx->setTextColor(uvColor);
    gfx->setCursor(condRowX + condW, 80);
    gfx->print(uvBuf);
  }

  char tempPart[8] = "--";
  // Degree sign is 0xF8 in this font's glyph table (CP437 layout, not
  // Latin-1/0xB0 -- verified by rendering the actual glyph bitmap before
  // trusting the byte value).
  // constrain() before lroundf(): tempC comes straight from a daemon push
  // with no range validation upstream (weatherApply()/CalendarClient.cpp),
  // and (int)lroundf() on an extreme/non-finite float (e.g. a malformed or
  // malicious POST /api/weather {"tempC":1e10}, unauthenticated by default
  // with no secret key set) is undefined behavior on this platform, not
  // just a display glitch -- clamp first so the cast is always well-defined.
  if (w.hasTemp) snprintf(tempPart, sizeof(tempPart), "%d\xF8" "C", (int)lroundf(constrain(w.tempC, -99.0f, 199.0f)));
  drawRow(gfx, 116, 3, wxOk ? C_WHITE : C_DIM, tempPart);

  char p[16] = "Rain --";
  if (w.hasPrecip) snprintf(p, sizeof(p), "Rain %d%%", w.precipPct);
  drawRow(gfx, 148, 3, w.hasPrecip ? C_SKY : C_DIM, p);

  // Full 6-band US AQI color scale, muted to match this palette's dark theme
  // (not AirNow's harsh saturated colors) -- see C_AQI_* above.
  char aqiBuf[16];
  uint16_t aqiColor = C_DIM;
  if (w.hasAqi) {
    // Clamp: w.aqi comes straight off an unauthenticated-by-default POST
    // with no upstream range check -- an out-of-range value (e.g. a
    // negative or 5-digit aqi) would both feed a meaningless color-ladder
    // bucket and blow "AQI %d" past the width this row (and the
    // parenthetical appended after it, below) was budgeted for.
    int aqiV = w.aqi;
    if (aqiV < 0) aqiV = 0;
    if (aqiV > 500) aqiV = 500;
    snprintf(aqiBuf, sizeof(aqiBuf), "AQI %d", aqiV);
    aqiColor = (aqiV <= 50)  ? C_AQI_GOOD :
               (aqiV <= 100) ? C_AQI_MODERATE :
               (aqiV <= 150) ? C_AQI_SENSITIVE :
               (aqiV <= 200) ? C_AQI_UNHEALTHY :
               (aqiV <= 300) ? C_AQI_VERY_UNHEALTHY : C_AQI_HAZARDOUS;
  } else {
    strlcpy(aqiBuf, "AQI --", sizeof(aqiBuf));
  }
  drawRow(gfx, 180, 3, aqiColor, aqiBuf);

  // aqiNow: real-time AQI computed daemon-side from THIS hour's PM2.5 alone
  // (python-aqi, classic EPA breakpoints) -- the AQI row above is
  // Open-Meteo's own 24h-rolling-average us_aqi and can stay elevated for
  // hours after a real spike has already passed (confirmed live this
  // session). Shown as a smaller parenthetical right after "AQI N" on the
  // same line, own severity color, so a fresh spike is visible immediately
  // instead of waiting on the rolling average to catch up -- own line
  // avoided since the two numbers usually disagree by design and reading
  // as one contradictory "AQI" would be more confusing, not less. Gated on
  // hasAqi too, not just hasAqiNow: a live colored "(25)" stapled onto a
  // dim placeholder "AQI --" would be exactly that same contradiction, in
  // reverse -- reachable, since aqiNow's computation is independent of
  // whether Open-Meteo's own us_aqi came back in the same poll.
  if (w.hasAqi && w.hasAqiNow) {
    // Same UB guard as every other daemon-pushed field on this page:
    // aqiNow is already clamped to python-aqi's 0-500 range server-side,
    // but re-clamp here too since /api/weather can be hit directly.
    int aqiN = w.aqiNow;
    if (aqiN < 0) aqiN = 0;
    if (aqiN > 500) aqiN = 500;
    char nowBuf[10];
    snprintf(nowBuf, sizeof(nowBuf), " (%d)", aqiN);
    uint16_t nowColor = (aqiN <= 50)  ? C_AQI_GOOD :
                         (aqiN <= 100) ? C_AQI_MODERATE :
                         (aqiN <= 150) ? C_AQI_SENSITIVE :
                         (aqiN <= 200) ? C_AQI_UNHEALTHY :
                         (aqiN <= 300) ? C_AQI_VERY_UNHEALTHY : C_AQI_HAZARDOUS;
    // Worst case "AQI 100" (7 chars) at size 3 + " (500)" (6 chars) at
    // size 2 = 126px + 72px = 198px, inside the 226px row budget -- no
    // gfxFitSize needed, same as the plain AQI row this replaces.
    int aqiW = gfxTextW(aqiBuf, 3);
    // Size-2 text is 16px tall vs size-3's 24px; nudge down 4px so the
    // parenthetical sits vertically centered against "AQI N", not
    // top-aligned against it (this font draws from the glyph's top-left,
    // no baseline metric to align on instead).
    gfx->setTextSize(2);
    gfx->setTextColor(nowColor);
    gfx->setCursor(14 + aqiW, 180 + 4);
    gfx->print(nowBuf);
  }

  // Unit is "\xE6g/m3" -- 0xE6 is the micro sign (µ) in this font's CP437
  // layout, verified against the actual glyph bitmap before trusting the
  // byte value. No superscript-3 glyph exists in this font's 256-char
  // set, so "m3" not "m³" -- a real constraint, not an oversight. No
  // space before the unit, same reasoning as when this was first added:
  // worst case at the 999.9 clamp below, "PM 2.5 999.9\xE6g/m3" is 17
  // chars at size 2 = 204px, inside the 226px row budget -- gfxFitSize
  // (not a fixed size) is a defensive fallback, not load-bearing here.
  char pm[24] = "PM 2.5 --";
  if (w.hasPm25) {
    // constrain() before lroundf()/formatting: pm25 comes straight from a
    // daemon push with no range validation upstream, same UB risk as tempC
    // above -- clamp to what this line's width budget was actually sized
    // for (never assume a source float is sane).
    float pm25c = constrain(w.pm25, 0.0f, 999.9f);
    char pmVal[10];
    // Drop the ".0" when the source value is a whole number (Open-Meteo
    // often reports e.g. 12.0) -- keep one decimal otherwise.
    if (fabsf(pm25c - roundf(pm25c)) < 0.05f)
      snprintf(pmVal, sizeof(pmVal), "%d", (int)lroundf(pm25c));
    else
      snprintf(pmVal, sizeof(pmVal), "%.1f", pm25c);
    snprintf(pm, sizeof(pm), "PM 2.5 %s\xE6g/m3", pmVal);
  }
  drawRow(gfx, 212, gfxFitSize(pm, 226, 2), C_DIM, pm);
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

// 3-day forecast (tomorrow..+3) -- today's own conditions are already on
// the main weather page, so this list starts at tomorrow (daemon already
// skips today when building the "forecast" array). Borderless stacked
// rows, no card panel, matching drawWeatherPage's own visual style rather
// than drawAgendaPage's gray-card look -- this page and the weather page
// are the same "at a glance" family, agenda's cards are a different look.
// No page-level header (unlike drawWeatherPage's city-name row) -- the
// screen is floor-to-ceiling packed at 3 rows of the row height below,
// and a page showing three explicit dates doesn't need a label to say
// what kind of page it is the way a single current-conditions page does.
static void drawForecastEmpty(Arduino_GFX* gfx) {
  gfx->fillScreen(C_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(14, 110);
  gfx->print("No forecast yet");
}

static void drawForecastPageHorizontal(Arduino_GFX* gfx, const WeatherData& w) {
  gfx->fillScreen(C_BLACK);

  const int top0 = 6, rh = 76;
  for (uint8_t i = 0; i < w.fcCount; i++) {
    const ForecastDay& d = w.fc[i];
    int y0 = top0 + i * rh;

    // Day label, left-aligned. Daemon-formatted "Tmr"/"Mon"/etc, fixed
    // English 3-4 chars, worst case 4*12=48px at size 2 -- no dynamic
    // width/fit needed, unlike the daemon-pushed numeric fields below
    // whose digit counts vary.
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(14, y0 + 4);
    gfx->print(d.day);

    // Precip probability, right-aligned on the day label's row. -1 sentinel
    // (set in CalendarClient.cpp when the daemon didn't send "precip")
    // means skip rather than print a bogus "-1%". Clamped to 0-100 like
    // every other daemon-pushed field on this page -- unclamped, the
    // compiler's own -Wformat-truncation flagged that "%d%%" into a
    // 6-byte buffer isn't provably safe for an arbitrary int, and
    // /api/weather is unauthenticated by default like every push endpoint
    // here.
    if (d.precip >= 0) {
      int precipV = d.precip; if (precipV > 100) precipV = 100;
      char precipBuf[6];
      snprintf(precipBuf, sizeof(precipBuf), "%d%%", precipV);
      int pw = gfxTextW(precipBuf, 2);
      gfx->setTextColor(C_SKY);
      gfx->setCursor(226 - pw, y0 + 4);
      gfx->print(precipBuf);
    }

    // Icon at native 40x40 (WX_ICON_SIZE) -- no scaling path exists in
    // this codebase's drawBitmap call, and at this row height (76px) the
    // icon fits with clearance on every side without needing one.
    WxCat cat = wxCategory(d.code, true);
    drawWeatherIcon(gfx, 40, y0 + 50, cat);

    // Lo/hi temp, own column starting past the icon's right edge (icon
    // spans x=20..60, text starts x=70 -- 10px clearance). constrain()
    // before formatting: hi/lo are unvalidated daemon-pushed ints like
    // every other field on this page, same UB risk as tempC/pm25
    // elsewhere. Worst case "-99--99\xF8C" is 9 chars = 108px at size 2,
    // well inside the row -- no gfxFitSize needed. Low first, dash
    // separator -- matches the reference layout's "16-26C" convention.
    int hiC = d.hi; if (hiC < -99) hiC = -99; if (hiC > 199) hiC = 199;
    int loC = d.lo; if (loC < -99) loC = -99; if (loC > 199) loC = 199;
    char tempBuf[12];
    snprintf(tempBuf, sizeof(tempBuf), "%d-%d\xF8" "C", loC, hiC);
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(70, y0 + 30);
    gfx->print(tempBuf);

    // Daily AQI, same column, same severity colors as the main weather
    // page's AQI row -- python-aqi/EPA on that day's max hourly PM2.5
    // (see clawdmeter_daemon.py's poll_weather()), independently optional
    // like every daemon field: "AQI --" in C_DIM when the daemon's own
    // aggregation failed for that day but temp/code still came through.
    char aqiBuf[10];
    uint16_t aqiColor;
    if (d.hasAqi) {
      int aqiV = d.aqi; if (aqiV < 0) aqiV = 0; if (aqiV > 500) aqiV = 500;
      snprintf(aqiBuf, sizeof(aqiBuf), "AQI %d", aqiV);
      aqiColor = (aqiV <= 50)  ? C_AQI_GOOD :
                 (aqiV <= 100) ? C_AQI_MODERATE :
                 (aqiV <= 150) ? C_AQI_SENSITIVE :
                 (aqiV <= 200) ? C_AQI_UNHEALTHY :
                 (aqiV <= 300) ? C_AQI_VERY_UNHEALTHY : C_AQI_HAZARDOUS;
    } else {
      strlcpy(aqiBuf, "AQI --", sizeof(aqiBuf));
      aqiColor = C_DIM;
    }
    gfx->setTextSize(2);
    gfx->setTextColor(aqiColor);
    gfx->setCursor(70, y0 + 50);
    gfx->print(aqiBuf);

    // Row separator, skipped after the last row.
    if (i + 1 < w.fcCount) gfx->drawFastHLine(14, y0 + rh - 2, 212, C_PANEL);
  }
}

// 3 columns (80px each), one per day, stacked top-to-bottom within each
// column -- alternate to drawForecastPageHorizontal(), toggled via
// Settings.forecastVertical, added on explicit request to compare bigger
// text against the horizontal-rows baseline. Iterated live against the
// real screen (not just the design pass) through several rounds: day
// label went size2->3->back to 2 (3 read as too large), precip moved
// under the icon then relocated again to sit after hi/lo, icon stays
// at the top (an "icon after hi/lo" reorder was tried in an intermediate
// round and explicitly declined -- "no icon at top is ok"). Final order
// top to bottom: day, icon, hi, lo, precip, AQI. hi/lo share one size
// (size 3, was hi=4/lo=3 in round 1 -- asymmetric for no real reason).
// No "AQI "/degree-symbol prefixes -- both overflow 80px at size >=2
// (see width table below), so severity color alone carries the AQI
// meaning and the white-over-dim vertical pair reads as the hi/lo
// pairing instead of a label.
//
// Worst-case width per element (fixed 6*size px/char font), all must
// clear the 80px column:
//   day    "Tmr"/3-letter abbr, 3 chars * 6*2=12     = 36px
//   hi/lo  "-99"/"199", 3 chars * 6*3=18             = 54px
//   precip "100%" (clamped 0-100), 4 chars * 6*2=12  = 48px
//   aqi    "500" (clamped 0-500) or "--", 3 chars * 6*3=18 = 54px --
//          explicitly re-verified for the 3-digit case per live feedback,
//          not just carried over from an earlier round's math.
// Vertical stack: 27 top + 16 day + 6 gap + 40 icon + 8 gap + 24 hi +
// 6 gap + 24 lo + 10 gap + 16 precip + 12 gap + 24 aqi = 213, 27px
// bottom clearance -- centered (advisor-audit finding: an earlier 8/46
// top/bottom split read as top-heavy on the real panel; the content
// itself, 186px tall, is unchanged, only shifted +19 to center it).
// Icon is a fixed 40x40 1-bit PROGMEM bitmap (see WeatherIcons.h) with
// no scaling path in this codebase's drawBitmap call -- a real ask to
// make it visually bigger needs a new baked icon asset at a larger
// resolution, not a coordinate change, and is deliberately not done
// here.
static void drawForecastPageVertical(Arduino_GFX* gfx, const WeatherData& w) {
  gfx->fillScreen(C_BLACK);
  // Only draw the divider between two populated columns -- fcCount < 3
  // is rare (daemon sends all 3 or none in practice) but reachable via a
  // direct /api/weather push, and an unconditional divider there drew a
  // line in front of two empty columns.
  // Content band is y=27..213 (see the +19 shift below) -- dividers match
  // that span instead of the full 8..232, so they don't run past the
  // bottom of the last element into empty space (advisor-audit finding).
  if (w.fcCount > 1) gfx->drawFastVLine(80, 27, 186, C_PANEL);
  if (w.fcCount > 2) gfx->drawFastVLine(160, 27, 186, C_PANEL);

  for (uint8_t i = 0; i < w.fcCount; i++) {
    const ForecastDay& d = w.fc[i];
    int colX = i * 80;
    int cx = colX + 40;

    // Every y below is the un-centered value +19 -- content spanned
    // y=8..194 on the 240px column (8 top margin, 46 bottom), which read
    // top-heavy on the real panel (advisor-audit finding); +19 centers
    // the same content vertically (27 top margin, 27 bottom).
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(colX + (80 - gfxTextW(d.day, 2)) / 2, 27);
    gfx->print(d.day);

    WxCat cat = wxCategory(d.code, true);
    drawWeatherIcon(gfx, cx, 69, cat);

    int hiC = d.hi; if (hiC < -99) hiC = -99; if (hiC > 199) hiC = 199;
    int loC = d.lo; if (loC < -99) loC = -99; if (loC > 199) loC = 199;
    char hiBuf[6]; snprintf(hiBuf, sizeof(hiBuf), "%d", hiC);
    char loBuf[6]; snprintf(loBuf, sizeof(loBuf), "%d", loC);
    gfx->setTextSize(3);
    gfx->setTextColor(C_WHITE);
    gfx->setCursor(colX + (80 - gfxTextW(hiBuf, 3)) / 2, 97);
    gfx->print(hiBuf);
    gfx->setTextColor(C_DIM);
    gfx->setCursor(colX + (80 - gfxTextW(loBuf, 3)) / 2, 127);
    gfx->print(loBuf);

    if (d.precip >= 0) {
      int precipV = d.precip; if (precipV > 100) precipV = 100;
      char precipBuf[6];
      snprintf(precipBuf, sizeof(precipBuf), "%d%%", precipV);
      gfx->setTextSize(2);
      gfx->setTextColor(C_SKY);
      gfx->setCursor(colX + (80 - gfxTextW(precipBuf, 2)) / 2, 161);
      gfx->print(precipBuf);
    }

    char aqiBuf[6];
    uint16_t aqiColor;
    if (d.hasAqi) {
      int aqiV = d.aqi; if (aqiV < 0) aqiV = 0; if (aqiV > 500) aqiV = 500;
      snprintf(aqiBuf, sizeof(aqiBuf), "%d", aqiV);
      aqiColor = (aqiV <= 50)  ? C_AQI_GOOD :
                 (aqiV <= 100) ? C_AQI_MODERATE :
                 (aqiV <= 150) ? C_AQI_SENSITIVE :
                 (aqiV <= 200) ? C_AQI_UNHEALTHY :
                 (aqiV <= 300) ? C_AQI_VERY_UNHEALTHY : C_AQI_HAZARDOUS;
    } else {
      strlcpy(aqiBuf, "--", sizeof(aqiBuf));
      aqiColor = C_DIM;
    }
    gfx->setTextSize(3);
    gfx->setTextColor(aqiColor);
    gfx->setCursor(colX + (80 - gfxTextW(aqiBuf, 3)) / 2, 189);
    gfx->print(aqiBuf);
  }
}

static void drawForecastPage(Arduino_GFX* gfx, const WeatherData& w, bool vertical) {
  if (w.fcCount == 0) { drawForecastEmpty(gfx); return; }
  if (vertical) drawForecastPageVertical(gfx, w);
  else          drawForecastPageHorizontal(gfx, w);
}

// ---- DisplayMode: Forecast -----------------------------------------------
void CalendarForecastMode::begin(const Settings& s) {
  calendarInit(s);
  needRender_ = true;
  wxRenderedOk_ = 0xFFFFFFFF;
}

void CalendarForecastMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
  wxRenderedOk_ = 0xFFFFFFFF;
}

void CalendarForecastMode::service(const Settings& s) {
  const WeatherData& w = weatherGet();
  if (w.lastOkMs != wxRenderedOk_) { wxRenderedOk_ = w.lastOkMs; needRender_ = true; }
  if (needRender_) {
    Arduino_GFX* gfx = gfxDev();
    if (gfx) drawForecastPage(gfx, w, s.forecastVertical);
    needRender_ = false;
  }
}
