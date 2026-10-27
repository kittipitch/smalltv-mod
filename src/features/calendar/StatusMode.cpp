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

#define STATUS_STALE_S         180
#define STATUS_X0              8
#define STATUS_ROW_W           224
#define STATUS_ROW_H           52
#define STATUS_GAP             5
#define STATUS_PAD             8
#define STATUS_L1              9
#define STATUS_L2              30
#define STATUS_TEXT_L          16
#define STATUS_TEXT_R          223
#define STATUS_ARROW_W         14
#define STATUS_ARROW_H         12

#define STATUS_FRAME_UP        0
#define STATUS_FRAME_DOWN      1
#define STATUS_FRAME_WAS_DOWN  2
#define STATUS_FRAME_OTHER     3

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

static void statusDrawText(Arduino_GFX* gfx, int x, int y, uint8_t size, const char* text, uint16_t col) {
  int cx = x;
  statusDrawRun(gfx, cx, y, size, text, col);
}

static void statusDrawRight(Arduino_GFX* gfx, int y, const char* text, uint16_t col) {
  int w = statusRunW(text, 2);
  int x = STATUS_TEXT_R - w + 1;
  statusDrawText(gfx, x, y, 2, text, col);
}

static void statusDrawArrow(Arduino_GFX* gfx, int x, int y, bool up, uint16_t col) {
  for (int r = 0; r < STATUS_ARROW_H; r++) {
    int k = up ? r : (STATUS_ARROW_H - 1 - r);
    int half = (k * (STATUS_ARROW_W / 2)) / (STATUS_ARROW_H - 1);
    gfx->fillRect(x + STATUS_ARROW_W / 2 - 1 - half, y + 1 + r, 2 + 2 * half, 1, col);
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

static void statusLabelFor(const char* name, char* out, size_t outCap) {
  if (!out || outCap == 0) return;
  out[0] = 0;
  char clean[64];
  statusSanitise(name, clean, sizeof(clean));
  size_t len = strlen(clean);
  while (len > 0 && clean[len - 1] == '/') clean[--len] = 0;
  while (len > 0 && (unsigned char)clean[len - 1] <= ' ') clean[--len] = 0;
  if (clean[0] == 0) {
    strlcpy(clean, "NONAME", sizeof(clean));
    len = 6;
  }
  const size_t N_MAX = 17;
  if (len <= N_MAX) {
    strlcpy(out, clean, outCap);
  } else {
    const char* tail = clean + (len - (N_MAX - 2));
    while (*tail == '.') tail++;
    snprintf(out, outCap, "..%s", tail);
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

static void statusDrawFrame(Arduino_GFX* gfx, int y, uint8_t kind) {
  gfx->fillRoundRect(STATUS_X0, y, STATUS_ROW_W, STATUS_ROW_H, 6, C_BLACK);
  if (kind == STATUS_FRAME_DOWN) {
    gfx->drawRoundRect(STATUS_X0, y, STATUS_ROW_W, STATUS_ROW_H, 6, C_RED);
    gfx->drawRoundRect(STATUS_X0 + 1, y + 1, STATUS_ROW_W - 2, STATUS_ROW_H - 2, 5, C_RED);
  } else if (kind == STATUS_FRAME_UP) {
    // owner 2026-10-27: a healthy row gets a quiet 1 px grey frame so it does not steal attention
    gfx->drawRoundRect(STATUS_X0, y, STATUS_ROW_W, STATUS_ROW_H, 6, C_GREY);
  } else {
    uint16_t col = (kind == STATUS_FRAME_WAS_DOWN) ? C_RED : C_BARBG;
    gfx->drawRoundRect(STATUS_X0, y, STATUS_ROW_W, STATUS_ROW_H, 6, col);
  }
}

static void statusDrawEmpty(Arduino_GFX* gfx, int y) {
  statusDrawFrame(gfx, y, STATUS_FRAME_OTHER);
}

static void statusDrawRow(Arduino_GFX* gfx, int y, const UptimeTarget& t, uint32_t age) {
  bool stale = (age > STATUS_STALE_S);
  uint8_t st = t.status;

  uint8_t fkind;
  if (stale) {
    fkind = (st == UPTIME_STATUS_DOWN) ? STATUS_FRAME_WAS_DOWN : STATUS_FRAME_OTHER;
  } else {
    if (st == UPTIME_STATUS_DOWN) fkind = STATUS_FRAME_DOWN;
    else if (st == UPTIME_STATUS_UP) fkind = STATUS_FRAME_UP;
    else fkind = STATUS_FRAME_OTHER;
  }
  statusDrawFrame(gfx, y, fkind);

  char nameBuf[32];
  statusLabelFor(t.name, nameBuf, sizeof(nameBuf));
  statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L1, 2, nameBuf, stale ? C_DIM : C_WHITE);

  if (stale) {
    const char* word = (st == UPTIME_STATUS_DOWN) ? "Was down" : "No data";
    statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L2, 2, word, C_DIM);
    char ageBuf[16];
    statusDurShort(age, ageBuf, sizeof(ageBuf));
    statusDrawRight(gfx, y + STATUS_L2, ageBuf, C_DIM);
    return;
  }

  if (st == UPTIME_STATUS_UP || st == UPTIME_STATUS_DOWN) {
    statusDrawArrow(gfx, STATUS_TEXT_L, y + STATUS_L2, st == UPTIME_STATUS_UP,
                    (st == UPTIME_STATUS_UP) ? C_UGREEN : C_RED);
  } else {
    const char* word = (st == UPTIME_STATUS_PENDING) ? "Pending" : "Paused";
    uint16_t wc = (st == UPTIME_STATUS_PENDING) ? C_GOLD : C_GREY;
    statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L2, 2, word, wc);
  }

  if (t.hasSinceS && (st == UPTIME_STATUS_UP || st == UPTIME_STATUS_DOWN)) {
    uint32_t effSince = t.sinceS + age;
    char durBuf[16];
    if (st == UPTIME_STATUS_UP) statusDurLong(effSince, durBuf, sizeof(durBuf));
    else statusDurShort(effSince, durBuf, sizeof(durBuf));
    statusDrawRight(gfx, y + STATUS_L2, durBuf, C_DIM);
  }

  if (st == UPTIME_STATUS_DOWN && t.kind != UPTIME_KIND_BEAT && t.hasCause) {
    char code[64];
    if (statusCauseCode(t.cause, code, sizeof(code))) {
      char* codePtr = code;
      if (strncmp(codePtr, "HTTP\x7F", 5) == 0 || strncmp(codePtr, "HTTP ", 5) == 0) {
        codePtr += 5;
      }
      if (*codePtr) {
        int w = statusRunW(codePtr, 2);
        int cx = STATUS_X0 + STATUS_ROW_W / 2 - w / 2;
        statusDrawText(gfx, cx, y + STATUS_L2, 2, codePtr, C_WHITE);
      }
    }
  }
}

static void statusDrawOverflow(Arduino_GFX* gfx, int y, const UptimeTarget* hidden, uint8_t hiddenCount, uint32_t age) {
  bool stale = (age > STATUS_STALE_S);
  uint8_t d = 0;
  bool allUp = true;
  for (uint8_t j = 0; j < hiddenCount; j++) {
    if (hidden[j].status == UPTIME_STATUS_DOWN) d++;
    if (hidden[j].status != UPTIME_STATUS_UP) allUp = false;
  }

  uint8_t fkind;
  if (stale) {
    fkind = (d > 0) ? STATUS_FRAME_WAS_DOWN : STATUS_FRAME_OTHER;
  } else {
    if (d > 0) fkind = STATUS_FRAME_DOWN;
    else if (allUp) fkind = STATUS_FRAME_UP;
    else fkind = STATUS_FRAME_OTHER;
  }
  statusDrawFrame(gfx, y, fkind);

  char line1[32];
  snprintf(line1, sizeof(line1), "+%u more", (unsigned)hiddenCount);
  statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L1, 2, line1, stale ? C_DIM : C_WHITE);

  char line2[32];
  snprintf(line2, sizeof(line2), "%u%c%s", (unsigned)d, CH_NSP, "down");
  statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L2, 2, line2, (d > 0 && !stale) ? C_RED : C_DIM);
}

static void statusDrawNoData(Arduino_GFX* gfx, int y) {
  statusDrawFrame(gfx, y, STATUS_FRAME_OTHER);
  statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L1, 2, "Uptime", C_DIM);
  statusDrawText(gfx, STATUS_TEXT_L, y + STATUS_L2, 2, "No data", C_DIM);
}

void drawStatusSlot(Arduino_GFX* gfx, const UptimeData& u, int i, uint32_t effAgeS, bool nodata) {
  if (!gfx || i < 0 || i > 3) return;
  static const int rowY[4] = {8, 65, 122, 179};
  int y = rowY[i];
  gfx->fillRect(STATUS_X0, y, STATUS_ROW_W, STATUS_ROW_H, C_BLACK);
  if (nodata) {
    if (i == 0) statusDrawNoData(gfx, y);
    else statusDrawEmpty(gfx, y);
  } else if (i == 3 && u.count > 4) {
    statusDrawOverflow(gfx, y, u.targets + 3, u.count - 3, effAgeS);
  } else if (i < u.count) {
    statusDrawRow(gfx, y, u.targets[i], effAgeS);
  } else {
    statusDrawEmpty(gfx, y);
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

  // An empty slot draws the same frame whatever the data's age: give it a fixed signature,
  // so the stale flip does not repaint it for nothing (check_sig_vs_pixels.py).
  if (i >= u.count && !(i == 3 && u.count > 4)) return sigInt(h, -1);

  uint32_t age = u.ageS + (nowMs - u.rxMillis) / 1000;
  bool stale = (age > STATUS_STALE_S);
  h = sigBool(h, stale);

  h = sigInt(h, u.count);       // decides which slot shows what (overflow, empty)

  if (i == 3 && u.count > 4) {
    uint8_t hiddenCount = u.count - 3;
    h = sigInt(h, hiddenCount);
    const UptimeTarget* hidden = u.targets + 3;
    uint8_t d = 0;
    bool allUp = true;
    for (uint8_t j = 0; j < hiddenCount; j++) {
      if (hidden[j].status == UPTIME_STATUS_DOWN) d++;
      if (hidden[j].status != UPTIME_STATUS_UP) allUp = false;
    }
    h = sigInt(h, d);
    h = sigBool(h, allUp);
  } else if (i < u.count) {
    const UptimeTarget& t = u.targets[i];
    h = sigInt(h, t.status);
    char nameBuf[32];
    statusLabelFor(t.name, nameBuf, sizeof(nameBuf));
    h = sigStr(h, nameBuf);
    if (stale) {
      char durBuf[16];
      statusDurShort(age, durBuf, sizeof(durBuf));
      h = sigStr(h, durBuf);
    } else {
      if (t.hasSinceS && (t.status == UPTIME_STATUS_UP || t.status == UPTIME_STATUS_DOWN)) {
        uint32_t effSince = t.sinceS + age;
        char durBuf[16];
        if (t.status == UPTIME_STATUS_UP) statusDurLong(effSince, durBuf, sizeof(durBuf));
        else statusDurShort(effSince, durBuf, sizeof(durBuf));
        h = sigStr(h, durBuf);
      }
      if (t.status == UPTIME_STATUS_DOWN && t.kind != UPTIME_KIND_BEAT && t.hasCause) {
        char code[64];
        if (statusCauseCode(t.cause, code, sizeof(code))) {
          char* codePtr = code;
          if (strncmp(codePtr, "HTTP\x7F", 5) == 0 || strncmp(codePtr, "HTTP ", 5) == 0) {
            codePtr += 5;
          }
          if (*codePtr) {
            h = sigStr(h, codePtr);
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
  for (int i = 0; i < 4; i++) {            // afterwards only the rows whose content changed
    uint32_t sig = statusSlotSig(u, now, nodata, i);
    if (sig != slotSig_[i]) {
      slotSig_[i] = sig;
      drawStatusSlot(gfx, u, i, effAgeS, nodata);
    }
  }
}
