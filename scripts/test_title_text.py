#!/usr/bin/env python3
"""Movie/anime title text: compile the title helpers from MoviesMode.cpp on the host and check what
they do. The title prints in ALL CAPS (owner 2026-11-01); the frame's final byte carries the poster
band's luminance and the title fills black with a yellow outline when that band is bright (yellow on
bright posters is hard to read). Run from the repo root."""
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

  assert(!titleBright(0) && !titleBright(kTitleLumBright - 1));
  assert(titleBright(kTitleLumBright) && titleBright(255));
  printf("ok\n");
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    r = subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    subprocess.run([f"{d}/t"], check=True)
print("test_title_text: OK")
