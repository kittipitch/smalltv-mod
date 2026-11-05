#!/usr/bin/env python3
"""Codex credit card: the "Exp in" row must fit the card like the "Resets in" row. Text size 2 is 12 px a character
wide, and the row is a right-aligned 17-character field ending 14 px inside the card, the same right edge as the value
and the bar. Compiles the row helpers on the host. Run from the repo root."""
import re
import subprocess
import tempfile
from pathlib import Path

cpp = (Path(__file__).resolve().parents[1] / "src/features/calendar/CodexMode.cpp").read_text()
assert "// ---- codex expire row" in cpp, "fmtExpireRow is missing"
row = cpp[cpp.index("// ---- codex expire row"):cpp.index("// ---- end codex expire row")]
reset = re.search(r"static void fmtReset\(.*?\n}\n", cpp, re.S).group(0)
card = cpp[cpp.index("static void drawCodexResetCard(Arduino_GFX* gfx, int top, bool hasCredits,\n                               int credits, bool hasExpire, int expireMins,\n                               bool full, bool growRight) {"):]
card = card[:card.index("\n}\n")]
assert "fmtExpireRow(" in card, "the card must print through fmtExpireRow"
assert "setCursor(x + w - 14 - gfxTextW(line, 2), top + 66)" in card, "same right edge as the Resets in row"

src = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
''' + reset + row + r'''
int main() {
  char rs[16], line[18];
  for (int mins = -1; mins < 100 * 1440; mins++) {     // far beyond the 7-day credit window
    fmtReset(mins, rs, sizeof(rs));
    fmtExpireRow(rs, line, sizeof(line));
    assert(strlen(line) == 17);                        // 204 px, never past the shared right edge
    assert(strstr(line, "Exp in "));                   // owner's wording, at every countdown
    assert(strcmp(line + 17 - strlen(rs), rs) == 0);   // the countdown is never cut off
  }
  fmtExpireRow("--", line, sizeof(line));
  assert(strcmp(line, "        Exp in --") == 0);
  fmtReset(4 * 1440, rs, sizeof(rs));
  fmtExpireRow(rs, line, sizeof(line));
  assert(strcmp(line, "     Exp in 4d 0h") == 0);
  fmtExpireRow("10d 23h", line, sizeof(line));
  assert(strcmp(line, "   Exp in 10d 23h") == 0);
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)
print("test_codex_expire_row: OK")
