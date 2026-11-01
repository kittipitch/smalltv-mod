#!/usr/bin/env python3
"""Movie/anime title text: compile the title helpers from MoviesMode.cpp on the host and check what
they do. The title prints in bold yellow ALL CAPS on a see-through banner fitted to each line (owner
2026-11-01). Run from the repo root."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src/features/movies/MoviesMode.cpp").read_text()
code = cpp[cpp.index("// ---- title text"):cpp.index("// ---- end title text")]
src = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
''' + code + r'''
static void check(const char* got, const char* want) {
  if (strcmp(got, want)) { printf("got '%s' want '%s'\n", got, want); assert(0); }
}
int main() {
  assert(kTitleY == 4 && kTitlePitch == 18);   // keep the harness using every helper
  char up[64];
  strcpy(up, "Sousou no Frieren");
  upperAscii(up);
  check(up, "SOUSOU NO FRIEREN");
  strcpy(up, "The Apothecary Diaries 3");
  upperAscii(up);
  check(up, "THE APOTHECARY DIARIES 3");     // digits and spaces untouched

  char lines[2][kTitleCols + 1], w[64];
  strcpy(w, "kusuriya no hitorigoto"); upperAscii(w);
  assert(wrapTitle(w, lines) == 2);
  check(lines[0], "KUSURIYA NO");
  check(lines[1], "HITORIGOTO");
  strcpy(w, "frieren"); upperAscii(w);
  assert(wrapTitle(w, lines) == 1);
  check(lines[0], "FRIEREN");
  strcpy(w, "a very long movie title that will not fit on one"); upperAscii(w);
  assert(wrapTitle(w, lines) == 2);
  check(lines[1] + strlen(lines[1]) - 3, "...");                       // ends in "..."

  // Banner: only as tall as the lines in use, only as wide as each line's letters, inside the poster.
  assert(titleBandRow(0, 2) && titleBandRow(0, 19) && !titleBandRow(0, 1) && !titleBandRow(0, 20));
  assert(titleBandRow(1, 20) && titleBandRow(1, 37) && !titleBandRow(1, 19) && !titleBandRow(1, 38));
  int x0, x1;
  titleBandX("TUNER", 160, &x0, &x1);
  assert(x0 == 88 && x1 == 152);
  titleBandX("KOORI NO JOUHEKI 2ND", 160, &x0, &x1);
  assert(x0 == 0 && x1 == 160);
  // See-through: each channel kept at kBannerPct percent, so the poster still shows.
  assert(kBannerPct == 45);
  uint16_t row[4] = {0xFFFF, 0x0000, 0xFFFF, 0xF800};
  dimPixels(row, 1, 4);
  assert(row[0] == 0xFFFF && row[1] == 0x0000);        // outside the range: untouched
  assert(row[2] == ((13 << 11) | (28 << 5) | 13));     // white
  assert(row[3] == (13 << 11));                        // red
  printf("ok\n");
}
'''
# Look (owner 2026-11-01): bold plain yellow, no outline, on a see-through banner fitted to each line.
draw = cpp[cpp.index("static void drawTitle"):cpp.index("static void drawColumn")]
assert "g->setTextColor(C_YELLOW);" in draw and "C_BLACK" not in draw, "plain yellow, no outline"
assert "x + 1" in draw and draw.count("g->print(") == 2, "bold: printed twice, 1 px apart"
current = cpp[cpp.index("static void drawCurrent"):cpp.index("bool moviesPromote")]
assert current.index("dimPixels(") < current.index("gfxToneRow("), "the poster is dimmed as it is drawn, before the tone step"

with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)
print("test_title_text: OK")
