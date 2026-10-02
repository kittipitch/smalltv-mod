#!/usr/bin/env python3
"""Regenerate the 1-bit weather-icon masks in src/features/calendar/WeatherIcons.h.

WHY THIS EXISTS: WeatherIcons.h said "regenerate via the conversion script" but no
such script was ever committed. With no generator, adding a night icon on 2026-09-27
was done by hand-drawing a filled crescent, which shipped visibly wrong -- every
other icon is 2px line art at 9-15% ink, the hand-drawn one was solid at 26% and
read as a blob next to them. This script is that missing generator.

Needs the upstream font (not vendored -- SIL OFL 1.1, fetched on demand):
  curl -sSL -o weathericons.ttf \
    https://raw.githubusercontent.com/erikflowers/weather-icons/master/font/weathericons-regular-webfont.ttf

The six original icons CANNOT be reproduced from the font by plain thresholding:
the font's strokes land ~5px at 40x40 while the committed icons are ~2px, so they
were thinned somehow before being committed. `outline40()` reproduces that house
style deliberately -- fill the glyph's interior to a silhouette, then keep only a
2px boundary band -- which is how kWxIconMoon was generated from `wi-night-clear`.

Usage:
  python3 wx_icons.py preview f02e      # ASCII + ink% for one codepoint
  python3 wx_icons.py bytes   f02e      # PROGMEM byte rows for pasting
  python3 wx_icons.py audit             # ink% of every committed icon
Ink coverage is the check that matters: stay inside 16-25% (the six originals measure 16.5-24.7%) or the icon will not
look like it belongs.
"""
import sys
from PIL import Image, ImageDraw, ImageFont
import re
FONT='weathericons.ttf'
H='/Users/kk/git_projects/mini_tv_claude_quota/smalltv-mod-src/src/features/calendar/WeatherIcons.h'
def committed(n):
    s=open(H,encoding='utf-8').read()
    m=re.search(r'kWxIcon'+n+r'\[200\] PROGMEM = \{(.*?)\};',s,re.S)
    if not m: return None
    b=[int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',m.group(1))]
    return [[1 if b[y*5+x//8]&(0x80>>(x%8)) else 0 for x in range(40)] for y in range(40)]
def hi(cp,size=400):
    f=ImageFont.truetype(FONT,size)
    img=Image.new('L',(size*3,size*3),0)
    ImageDraw.Draw(img).text((size*1.5,size*1.5),cp,font=f,fill=255,anchor='mm')
    return img.crop(img.getbbox())
def mask(img,thr=128):
    px=img.load()
    return [[1 if px[x,y]>=thr else 0 for x in range(img.width)] for y in range(img.height)]
def fill_holes(m):
    W=len(m[0]);Hh=len(m);seen=[[False]*W for _ in range(Hh)];st=[]
    for x in range(W):
        for y in (0,Hh-1):
            if not m[y][x] and not seen[y][x]: seen[y][x]=True;st.append((x,y))
    for y in range(Hh):
        for x in (0,W-1):
            if not m[y][x] and not seen[y][x]: seen[y][x]=True;st.append((x,y))
    while st:
        x,y=st.pop()
        for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):
            nx,ny=x+dx,y+dy
            if 0<=nx<W and 0<=ny<Hh and not m[ny][nx] and not seen[ny][nx]:
                seen[ny][nx]=True;st.append((nx,ny))
    return [[1 if (m[y][x] or not seen[y][x]) else 0 for x in range(W)] for y in range(Hh)]
def m2img(m):
    im=Image.new('L',(len(m[0]),len(m)),0);p=im.load()
    for y in range(len(m)):
        for x in range(len(m[0])): p[x,y]=255 if m[y][x] else 0
    return im
def to40(img,pad=1,thr=128):
    box=40-2*pad;sc=min(box/img.width,box/img.height)
    g=img.resize((max(1,round(img.width*sc)),max(1,round(img.height*sc))),Image.LANCZOS)
    c=Image.new('L',(40,40),0);c.paste(g,((40-g.width)//2,(40-g.height)//2));px=c.load()
    return [[1 if px[x,y]>=thr else 0 for x in range(40)] for y in range(40)]
def erode(m,n=1):
    cur=[r[:] for r in m]
    for _ in range(n):
        nx=[[0]*40 for _ in range(40)]
        for y in range(1,39):
            for x in range(1,39):
                if cur[y][x] and cur[y-1][x] and cur[y+1][x] and cur[y][x-1] and cur[y][x+1]: nx[y][x]=1
        cur=nx
    return cur
def outline40(cp):
    sil=fill_holes(mask(hi(cp)));m=to40(m2img(sil));e=erode(m,2)
    return [[1 if (m[y][x] and not e[y][x]) else 0 for x in range(40)] for y in range(40)]
def raw40(cp,thr=160):
    return to40(hi(cp),thr=thr)
def ink(m): return sum(map(sum,m))
def diff(a,b): return sum(1 for y in range(40) for x in range(40) if a[y][x]!=b[y][x])
def show(label,m,step=1):
    print(f"--- {label}  ink={ink(m)} ({ink(m)/1600*100:.0f}%)")
    for r in m[::step]: print('    '+''.join('#' if v else '.' for v in r))

def _bytes(m):
    out=[]
    for y in range(40):
        for b in range(5):
            v=0
            for bit in range(8):
                if m[y][b*8+bit]: v |= 0x80>>bit
            out.append(v)
    return out

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv)>1 else 'audit'
    if cmd == 'audit':
        for n in ('Clear','Cloud','Rain','Snow','Storm','Fog','Moon'):
            m = committed(n)
            if not m: continue
            i = ink(m)
            flag = 'ok' if 16 <= i/1600*100 <= 25 else 'OUT OF BAND'
            print(f"{n:6} {i:4} px  {i/1600*100:4.1f}%  {flag}")
    else:
        cp = chr(int(sys.argv[2], 16))
        m = outline40(cp)
        if cmd == 'preview':
            show(f"U+{sys.argv[2].upper()}", m)
        else:
            for i in range(0,200,16):
                print('  ' + ','.join(f'0x{v:02X}' for v in _bytes(m)[i:i+16]) + ',')
