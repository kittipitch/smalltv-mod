#include "StatusMode.h"
#include <Arduino_GFX_Library.h>
#include "Gfx.h"
#include "CalendarClient.h"

StatusMode g_statusMode;

#define C_BLACK    0x0000
#define C_WHITE    0xFFFF
#define C_PANEL    0x18E3
#define C_DIM      0xB574
#define C_UGREEN   0x7C6B
#define C_GOLD     0xBCC9
#define C_ORANGE   0xC3C8
#undef C_RED
#define C_RED      0xB289
#undef C_GREY
#define C_GREY     0x8410
#define C_BARBG    0x2945
#define C_ACCENT   0xDBAA
#define C_SKY      0x5D9C

#define STATUS_TILE      108
#define STATUS_STALE_S   180
#define STATUS_HDR_X     8
#define STATUS_HDR_Y     7
#define STATUS_DIV_Y     20
#define STATUS_NAME_X    8
#define STATUS_ROW_A     27
#define STATUS_ROW_B     48
#define STATUS_ROW_C     70
#define STATUS_ROW_D     91

static const char CH_MID = (char)0xB7;
static const char CH_ELL = (char)0x85;
static const char CH_NSP = (char)0x7F;

static int statusAdv(char ch, uint8_t size) {
  if (ch == CH_MID) return 4 * size;
  if (ch == CH_NSP) return 3 * size;
  return 6 * size;
}

static int statusRunW(const char* text, uint8_t size) {
  if (!text || !*text) return 0;
  int w = 0;
  for (const char* p = text; *p; p++) {
    w += statusAdv(*p, size);
  }
  return w - size;
}

static void statusDrawRunChar(Arduino_GFX* gfx, int& cx, int y, uint8_t size, char ch, uint16_t col) {
  if (ch == CH_MID) {
    gfx->fillRect(cx + size, y + (7 * size) / 2, size, size, col);
    cx += 4 * size;
  } else if (ch == CH_ELL) {
    for (int k = 0; k <= 4; k += 2) {
      gfx->fillRect(cx + k * size, y + 6 * size, size, size, col);
    }
    cx += 6 * size;
  } else if (ch == CH_NSP) {
    cx += 3 * size;
  } else {
    gfx->setTextSize(size);
    gfx->setTextColor(col);
    gfx->setCursor(cx, y);
    gfx->print(ch);
    cx += 6 * size;
  }
}

static void statusDrawRun(Arduino_GFX* gfx, int& cx, int y, uint8_t size, const char* text, uint16_t col) {
  if (!text) return;
  while (*text) {
    statusDrawRunChar(gfx, cx, y, size, *text++, col);
  }
}

static void statusSanitise(const char* in, char* out, size_t outCap) {
  if (!out || outCap == 0) return;
  out[0] = 0;
  if (!in) return;
  size_t idx = 0;
  for (const char* p = in; *p && idx + 1 < outCap; p++) {
    unsigned char uc = (unsigned char)*p;
    if (uc >= 0x20 && uc <= 0x7E) out[idx++] = (char)uc;
    else out[idx++] = '?';
  }
  out[idx] = 0;
  size_t start = 0;
  while (out[start] && (unsigned char)out[start] <= ' ') start++;
  if (start > 0) {
    memmove(out, out + start, strlen(out + start) + 1);
  }
  size_t len = strlen(out);
  while (len > 0 && (unsigned char)out[len - 1] <= ' ') {
    out[--len] = 0;
  }
}

static void statusLabelFor(const char* name, char* out, size_t outCap, uint8_t& outSize) {
  char clean[64];
  statusSanitise(name, clean, sizeof(clean));
  size_t clen = strlen(clean);
  while (clen > 0 && clean[clen - 1] == '/') clean[--clen] = 0;
  while (clen > 0 && (unsigned char)clean[clen - 1] <= ' ') clean[--clen] = 0;
  if (clean[0] == 0) {
    strlcpy(clean, "NONAME", sizeof(clean));
    clen = 6;
  }
  if (clen <= 7) {
    outSize = 2;
    strlcpy(out, clean, outCap);
  } else if (clen <= 15) {
    outSize = 1;
    strlcpy(out, clean, outCap);
  } else {
    outSize = 1;
    const char* tail = clean + (clen - 13);
    while (*tail == '.') tail++;
    snprintf(out, outCap, "..%s", tail);
  }
}

static void statusMiniName(const char* name, char* out, size_t outCap) {
  char clean[64];
  statusSanitise(name, clean, sizeof(clean));
  size_t clen = strlen(clean);
  while (clen > 0 && clean[clen - 1] == '/') clean[--clen] = 0;
  while (clen > 0 && (unsigned char)clean[clen - 1] <= ' ') clean[--clen] = 0;
  const char* slash = strrchr(clean, '/');
  const char* seg = slash ? (slash + 1) : clean;
  while (*seg && (unsigned char)*seg <= ' ') seg++;
  char finalSeg[64];
  strlcpy(finalSeg, seg, sizeof(finalSeg));
  size_t slen = strlen(finalSeg);
  while (slen > 0 && (unsigned char)finalSeg[slen - 1] <= ' ') finalSeg[--slen] = 0;
  if (finalSeg[0] == 0) {
    strlcpy(finalSeg, "NONAME", sizeof(finalSeg));
    slen = 6;
  }
  if (slen <= 13) {
    strlcpy(out, finalSeg, outCap);
  } else {
    snprintf(out, outCap, "%.11s..", finalSeg);
  }
}

static void statusDurLong(uint32_t s, char* out, size_t outCap) {
  if (s < 60) {
    snprintf(out, outCap, "%us", (unsigned)s);
  } else if (s < 3600) {
    snprintf(out, outCap, "%um", (unsigned)(s / 60));
  } else if (s < 36000) {
    uint32_t h = s / 3600;
    uint32_t m = (s / 60) % 60;
    if (m) snprintf(out, outCap, "%uh%um", (unsigned)h, (unsigned)m);
    else snprintf(out, outCap, "%uh", (unsigned)h);
  } else if (s < 86400) {
    snprintf(out, outCap, "%uh", (unsigned)(s / 3600));
  } else {
    uint32_t d = s / 86400;
    if (d > 99) d = 99;
    snprintf(out, outCap, "%ud", (unsigned)d);
  }
}

static void statusDurShort(uint32_t s, char* out, size_t outCap) {
  if (s < 60) {
    snprintf(out, outCap, "%us", (unsigned)s);
  } else if (s < 3600) {
    snprintf(out, outCap, "%um", (unsigned)(s / 60));
  } else if (s < 86400) {
    snprintf(out, outCap, "%uh", (unsigned)(s / 3600));
  } else {
    uint32_t d = s / 86400;
    if (d > 99) d = 99;
    snprintf(out, outCap, "%ud", (unsigned)d);
  }
}

static bool isWordBound(const char* s, size_t pos, size_t len, size_t totalLen) {
  bool leftAlnum = (pos > 0) && isalnum((unsigned char)s[pos - 1]);
  bool rightAlnum = (pos + len < totalLen) && isalnum((unsigned char)s[pos + len]);
  return !leftAlnum && !rightAlnum;
}

static bool statusCauseCode(const char* cause, char* out, size_t outCap) {
  if (!out || outCap == 0) return false;
  out[0] = 0;
  if (!cause) return false;

  char clean[64];
  statusSanitise(cause, clean, sizeof(clean));
  if (clean[0] == 0) return false;

  char lc[64];
  size_t len = 0;
  for (; clean[len] && len + 1 < sizeof(lc); len++) {
    lc[len] = (char)tolower((unsigned char)clean[len]);
  }
  lc[len] = 0;

  // Rule 1: status / http 3 digits
  const char* prefixes[] = {"status", "http"};
  for (size_t k = 0; k < 2; k++) {
    const char* pfx = prefixes[k];
    size_t plen = strlen(pfx);
    const char* found = strstr(lc, pfx);
    while (found) {
      size_t pos = found - lc;
      if ((pos == 0 || !isalnum((unsigned char)lc[pos - 1])) &&
          (pos + plen == len || !isalnum((unsigned char)lc[pos + plen]))) {
        size_t d = pos + plen;
        while (d < len && isspace((unsigned char)lc[d])) d++;
        if (d + 3 <= len && isdigit((unsigned char)lc[d]) &&
            isdigit((unsigned char)lc[d + 1]) && isdigit((unsigned char)lc[d + 2]) &&
            (d + 3 == len || !isalnum((unsigned char)lc[d + 3]))) {
          snprintf(out, outCap, "HTTP%c%c%c%c", CH_NSP, lc[d], lc[d + 1], lc[d + 2]);
          return true;
        }
      }
      found = strstr(found + plen, pfx);
    }
  }

  // Rule 2: heartbeat
  if (strstr(lc, "heartbeat") != nullptr) {
    snprintf(out, outCap, "NO%cBEAT", CH_NSP);
    return true;
  }

  // Rule 3: timeout / timed out / time out
  if (strstr(lc, "timeout") != nullptr ||
      strstr(lc, "timed out") != nullptr ||
      strstr(lc, "time out") != nullptr) {
    strlcpy(out, "TIMEOUT", outCap);
    return true;
  }

  // Rule 4: refused
  if (strstr(lc, "refused") != nullptr) {
    strlcpy(out, "REFUSED", outCap);
    return true;
  }

  // Rule 5: ssl / tls / certificate
  if (strstr(lc, "certificate") != nullptr) {
    snprintf(out, outCap, "SSL%cERR", CH_NSP);
    return true;
  }
  const char* secTokens[] = {"ssl", "tls"};
  for (size_t k = 0; k < 2; k++) {
    const char* tok = secTokens[k];
    const char* found = strstr(lc, tok);
    while (found) {
      size_t pos = found - lc;
      if (isWordBound(lc, pos, 3, len)) {
        snprintf(out, outCap, "SSL%cERR", CH_NSP);
        return true;
      }
      found = strstr(found + 3, tok);
    }
  }

  // Rule 6: dns
  const char* foundDns = strstr(lc, "dns");
  while (foundDns) {
    size_t pos = foundDns - lc;
    if (isWordBound(lc, pos, 3, len)) {
      snprintf(out, outCap, "DNS%cERR", CH_NSP);
      return true;
    }
    foundDns = strstr(foundDns + 3, "dns");
  }

  // Rule 7: keyword
  if (strstr(lc, "keyword") != nullptr) {
    strlcpy(out, "KEYWORD", outCap);
    return true;
  }

  // Fallback: uppercase alphanumeric words joined with CH_NSP
  char full[64];
  size_t fpos = 0;
  bool inWord = false;
  for (size_t i = 0; clean[i]; i++) {
    char ch = (char)toupper((unsigned char)clean[i]);
    if (isalnum((unsigned char)ch)) {
      if (!inWord && fpos > 0 && fpos + 1 < sizeof(full)) {
        full[fpos++] = CH_NSP;
      }
      if (fpos + 1 < sizeof(full)) {
        full[fpos++] = ch;
      }
      inWord = true;
    } else {
      inWord = false;
    }
  }
  full[fpos] = 0;
  if (full[0] == 0) {
    strlcpy(full, "DOWN", sizeof(full));
  }

  if (statusRunW(full, 2) <= 92) {
    strlcpy(out, full, outCap);
    return true;
  }

  char accum[64];
  size_t apos = 0;
  accum[0] = 0;
  for (size_t i = 0; full[i]; i++) {
    char cand[64];
    size_t cpos = apos;
    memcpy(cand, accum, cpos);
    cand[cpos++] = full[i];
    cand[cpos] = 0;
    while (cpos > 0 && cand[cpos - 1] == CH_NSP) {
      cand[--cpos] = 0;
    }
    cand[cpos++] = CH_ELL;
    cand[cpos] = 0;
    if (statusRunW(cand, 2) > 92) {
      break;
    }
    if (apos + 1 < sizeof(accum)) {
      accum[apos++] = full[i];
      accum[apos] = 0;
    }
  }
  while (apos > 0 && accum[apos - 1] == CH_NSP) {
    accum[--apos] = 0;
  }
  accum[apos++] = CH_ELL;
  accum[apos] = 0;
  strlcpy(out, accum, outCap);
  return true;
}

static void statusDrawMini(Arduino_GFX* gfx, int x, int y, uint8_t kind) {
  if (kind == UPTIME_STATUS_DOWN) {
    gfx->fillRoundRect(x, y, 7, 7, 3, C_RED);
  } else if (kind == UPTIME_STATUS_UP) {
    gfx->fillRoundRect(x, y, 7, 7, 3, C_UGREEN);
    gfx->fillRect(x + 2, y + 2, 3, 3, C_BLACK);
  } else if (kind == UPTIME_STATUS_PENDING) {
    gfx->drawRoundRect(x, y, 7, 7, 3, C_GOLD);
  } else if (kind == UPTIME_STATUS_PAUSED) {
    gfx->fillRect(x + 1, y, 2, 7, C_GREY);
    gfx->fillRect(x + 4, y, 2, 7, C_GREY);
  } else {
    gfx->drawRoundRect(x, y, 7, 7, 3, C_DIM);
    gfx->setTextSize(1);
    gfx->setTextColor(C_DIM);
    gfx->setCursor(x + 1, y);
    gfx->print('?');
  }
}

static void statusDrawFrame(Arduino_GFX* gfx, int tx, int ty, bool alarm, bool lastDownStale, bool up) {
  gfx->fillRoundRect(tx, ty, STATUS_TILE, STATUS_TILE, 8, C_BLACK);
  int b;
  if (alarm || up) {
    uint16_t col = alarm ? C_RED : C_UGREEN;
    gfx->drawRoundRect(tx, ty, STATUS_TILE, STATUS_TILE, 8, col);
    gfx->drawRoundRect(tx + 1, ty + 1, STATUS_TILE - 2, STATUS_TILE - 2, 7, col);
    b = 2;
  } else {
    uint16_t col = lastDownStale ? C_RED : C_BARBG;
    gfx->drawRoundRect(tx, ty, STATUS_TILE, STATUS_TILE, 8, col);
    b = 1;
  }
  gfx->drawFastHLine(tx + b, ty + STATUS_DIV_Y, STATUS_TILE - 2 * b, C_BARBG);
}

static void statusDrawEmpty(Arduino_GFX* gfx, int tx, int ty) {
  gfx->drawRoundRect(tx, ty, STATUS_TILE, STATUS_TILE, 8, C_BARBG);
}

static void statusDrawTile(Arduino_GFX* gfx, int tx, int ty, const UptimeTarget& t, uint32_t age) {
  bool stale = (age > STATUS_STALE_S);
  uint8_t st = t.status;
  bool alarm = (st == UPTIME_STATUS_DOWN && !stale);
  bool wasDown = (stale && st == UPTIME_STATUS_DOWN);
  bool up = (st == UPTIME_STATUS_UP && !stale);

  statusDrawFrame(gfx, tx, ty, alarm, wasDown, up);

  if (t.kind == UPTIME_KIND_BEAT) {
    int cx = tx + STATUS_HDR_X;
    statusDrawRun(gfx, cx, ty + STATUS_HDR_Y, 1, "Heartbeat", C_DIM);
  } else if (t.kind == UPTIME_KIND_SITE) {
    int cx = tx + STATUS_HDR_X;
    statusDrawRun(gfx, cx, ty + STATUS_HDR_Y, 1, "Monitor", C_DIM);
  }

  char nameBuf[32];
  uint8_t nameSize = 1;
  statusLabelFor(t.name, nameBuf, sizeof(nameBuf), nameSize);
  int nameY = (nameSize == 2) ? STATUS_ROW_A : (STATUS_ROW_A + 3);
  int nameCx = tx + STATUS_NAME_X;
  statusDrawRun(gfx, nameCx, ty + nameY, nameSize, nameBuf, stale ? C_DIM : C_WHITE);

  if (stale) {
    int cxB = tx + 8;
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, "No data", C_DIM);
    if (wasDown) {
      int cxC = tx + 8;
      char wasDownStr[16];
      snprintf(wasDownStr, sizeof(wasDownStr), "Was%cdown", CH_NSP);
      statusDrawRun(gfx, cxC, ty + STATUS_ROW_C, 2, wasDownStr, C_DIM);
    }
    char updBuf[32];
    char durBuf[16];
    statusDurShort(age, durBuf, sizeof(durBuf));
    snprintf(updBuf, sizeof(updBuf), "Updated %s ago", durBuf);
    int cxD = tx + 8;
    statusDrawRun(gfx, cxD, ty + STATUS_ROW_D, 1, updBuf, C_DIM);
    return;
  }

  const char* word = "Up";
  uint16_t wc = C_UGREEN;
  if (st == UPTIME_STATUS_DOWN) { word = "Down"; wc = C_RED; }
  else if (st == UPTIME_STATUS_PENDING) { word = "Pending"; wc = C_GOLD; }
  else if (st == UPTIME_STATUS_PAUSED) { word = "Paused"; wc = C_GREY; }

  int cxB = tx + 8;
  statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, word, wc);

  if (t.hasSinceS && (st == UPTIME_STATUS_UP || st == UPTIME_STATUS_DOWN)) {
    uint32_t effSince = t.sinceS + age;
    char dur[16];
    if (st == UPTIME_STATUS_UP) statusDurLong(effSince, dur, sizeof(dur));
    else statusDurShort(effSince, dur, sizeof(dur));

    statusDrawRunChar(gfx, cxB, ty + STATUS_ROW_B, 2, CH_MID, C_DIM);
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, dur, C_DIM);
  }

  if (st == UPTIME_STATUS_DOWN && t.hasCause) {
    char code[64];
    if (statusCauseCode(t.cause, code, sizeof(code))) {
      int cxC = tx + 8;
      statusDrawRun(gfx, cxC, ty + STATUS_ROW_C, 2, code, C_WHITE);
    }
  }
}

static void statusDrawOverflow(Arduino_GFX* gfx, int tx, int ty, const UptimeTarget* hidden, uint8_t hiddenCount, uint32_t age) {
  bool stale = (age > STATUS_STALE_S);
  uint8_t d = 0;
  bool allUp = true;
  bool anyPending = false;
  for (uint8_t i = 0; i < hiddenCount; i++) {
    if (hidden[i].status == UPTIME_STATUS_DOWN) d++;
    if (hidden[i].status != UPTIME_STATUS_UP) allUp = false;
    if (hidden[i].status == UPTIME_STATUS_PENDING) anyPending = true;
  }
  bool alarm = (d > 0 && !stale);
  uint8_t ovMark = 0; // 0: up, 1: down, 2: pending, 3: paused, 4: stale
  if (stale) ovMark = 4;
  else if (d > 0) ovMark = 1;
  else if (allUp) ovMark = 0;
  else if (anyPending) ovMark = 2;
  else ovMark = 3;

  statusDrawFrame(gfx, tx, ty, alarm, (stale && d > 0), (ovMark == 0 && !stale));

  int cxH = tx + STATUS_HDR_X;
  statusDrawRun(gfx, cxH, ty + STATUS_HDR_Y, 1, "More targets", C_DIM);

  char cntBuf[16];
  snprintf(cntBuf, sizeof(cntBuf), "+%u", (unsigned)hiddenCount);
  int cxA = tx + STATUS_NAME_X;
  statusDrawRun(gfx, cxA, ty + STATUS_ROW_A, 2, cntBuf, stale ? C_DIM : C_WHITE);

  int cxB = tx + 8;
  if (stale) {
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, "No data", C_DIM);
  } else if (d > 0) {
    char dBuf[16];
    snprintf(dBuf, sizeof(dBuf), "%u down", (unsigned)d);
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, dBuf, C_RED);
  } else if (allUp) {
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, "All up", C_UGREEN);
  } else {
    statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, "0 down", C_DIM);
  }

  static const int listY[3] = {70, 80, 90};
  uint8_t numShown = (hiddenCount <= 3) ? hiddenCount : 2;
  for (uint8_t i = 0; i < numShown; i++) {
    uint8_t miniKind = stale ? 4 : hidden[i].status;
    statusDrawMini(gfx, tx + 8, ty + listY[i], miniKind);
    char mName[32];
    statusMiniName(hidden[i].name, mName, sizeof(mName));
    uint16_t col = (hidden[i].status == UPTIME_STATUS_DOWN && !stale) ? C_WHITE : C_DIM;
    int cxL = tx + 17;
    statusDrawRun(gfx, cxL, ty + listY[i], 1, mName, col);
  }
  if (hiddenCount > 3) {
    char moreBuf[16];
    snprintf(moreBuf, sizeof(moreBuf), "+%u more", (unsigned)(hiddenCount - 2));
    int cxM = tx + 17;
    statusDrawRun(gfx, cxM, ty + listY[2], 1, moreBuf, C_DIM);
  }
}

static void statusDrawNoData(Arduino_GFX* gfx, int tx, int ty) {
  statusDrawFrame(gfx, tx, ty, false, false, false);
  int cxH = tx + STATUS_HDR_X;
  statusDrawRun(gfx, cxH, ty + STATUS_HDR_Y, 1, "Uptime", C_DIM);
  int cxA = tx + STATUS_NAME_X;
  statusDrawRun(gfx, cxA, ty + STATUS_ROW_A, 2, "STATUS", C_DIM);
  int cxB = tx + 8;
  statusDrawRun(gfx, cxB, ty + STATUS_ROW_B, 2, "No data", C_DIM);
  int cxD = tx + 8;
  statusDrawRun(gfx, cxD, ty + STATUS_ROW_D, 1, "Never received", C_DIM);
}

void drawStatusSlot(Arduino_GFX* gfx, const UptimeData& u, int i, uint32_t effAgeS, bool nodata) {
  if (!gfx || i < 0 || i > 3) return;
  static const struct { int x, y; } pos[4] = {
    {8, 8}, {124, 8}, {8, 124}, {124, 124}
  };
  int tx = pos[i].x;
  int ty = pos[i].y;
  gfx->fillRect(tx, ty, 108, 108, C_BLACK);   // empty / no-data tiles only outline: clear the slot first
  if (nodata) {
    if (i == 0) statusDrawNoData(gfx, tx, ty);
    else statusDrawEmpty(gfx, tx, ty);
  } else if (i == 3 && u.count > 4) {
    statusDrawOverflow(gfx, tx, ty, u.targets + 3, u.count - 3, effAgeS);
  } else if (i < u.count) {
    statusDrawTile(gfx, tx, ty, u.targets[i], effAgeS);
  } else {
    statusDrawEmpty(gfx, tx, ty);
  }
}

void drawStatusPage(Arduino_GFX* gfx, const UptimeData& u, uint32_t effAgeS, bool nodata) {
  if (!gfx) return;
  gfx->fillScreen(C_BLACK);
  for (int i = 0; i < 4; i++) drawStatusSlot(gfx, u, i, effAgeS, nodata);
}

static const uint32_t kFnvBasis = 2166136261u;
static uint32_t sigByte(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
static uint32_t sigStr(uint32_t h, const char* s) {
  if (!s) s = "";
  while (*s) h = sigByte(h, (uint8_t)*s++);
  return sigByte(h, 0);
}
static uint32_t sigInt(uint32_t h, int v) {
  uint32_t u = (uint32_t)v;
  for (int i = 0; i < 4; i++) h = sigByte(h, (uint8_t)(u >> (i * 8)));
  return h;
}
static uint32_t sigBool(uint32_t h, bool b) { return sigByte(h, b ? 1 : 0); }

static uint32_t statusSlotSig(const UptimeData& u, uint32_t nowMs, bool nodata, int i) {
  uint32_t h = kFnvBasis;
  h = sigBool(h, nodata);
  if (nodata) return h;
  h = sigBool(h, u.valid);
  if (!u.valid) return h;

  uint32_t age = u.ageS + (nowMs - u.rxMillis) / 1000;
  bool stale = (age > STATUS_STALE_S);
  h = sigBool(h, stale);
  if (stale) {
    char durBuf[16];
    statusDurShort(age, durBuf, sizeof(durBuf));
    h = sigStr(h, durBuf);
  }

  h = sigInt(h, u.count);       // decides which slot shows what (overflow, empty)

  {
    if (i == 3 && u.count > 4) {
      uint8_t hiddenCount = u.count - 3;
      h = sigInt(h, hiddenCount);
      const UptimeTarget* hidden = u.targets + 3;
      uint8_t d = 0;
      bool allUp = true;
      bool anyPending = false;
      for (uint8_t j = 0; j < hiddenCount; j++) {
        if (hidden[j].status == UPTIME_STATUS_DOWN) d++;
        if (hidden[j].status != UPTIME_STATUS_UP) allUp = false;
        if (hidden[j].status == UPTIME_STATUS_PENDING) anyPending = true;
      }
      h = sigInt(h, d);
      h = sigBool(h, allUp);
      h = sigBool(h, anyPending);
      uint8_t numShown = (hiddenCount <= 3) ? hiddenCount : 2;
      for (uint8_t j = 0; j < numShown; j++) {
        h = sigInt(h, hidden[j].status);
        char mName[32];
        statusMiniName(hidden[j].name, mName, sizeof(mName));
        h = sigStr(h, mName);
      }
    } else if (i < u.count) {
      const UptimeTarget& t = u.targets[i];
      h = sigInt(h, t.kind);
      h = sigInt(h, t.status);
      char nameBuf[32];
      uint8_t nameSize = 1;
      statusLabelFor(t.name, nameBuf, sizeof(nameBuf), nameSize);
      h = sigInt(h, nameSize);
      h = sigStr(h, nameBuf);
      if (!stale) {
        if (t.hasSinceS && (t.status == UPTIME_STATUS_UP || t.status == UPTIME_STATUS_DOWN)) {
          uint32_t effSince = t.sinceS + age;
          char dur[16];
          if (t.status == UPTIME_STATUS_UP) statusDurLong(effSince, dur, sizeof(dur));
          else statusDurShort(effSince, dur, sizeof(dur));
          h = sigStr(h, dur);
        }
        if (t.status == UPTIME_STATUS_DOWN && t.hasCause) {
          char code[64];
          if (statusCauseCode(t.cause, code, sizeof(code))) {
            h = sigStr(h, code);
          }
        }
      }
    }
  }
  return h;
}

void StatusMode::begin(const Settings& s) {
  (void)s;
  needRender_ = true;
}

void StatusMode::invalidate(const Settings& s) {
  (void)s;
  needRender_ = true;
}

void StatusMode::wake(const Settings& s) {
  (void)s;
  needRender_ = true;
}

void StatusMode::service(const Settings& s) {
  (void)s;
  const UptimeData& u = uptimeGet();
  bool nodata = false;
  uint32_t now = millis();
  if (!u.valid) {
    if (now > 180000UL) {
      nodata = true;
    } else {
      if (s.mode == MODE_STATUS) {
        nodata = true;
      } else {
        return;
      }
    }
  }

  uint32_t effAgeS = 0;
  if (u.valid) {
    effAgeS = u.ageS + (now - u.rxMillis) / 1000;
  }

  Arduino_GFX* gfx = gfxDev();
  if (!gfx) return;
  if (needRender_) {                       // page entered or invalidated: full paint
    drawStatusPage(gfx, u, effAgeS, nodata);
    for (int i = 0; i < 4; i++) slotSig_[i] = statusSlotSig(u, now, nodata, i);
    needRender_ = false;
    return;
  }
  for (int i = 0; i < 4; i++) {            // afterwards only the tiles whose content changed
    uint32_t sig = statusSlotSig(u, now, nodata, i);
    if (sig != slotSig_[i]) {
      slotSig_[i] = sig;
      drawStatusSlot(gfx, u, i, effAgeS, nodata);
    }
  }
}
