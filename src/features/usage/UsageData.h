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

  // Free "limit reset" credits, same shape and meaning as CodexData's pair so the
  // two pages can share one renderer. Unlike Codex's -- which come from a real
  // field in the app-server RPC (`rateLimitResetCredits`) -- Anthropic exposes no
  // such field anywhere: not in the 34 anthropic-ratelimit-unified-* headers, and
  // not in the promo status schema (`{eligible, claimed, state}` behind the
  // tengu_swift_lynx flag config), which carries no expiry at all. These therefore
  // come from a value the owner enters in the daemon's own .env
  // (CLAUDE_RESET_CREDITS="<count>@<YYYY-MM-DD>"), transcribed from the promo
  // email. The daemon recomputes the countdown on every push and stops sending
  // both keys once the date passes, so an expired credit disappears on its own.
  int      resetCredits;              // count currently available
  bool     hasResetCredits;
  int      resetCreditExpireMins;     // minutes until the soonest one expires
  bool     hasResetCreditExpireMins;

  bool     valid;            // populated at least once
  bool     error;            // most recent fetch failed
  uint32_t lastOkMs;         // millis() of last good update

  void clear() {
    sessionPct = weeklyPct = 0;
    sessionResetMin = weeklyResetMin = 0;
    hasWeekly = false;   // absent until a payload actually carries a w field
    status[0] = 0;
    resetCredits = 0; hasResetCredits = false;
    resetCreditExpireMins = 0; hasResetCreditExpireMins = false;
    valid = false;
    error = false;
    lastOkMs = 0;
  }
};
