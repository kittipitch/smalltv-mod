#!/usr/bin/env python3
"""Anime settings under Movies: four keys with their defaults, exported and imported (the star threshold
clamped to 7.0..9.5 as tenths), and the web UI loads and saves each control. Run from the repo root."""
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
assert 'if (root["animeMinScore"].is<int>()) s.animeMinScore = constrain((int)root["animeMinScore"], 70, 95);' in st, \
    "animeMinScore import must clamp to 70..95"

for k, label in (("animeMovies", "Anime movies"), ("animeSeries", "Anime series"),
                 ("animeEnglish", "English anime titles")):
    assert re.search(rf'<input id="{k}" type="checkbox"[^>]*> {label}', ui), f"{k} checkbox"
    assert f"sc('{k}'" in ui and f"{k}:gc('{k}')" in ui, f"{k} not loaded/saved"
assert re.search(r'<input id="animeMinScore" type="range" min="7" max="9.5" step="0.1"', ui), "slider"
assert "animeMinScoreVal" in ui, "slider shows its value"
assert "(c.animeMinScore||75)/10" in ui, "slider loads tenths as stars"
assert "animeMinScore:Math.round(+gv('animeMinScore')*10)||75" in ui, "slider saves stars as tenths (null-safe)"
assert "sv('animeMinScore'" in ui, "slider loads null-safe (a lean build may drop the Movies tab)"
# The controls sit inside the Movies sub-options, so they show and hide with Movies.
movies = ui[ui.index('<div id="moviesControl"'):]
movies = movies[:movies.index("</div>")]
for k in ("animeMovies", "animeSeries", "animeEnglish", "animeMinScore"):
    assert f'id="{k}"' in movies, f"{k} outside moviesControl"
print("test_anime_settings: OK")
