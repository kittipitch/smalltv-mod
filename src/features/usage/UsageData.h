// UsageData.h — runtime (volatile) Claude usage snapshot from the daemon.
#pragma once
#include <Arduino.h>

struct UsageData {
  float    sessionPct;       // 5-hour window utilization (0..100)
  int      sessionResetMin;  // minutes until the 5-hour window resets
  float    weeklyPct;        // 7-day window utilization (0..100)
  int      weeklyResetMin;   // minutes until the 7-day window resets
  bool     hasWeekly;        // false = the daemon sent no 7d window at all.
                             // Org-managed accounts get no anthropic-ratelimit-
                             // unified-7d-* headers, and a missing header used to
                             // default to 0, rendering a fabricated 0% / resets-now
                             // that never changed. False means no such window,
                             // which draws as N/A.
  char     status[16];       // e.g. "allowed", "allowed_warning", "rejected"

  bool     valid;            // populated at least once
  bool     error;            // most recent fetch failed
  uint32_t lastOkMs;         // millis() of last good update

  void clear() {
    sessionPct = weeklyPct = 0;
    sessionResetMin = weeklyResetMin = 0;
    hasWeekly = false;   // absent until a payload actually carries a w field
    status[0] = 0;
    valid = false;
    error = false;
    lastOkMs = 0;
  }
};
