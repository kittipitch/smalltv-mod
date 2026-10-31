#!/usr/bin/env python3
"""Anime card text: compile the anime helpers from MoviesMode.cpp on the host and check what they print.

Score in hundredths -> "8.64"; members in thousands -> "218K", "2.7M", "65M"; the season label is read
from after the title's 0 byte in the 64-byte field. Run from the repo root.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src/features/movies/MoviesMode.cpp").read_text()
code = cpp[cpp.index("// ---- anime card text"):cpp.index("// ---- end anime card text")]
src = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
''' + code + r'''
static void check(const char* got, const char* want) {
  if (strcmp(got, want)) { printf("got '%s' want '%s'\n", got, want); assert(0); }
}
int main() {
  char b[16];
  animeScoreText(864, b, sizeof b); check(b, "8.64");
  animeScoreText(750, b, sizeof b); check(b, "7.50");
  animeScoreText(1000, b, sizeof b); check(b, "10.00");
  membersText(218, b, sizeof b); check(b, "218K");
  membersText(999, b, sizeof b); check(b, "999K");
  membersText(1000, b, sizeof b); check(b, "1.0M");
  membersText(2741, b, sizeof b); check(b, "2.7M");
  membersText(65535, b, sizeof b); check(b, "65M");
  uint8_t t[72] = {'M', 'V', 0x60, 0x03, 218, 0, 3, 7};
  memcpy(t + 8, "Frieren\0Fall 26", 15);
  check(seasonLabel(t), "Fall 26");
  uint8_t full[72] = {'M', 'V', 0, 0, 0, 0, 3, 64};
  memset(full + 8, 'A', 64);                       // title fills the field: no label
  check(seasonLabel(full), "");
  uint8_t odd[72] = {'M', 'V', 0, 0, 0, 0, 3, 200}; // corrupt length: no label, no overrun
  check(seasonLabel(odd), "");
  printf("ok\n");
}
'''
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    subprocess.run(["c++", "-std=c++14", "-Wall", "-Wextra", "-Werror", "-o", f"{d}/t", f"{d}/t.cpp"], check=True)
    subprocess.run([f"{d}/t"], check=True)
print("test_anime_card: OK")
