#!/usr/bin/env python3
"""Anime settings under Movies: four keys with their defaults, exported and imported (the star threshold
clamped to 5.0..9.5 as tenths), and the web UI loads and saves each control. Run from the repo root."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
h = (root / "src/Settings.h").read_text()
st = (root / "src/Settings.cpp").read_text()
ui = (root / "src/webui.h").read_text()

assert re.search(r"bool\s+animeMovies,\s*animeSeries,\s*animeEnglish;", h), "bool fields"
assert re.search(r"uint8_t\s+animeMinScore;", h), "uint8_t animeMinScore"

assert "animeMovies = animeSeries = true;" in st, "default: both kinds on"
assert "animeEnglish = false;" in st, "default: romaji"
assert "animeMinScore = 75;" in st, "default: 7.5 stars"
for k in ("animeMovies", "animeSeries", "animeEnglish", "animeMinScore"):
    assert f'root["{k}"]' in st and f's.{k}' in st, f"{k} not exported"
for k in ("animeMovies", "animeSeries", "animeEnglish"):
    assert f'root["{k}"].is<bool>()' in st, f"{k} not imported"
assert 'if (root["animeMinScore"].is<int>()) s.animeMinScore = constrain((int)root["animeMinScore"], 50, 95);' in st, \
    "animeMinScore import must clamp to 50..95"

for k, label in (("animeMovies", "Anime movies"), ("animeSeries", "Anime series"),
                 ("animeEnglish", "English anime titles")):
    assert re.search(rf'<input id="{k}" type="checkbox"[^>]*> {label}', ui), f"{k} checkbox"
    assert f"sc('{k}'" in ui and f"{k}:gc('{k}')" in ui, f"{k} not loaded/saved"
# Star threshold: a pull-down menu 5.0, 5.5 ... 9.5 (owner 2026-11-01, replacing the slider), stored as tenths.
sel = re.search(r'<select id="animeMinScore"[^>]*>(.*?)</select>', ui, re.S)
assert sel, "animeMinScore must be a <select>"
vals = re.findall(r'<option value="(\d+)"[^>]*>([\d.]+)</option>', sel.group(1))
assert vals == [(str(v), f"{v / 10:.1f}") for v in range(50, 100, 5)], vals
assert 'id="animeMinScore" type="range"' not in ui, "slider must be gone"
assert "animeMinScoreVal" not in ui, "no separate value label with a menu"
assert "sv('animeMinScore',String(Math.min(95,Math.max(50,Math.round((c.animeMinScore||75)/5)*5))))" in ui, \
    "menu loads the stored tenths snapped to the nearest 0.5 (an old slider value like 78 still selects 8.0)"
assert "animeMinScore:parseInt(gv('animeMinScore'),10)||75" in ui, "menu saves tenths (null-safe)"
# The controls sit inside the Movies sub-options, so they show and hide with Movies.
movies = ui[ui.index('<div id="moviesControl"'):]
movies = movies[:movies.index("</div>")]
for k in ("animeMovies", "animeSeries", "animeEnglish", "animeMinScore"):
    assert f'id="{k}"' in movies, f"{k} outside moviesControl"
print("test_anime_settings: OK")
