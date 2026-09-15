---
title: What the daemon needs installed
description: Per-feature requirements for clawdmeter-daemon — Python packages, external CLIs like translate-shell, keys, and the log line each missing piece produces.
---

The device shows what the daemon pushes to it. The daemon is
[clawdmeter-daemon](https://github.com/kittipitch/clawdmeter-daemon), a single Python
script that runs on your own machine.

![Data flow: your computer asks Anthropic, then pushes to the SmallTV over your WiFi](/smalltv-mod/assets/daemon-dataflow.svg)

**Only the base is mandatory.** Every page is a separate feature with its own
requirement, and a missing one degrades that page only — the daemon keeps running.

## Base

- **Python 3.10+**, any OS (Windows, macOS, Linux, Raspberry Pi).
- From the cloned `clawdmeter-daemon` directory, in the same virtual environment that
  will run the daemon:
  ```sh
  python3 -m pip install -r requirements.txt     # Windows: py -m pip install -r requirements.txt
  ```
  That covers `httpx` (all HTTP), `python-aqi` (real-time US AQI), `google-auth`
  (calendar), `pyserial` (USB transport), `zeroconf` (device discovery), and
  `pystray` + `Pillow` (tray icon; Pillow also does all Pictures conversion).
- A device to push to: `--push-to smalltv-XXXX.local`, where `XXXX` is the four
  characters on the device's **WiFi tab → Device name**.
- **Run the daemon with the venv's own interpreter**, or the imports fail:
  `.venv/bin/python clawdmeter_daemon.py …` (Windows: `.venv\Scripts\python …`).

:::caution[`--push` discovery only carries the usage page]
Calendar, Weather/AQI, z.ai, OpenRouter, Codex and Antigravity all push to the
**explicit** `--push-to` targets only. Give at least one `--push-to <host>` if you use
any of them; Calendar and Weather also read device-side settings from the first one.
:::

## Per feature

| Page / flag | Needs | If missing |
|---|---|---|
| Claude usage (on by default, no flag) | The **`claude` CLI** on `PATH` and logged in — **or** a long-lived `CLAUDE_CODE_OAUTH_TOKEN` from `claude setup-token` (in the environment or `.env`) | No usage numbers; the device shows the mascot/stale page |
| Codex quota (`--codex`) | The **`codex` CLI**, logged in. The daemon runs `codex app-server` briefly per poll — no API key, no cost | Codex page stays empty |
| Antigravity (`--antigravity`) | The **`agy` CLI**, authenticated, a directory it trusts, and **`lsof`** on `PATH` (the daemon finds `agy`'s port with it). **Each poll is a real billable prompt** | Page empty; leave it off unless you want it |
| z.ai (`--zai`) | Key via `--zai-key` or `CLAWDMETER_ZAI_KEY` (no file fallback) | Page empty |
| OpenRouter (`--openrouter`) | Key via `--openrouter-key`, `CLAWDMETER_OPENROUTER_KEY`, or `~/.openrouter_dot_ai_key` | Page empty |
| Pictures (`--album DIR`) | `Pillow` — already in the base install, and it **is** the converter (decode jpg/jpeg/png/bmp/gif/webp → letterbox-scale the whole picture to the panel → raw frame → disk cache). **No `convert`/`ffmpeg`/`sips` CLI needed** | `Album: Pillow not installed -- pip install -r requirements.txt` |
| Calendar (`--calendar`) | `google-auth` **and** a service-account key + a shared calendar — see [Google Calendar](/smalltv-mod/getting-started/google-calendar/) | `Calendar: google-auth not installed`, or silence |
| Non-English event titles | **`trans` (translate-shell)** on `PATH` — see below | Titles are stripped to ASCII on the device |
| Weather + AQI (`--weather`) | Base packages, an explicit `--push-to <host>`, and latitude/longitude saved in that device's **Agenda & weather** tab | — |
| Auto-discovery (`--push`) | `zeroconf` | Use explicit `--push-to <host>` instead |
| USB transport (`--serial`) | `pyserial` | HTTP push still works |
| Tray icon | `pystray` + `Pillow`; macOS also `pyobjc-framework-Cocoa` (installed automatically) | Run with `--no-tray` |

### Linux tray needs OS packages, not pip

```sh
# Debian / Ubuntu
sudo apt install python3-gi gir1.2-gtk-3.0 gir1.2-ayatanaappindicator3-0.1 python3-tk
# Fedora
sudo dnf install python3-gobject gtk3 libayatana-appindicator-gtk3 python3-tkinter
# Arch
sudo pacman -S python-gobject gtk3 libayatana-appindicator tk
```

GNOME on Wayland also needs the **AppIndicator and KStatusNotifier Support** extension,
or the icon simply never appears.

## translate-shell, for non-English calendar titles

The display font is ASCII-only, so the firmware strips anything else — a Thai or
Japanese event title arrives on screen as blanks. The daemon fixes this **before**
pushing, by romanizing the title with `trans` (translate-shell).

Install it:

```sh
brew install translate-shell          # macOS
sudo apt install translate-shell      # Debian / Ubuntu
sudo dnf install translate-shell      # Fedora
sudo pacman -S translate-shell        # Arch
```

Check it resolves **in the environment that runs the daemon**: `command -v trans`. On
native Windows the daemon needs a Windows-callable `trans`; one installed inside WSL is
invisible to it.

How the daemon uses it, so the behaviour is not surprising:

- It calls `trans -e bing -b :en "<title>"`. **Bing, deliberately** — translate-shell's
  default Google endpoint rate-limits per source network, and every machine behind one
  home connection hits `[ERROR] Rate limiting` at the same time.
- It asks for English output. Proper names usually come back transliterated, while
  ordinary words can be translated by meaning.
- Results are cached in `~/.clawdmeter-translate-cache.json` and survive restarts. Only
  successes are cached.
- Calls are paced 1.5 s apart, each has a 15 s timeout, and a title that fails is not
  retried for 5 minutes.
- Missing binary logs once, then the daemon carries on:

  ```
  Calendar: `trans` (translate-shell) not found on PATH -- non-English event titles will be stripped device-side instead of translated
  ```

If a title stays wrong after you install `trans`, delete its entry from the cache file
(or the whole file) and restart.

## Pictures (`--album`): the converter is Pillow, not a CLI

The board has no image decoder and no room for one, so conversion happens entirely in
the daemon. **You do not need ImageMagick, ffmpeg, or `sips`** — Pillow (already
installed with the base requirements) does everything:

1. It reads any jpg/jpeg/png/bmp/gif/webp from your album folder (recursively, filename
   order or `--album-shuffle`).
2. Letterbox-scales the **whole** picture to fit the panel's 240×240 (the way
   a photo frame behaves — nothing cropped) and converts it to the device's
   raw RGB565 frame format; leftover space is padded black.
3. Caches the finished frame in `~/.clawdmeter-album-cache/`, keyed by path + mtime +
   size — an unchanged folder converts nothing on later passes.
4. Pushes one frame per carousel rotation and sleeps for exactly as long as the device
   says the Pictures page is away (`next` in the push reply), so a 9-page rotation at
   15 s/page feeds a frame roughly every 2 minutes.

Setup is three steps: tick **Pictures** in the device's web UI (Display → Carousel),
point `--album` at a folder of photos, and keep the daemon running
([Keep the daemon running](/smalltv-mod/getting-started/keep-it-running/)). Drop more
pictures in any time — the daemon rescans the folder every rotation, no restart needed.

Full device-side story — where images go, letterboxing, timing: [Pictures](/smalltv-mod/features/pictures/).

## Quick self-check

```sh
python3 --version                       # 3.10 or newer
python3 -c 'import httpx, aqi; print("base ok")'
python3 -c 'import google.auth; print("calendar ok")'
command -v claude codex agy trans lsof  # macOS/Linux: the external CLIs you plan to use
# Windows (cmd/PowerShell): where claude & where codex & where trans
```

Once it works, make it start by itself: [Keep the daemon running](/smalltv-mod/getting-started/keep-it-running/).

Then start the daemon in a terminal and read the first 20 lines: it prints one line per
feature it is polling. Anything you enabled that is missing from that list is missing a
requirement above.
