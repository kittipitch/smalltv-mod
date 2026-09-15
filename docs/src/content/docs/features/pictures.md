---
title: Pictures
description: A full-screen photo frame fed by the daemon from a folder of your own images.
---

Switch **Display → Mode → Pictures** and the device becomes a small photo
frame: the daemon on your computer converts each picture to the panel's raw
frame format and pushes it, one picture per rotation, full-screen.

The firmware never decodes images — the board has no room for a decoder. The
daemon does all conversion (Pillow, already part of the base install — no
ImageMagick/ffmpeg/`sips` needed) and the device just displays the result.

## Where the images go

A normal folder of pictures **on the machine that runs the
[daemon](https://github.com/kittipitch/clawdmeter-daemon)**, passed with
`--album /path/to/pictures` (a bare `--album` defaults to
`~/Pictures/claudedaemon`). The folder is scanned recursively — subfolders are
fine.

- Formats: `jpg` `jpeg` `png` `bmp` `gif` `webp`.
- Order: filename order, or `--album-shuffle` for random.
- Add or remove pictures any time; the folder is rescanned every rotation, no
  restart needed.

## How pictures are converted (letterbox, not crop)

The daemon scales the **whole** picture to fit the 240×240 panel — the way a
photo frame behaves — and pads the leftover space with black bars. Nothing is
cropped away: a portrait phone photo shows completely, with bars at the sides.

Each converted frame is cached on disk keyed by path + mtime + size, so an
unchanged folder converts nothing on later passes.

## Timing — the rotation interval

The interval lives in one field in the web UI's **Display** tab, labelled per
mode:

- **Pictures** mode: "Each picture shows for (s)" — how long each photo stays
  on screen.
- **Carousel** mode: "Each page shows for (s)" — the Pictures page gets that
  same dwell once per loop among the other pages, right where you placed it
  with the arrows.
- **Next event** mode: "Each page shows for (s)" — with page 2 ticked and more
  than 3 events, standalone Next event flips between the two agenda pages on
  the same interval. (Without page 2 the page is static.)

The daemon sleeps exactly as long as the device tells it to after each push,
so drift corrects itself; if Pictures is not in the rotation at all it checks
back every 5 minutes.

## Set up in three steps

1. Tick **Pictures** in **Display → Carousel** (or set **Mode → Pictures** for
   photos only).
2. Point the daemon at a folder: `python clawdmeter_daemon.py --album
   ~/Pictures/claudedaemon`
3. Keep the daemon running ([Keep the daemon running](/getting-started/keep-it-running/)).

## How it reaches the screen

The daemon POSTs each 115,200-byte RGB565 frame to the device's
`/api/album` endpoint, streamed row by row straight to the panel — the device
only paints while the Pictures page is the one on screen, so a frame landing
mid-carousel never scribbles over another page. Frames are checkpointed to
flash so a reboot mid-picture recovers the last shown photo.
