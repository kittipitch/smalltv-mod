#!/usr/bin/env python3
"""Regenerate src/features/calendar/WeatherIcons.h.

Pipeline (verified byte-identical to the checked-in masks via --verify-28):
render the erikflowers weather-icons SVG, composite over white, threshold at
128, produce a 40x40 1-bit mask, center the ink.

Two mask paths:
  direct40  -- render at width 40, threshold. The glyph's viewBox padding
               leaves ~28px of ink; byte-identical to the original masks.
  large     -- render at 160, crop the ink, LANCZOS-resize the ink to fit a
               box square (default 36 = the moon's ink height), re-threshold,
               center. Same glyphs, same style, moon-scale weight.

Usage:
  python3 scripts/gen_weather_icons.py --verify-28   # prove pipeline vs git HEAD
  python3 scripts/gen_weather_icons.py               # large icons (box 36)
  python3 scripts/gen_weather_icons.py --box 28      # revert to original look
"""
import argparse, io, re, subprocess, sys, tempfile, os, urllib.request
from PIL import Image

OUT = os.path.join(os.path.dirname(__file__), '..',
                   'src', 'features', 'calendar', 'WeatherIcons.h')
BASE = 'https://raw.githubusercontent.com/erikflowers/weather-icons/master/svg'
GLYPHS = {
    'Clear': 'wi-day-sunny',
    'Cloud': 'wi-cloud',
    'Rain':  'wi-rain',
    'Snow':  'wi-snow',
    'Storm': 'wi-thunderstorm',
    'Fog':   'wi-fog',
}

def fetch_glyph(glyph):
    url = f'{BASE}/{glyph}.svg'
    req = urllib.request.Request(url, headers={'User-Agent': 'smalltv-mod-icon-gen'})
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read()

def fetch(name):
    url = f'{BASE}/{GLYPHS[name]}.svg'
    req = urllib.request.Request(url, headers={'User-Agent': 'smalltv-mod-icon-gen'})
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read()

def raster(svg_bytes, width):
    fd, p = tempfile.mkstemp(suffix='.svg')
    try:
        with os.fdopen(fd, 'wb') as f:
            f.write(svg_bytes)
        out = subprocess.run(['rsvg-convert', '-w', str(width)],
                             stdin=open(p, 'rb'), capture_output=True)
    finally:
        os.unlink(p)
    if out.returncode != 0:
        sys.exit(f'rsvg-convert failed: {out.stderr.decode()[:200]}')
    im = Image.open(io.BytesIO(out.stdout)).convert('RGBA')
    bg = Image.new('RGBA', im.size, (255, 255, 255, 255))
    return Image.alpha_composite(bg, im).convert('L')

def rows_direct40(svg_bytes):
    bw = raster(svg_bytes, 40).point(lambda v: 255 if v < 128 else 0)
    pix = bw.load()
    return [''.join('1' if pix[x, y] else '0' for x in range(40)) for y in range(40)]

def rows_large(svg_bytes, box):
    hi = raster(svg_bytes, 160)
    bw = hi.point(lambda v: 255 if v < 128 else 0)
    crop = bw.crop(bw.getbbox())
    w, h = crop.size
    scale = box / max(w, h)
    w2, h2 = max(1, round(w * scale)), max(1, round(h * scale))
    small = crop.resize((w2, h2), Image.LANCZOS).point(lambda v: 255 if v >= 128 else 0)
    canvas = Image.new('L', (40, 40), 0)
    canvas.paste(small, ((40 - w2) // 2, (40 - h2) // 2))
    pix = canvas.load()
    return [''.join('1' if pix[x, y] else '0' for x in range(40)) for y in range(40)]

def carray(rows):
    out = []
    for y in range(40):
        row = rows[y]
        hexes = ['0x%02X' % int(row[i:i+8], 2) for i in range(0, 40, 8)]
        out.append('  ' + ','.join(hexes) + ',')
    return chr(10).join(out)

def block(cat, rows):
    return 'static const uint8_t kWxIcon%s[200] PROGMEM = {\n%s\n};\n' % (cat, carray(rows))

MOON_NOTE = """
// %s
//
// Used ONLY for the current-conditions icon when the daemon reports night
// (isDay=0) and the sky is clear -- never on the 3-day forecast rows.
static const uint8_t kWxIconMoon[200] PROGMEM = {
%s
};
"""

def current_moon_rows():
    hdr = open(OUT).read()
    m = re.search(r'kWxIconMoon\[200\] PROGMEM = \{(.*?)\};', hdr, re.S)
    bs = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]
    return [''.join(f'{b:08b}' for b in bs[r*5:(r+1)*5]) for r in range(40)]

def emit(rows_by_cat, moon_rows, box, moon_src=None):
    hdr_comment = """// WeatherIcons.h -- auto-generated 1-bit PROGMEM masks, 40x40.
// Source: erikflowers/weather-icons (SIL OFL 1.1), rasterized then thresholded
// to a monochrome mask. Drawn via Arduino_GFX::drawBitmap(x,y,mask,w,h,color) --
// the library's own PROGMEM-safe 1-bit path (pgm_read_byte, same mechanism this
// codebase already uses for font glyphs). Regenerate with
// scripts/gen_weather_icons.py; --verify-28 proves the pipeline byte-identical
// against the original 28px-wide masks in git. (Earlier RGB565 pgm_read_word
// version crashed on-device -- LoadStoreError on a uint16_t PROGMEM array;
// byte-wide reads don't hit it.)
#pragma once
#include <Arduino.h>

#define WX_ICON_SIZE 40

"""
    parts = [hdr_comment]
    for cat in ['Clear', 'Cloud', 'Rain', 'Snow', 'Storm', 'Fog']:
        parts.append(block(cat, rows_by_cat[cat]))
    if moon_src:
        note = ("Crescent moon from erikflowers/weather-icons (%s), rendered by the\n"
                "// same pipeline as the six above (ink in a %dx%d box, centered) --\n"
                "// replaces the original geometric crescent so the whole\n"
                "// current-conditions set shares one collection. Same 1-bit PROGMEM\n"
                "// byte path." % (moon_src, box, box))
    else:
        note = ("Crescent moon, generated geometrically (filled r=18 disc minus an\n"
                "// offset r=17.5 disc, opening left) -- erikflowers/weather-icons has\n"
                "// no night variants. Same 40x40 1-bit layout and byte-wide PROGMEM\n"
                "// path as the six above.")
    parts.append(MOON_NOTE % (note, carray(moon_rows)))
    with open(OUT, 'w') as f:
        f.write(chr(10).join(parts))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--box', type=int, default=36)
    ap.add_argument('--verify-28', action='store_true')
    ap.add_argument('--moon', metavar='GLYPH', default=None,
                    help='replace the geometric moon with a collection glyph '
                         '(e.g. wi-night-clear) at the same box')
    args = ap.parse_args()

    if args.moon:
        moon = rows_large(fetch_glyph(args.moon), args.box)
        moon_src = args.moon
    else:
        moon = current_moon_rows()
        moon_src = None
    rows = {}
    for cat in GLYPHS:
        rows[cat] = rows_direct40(fetch(cat)) if args.verify_28 else rows_large(fetch(cat), args.box)

    if args.verify_28:
        old = subprocess.run(['git', 'show', 'HEAD:src/features/calendar/WeatherIcons.h'],
                             capture_output=True, text=True, check=True).stdout
        bad = 0
        for cat in GLYPHS:
            m = re.search(r'kWxIcon'+cat+r'\[200\] PROGMEM = \{(.*?)\};', old, re.S)
            bs = [int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))]
            old_rows = [''.join(f'{b:08b}' for b in bs[r*5:(r+1)*5]) for r in range(40)]
            ok = old_rows == rows[cat]
            print(cat, 'IDENTICAL' if ok else 'MISMATCH')
            bad += 0 if ok else 1
        if bad:
            sys.exit(1)
        return

    emit(rows, moon, args.box, moon_src)
    print('wrote', os.path.normpath(OUT))

if __name__ == '__main__':
    main()
