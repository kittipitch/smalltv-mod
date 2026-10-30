#!/usr/bin/env python3
"""Movies title wrap: compile wrapTitle() from MoviesMode.cpp on the host and check the lines it makes.

At most two lines of 20 characters (size 2 on 240 px), broken at a space, a word longer than a line cut
hard, and a title that does not fit ending in "...". Run from the repo root.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp = (root / "src/features/movies/MoviesMode.cpp").read_text()
code = cpp[cpp.index("static const int kTitleCols"):cpp.index("// Yellow with a 1 px black outline")]
cases = {
    "Clayface": ["Clayface"],
    "One Battle After Another": ["One Battle After", "Another"],
    "Mission: Impossible - The Final Reckoning Part Two": ["Mission: Impossible", "- The Final Recko..."],
    "Supercalifragilisticexpialidocious": ["Supercalifragilistic", "expialidocious"],
    "": [],
    "Exactly twenty chars": ["Exactly twenty chars"],
}
src = "#include <cstdio>\n#include <cstring>\n" + code + "int main(int c, char** v) {\n" \
      "  char out[kTitleLines][kTitleCols + 1]; int n = wrapTitle(v[1], out);\n" \
      "  for (int i = 0; i < n; i++) printf(\"%s|\\n\", out[i]);\n}\n"
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "t.cpp").write_text(src)
    subprocess.run(["c++", "-std=c++17", "-o", f"{d}/t", f"{d}/t.cpp"], check=True)
    for title, want in cases.items():
        got = [l[:-1] for l in subprocess.run([f"{d}/t", title], capture_output=True, text=True,
                                              check=True).stdout.splitlines()]
        assert got == want, (title, got, want)
        assert all(len(l) <= 20 for l in got), got
print("test_movie_title: OK")
