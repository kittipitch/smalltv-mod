# Making a GeekMagic Screen Show Your Claude Code Quota, Agenda, and Weather — Full Guide

Source (Thai original): https://share.aiceo.im/geekmagic-claude-meter/

## Goal

A tiny desk display showing:
- Your Claude Code (CC) usage in real time — % used of your 5-hour and 7-day limits
- Your next few Google Calendar events (agenda)
- Local weather, rain chance, and air quality (AQI/PM2.5)
- (Optional) a stock/crypto ticker

...without opening chat, your calendar app, or a weather site to check.

## The pieces

- **GeekMagic SmallTV / SmallTV Ultra** — a tiny pixel-display desk gadget,
  ESP8266 chip (WiFi 2.4GHz only)
- **Stock firmware** — only does clock/weather/photo album; no Claude usage
  support (that's PRO-model-only, for crypto/stocks)
- **This firmware fork ([`kittipitch/smalltv-mod`](https://github.com/kittipitch/smalltv-mod))**
  — community firmware (originally by giovi321) adding a Claude usage mode,
  then extended in this fork with an Agenda + Weather/AQI mode, device
  write-auth (secret key), display color correction, and a usage-bar
  direction option
- **[`clawdmeter-daemon`](https://github.com/kittipitch/clawdmeter-daemon)**
  — a small program that runs on a computer you control (Mac, Linux, or
  Windows), reads your Claude quota (and optionally your Google Calendar and
  local weather), and pushes it all over WiFi to the device

## What the device can show

The web UI (`http://<device-ip>/`) lets you turn any of these on and pick a
`carousel` that rotates through whichever are enabled, or pin the display to
one mode:

- **Claude usage** — animated mascot + big 5h/7d percentages, fill bars
  (direction configurable — left-to-right by default), reset countdowns
- **Agenda** — up to 3 upcoming Google Calendar events, one card each
- **Weather + AQI** — current temperature, condition (icon + word + WMO
  code), rain probability, US AQI (6-band color scale), PM2.5, and your
  city name
- **Stock/crypto ticker** — optional, off by default if you don't configure
  any symbols
- A small clock overlay (top-right) on the Claude-usage page

All data (usage, agenda, weather) is **pushed** to the device by the daemon
running on your computer — the device itself never talks to Claude, Google,
or a weather API directly.

## Steps

### 1. Connect the device to your home WiFi (first time)

1. Power it on — the device broadcasts a hotspot (`GIFTV` on stock firmware,
   `SmallTV-Setup` once this firmware is installed)
2. Connect your computer to that WiFi (ignore "no internet" warnings)
3. Open `http://192.168.4.1`
4. Network → Scan → pick your home WiFi (**2.4GHz only**) → enter password → Save

### 2. Flash this firmware

Download the latest release from
[`kittipitch/smalltv-mod`'s Releases page](https://github.com/kittipitch/smalltv-mod/releases),
or build it yourself (`pio run -e smalltv` — see the main `README.md` in this
repo for the full build/flash matrix across board variants).

**SmallTV-Ultra** needs a two-step flash (its stock firmware's OTA slot is
too small for the full image):
- **Step 1**: device's existing settings page → Firmware Upgrade → upload
  `smalltv-mod-loader.bin` → device restarts, broadcasts an open hotspot
  `SmallTV-Loader`
- **Step 2**: connect to `SmallTV-Loader` → `192.168.4.1` → upload
  `smalltv-mod-firmware.bin` → restart → now running this fork's firmware

**Plain SmallTV (ESP8266)** flashes in one step: `http://<device-ip>/update`
→ upload `smalltv-mod-firmware.bin`.

⚠️ **macOS auto-switches WiFi away mid-upload.** During a large-file upload
macOS may switch back to your home WiFi (it treats a no-internet network as
"worse"). Fix: join the device's WiFi and start the upload in one
uninterrupted step, and retry if it drops (2-3 rounds usually gets it
through).

Once flashed, every future update is one-step OTA from this firmware's own
web UI (Update tab → "Check for update", or manual `.bin` upload) — the
two-step loader dance is a one-time thing only for the Ultra's stock
firmware.

⚖️ **Trade-off to know before deciding**: this firmware is unofficial, and
flashing it replaces the device's original modes (clock/weather/photo album)
entirely — confirm with whoever owns the device first, since it's an
all-or-nothing swap.

### 3. Set the device's modes

In the web UI: pick which modes to include in the carousel (Usage, Agenda &
weather, Ticker), or pin one mode permanently. This can be done anytime from
`http://<device-ip>/`, no reflash needed.

If your daemon machine can't resolve the device's `.local` hostname, find its
IP instead via your router's DHCP client list (look for an `ESP-xxxxxx`
hostname), and pass that IP to `--push-to` below instead of the hostname.

### 4. Install and configure `clawdmeter-daemon`

1. Clone [`kittipitch/clawdmeter-daemon`](https://github.com/kittipitch/clawdmeter-daemon)
   (or the upstream `giovi321/clawdmeter-daemon` — same code, no secrets
   baked into either)
2. Install dependencies (needs a real Python — see the Windows gotcha below)
3. Run it pointed at your device:
   ```
   python3 clawdmeter_daemon.py --push --push-to <device-ip-or-hostname> --no-discover --interval 300
   ```
4. Set it to auto-start (macOS: `--install` registers a LaunchAgent; Linux:
   run as a `systemd --user` service; Windows: `install.bat` registers a
   `HKCU\...\Run` autostart entry). **Gotcha**: `--install` ignores any
   transport/interval flags passed in the *same* command — it saves nothing
   and exits immediately. Run the command from step 3 once by itself first
   (so your settings get saved to disk), stop it, then run `--install`
   separately to register autostart with those saved settings.

**Getting a Claude token — do this, never paste a token into chat:**
Run `claude setup-token` directly in a terminal on the machine that will run
the daemon. It writes straight to that OS's credential store (macOS
Keychain, or `~/.claude/.credentials.json` on Linux/Windows) with nothing
printed to the screen — no copy-paste, no `.env` file, no risk of a token
ending up in a chat transcript. If the daemon machine has no browser for the
interactive login, run `claude setup-token` on a machine that does, then set
the resulting token as the `CLAUDE_CODE_OAUTH_TOKEN` environment variable on
the daemon machine instead (still don't paste it into a chat window while
doing this — type or paste it directly into a `.env` file or your shell).

⚠️ **Windows Python gotcha**: many Windows machines only have the Microsoft
Store's `python`/`python3` app-execution-alias stub on PATH, which doesn't
actually run anything. Install a real interpreter first:
`winget install -e --id Python.Python.3.12`.

**Optional: Agenda (Google Calendar)**
1. `python3 clawdmeter_daemon.py --calendar-auth` — one-time interactive
   Google OAuth login (needs a browser on the machine you run this on)
2. Add `--calendar` to your normal daemon command line to start pushing
   agenda data
3. Google expires the login after 7 days unless you publish your OAuth
   consent screen to "Production" in the Google Cloud Console (no
   verification review needed for a personal app using only the read-only
   Calendar scope) — the daemon's own `README.md` has the exact steps

**Optional: Weather + AQI**
1. Set your location in the device's own web UI (Agenda & weather tab →
   "Use my location", or enter lat/lon manually)
2. Add `--weather` to your daemon command line — no API key needed
   (Open-Meteo is free and keyless)

**Optional: device write-auth (secret key)**
If you want to stop other devices on your LAN from being able to overwrite
the screen or its settings, set a secret key in the device's web UI (Update
tab → Access card). If you do this, you **must** also pass
`--device-secret-key <the same key>` (or set `CLAWDMETER_SECRET_KEY`) to the
daemon — **set the daemon's key before the device's key**, not after, or
every push will be rejected until the daemon catches up. Leave both unset if
you don't need this (the device is open by default, same as most home LAN
gadgets).

## How much power / quota does this cost?

**Power**: the device itself barely draws anything (ESP8266 + small screen,
roughly nightlight-level, 0.5-2W). The daemon machine is the real variable —
if it sleeps, pushes stop and the screen just freezes on the last values
until it wakes. Running the daemon on an always-on low-power box (a
Raspberry Pi, for example) avoids this entirely.

**Claude Code quota**: checking your quota costs a tiny sliver of quota
itself, because the daemon has to fire a real API request to read the
"remaining quota" values off the response headers — there's no way to read
this without a real request. It uses your own account token, not a separate
key, so no extra dollar cost, but it does eat a little of your time-based
quota. The daemon uses the cheapest model (`claude-haiku-4-5`) with
`max_tokens: 1` for this check. Default poll interval is every 60 seconds
(~1,440 checks/day) — raising `--interval` to e.g. 300s (5 minutes) cuts
that self-consumption roughly 5x, trading slightly slower on-screen updates.

## System overview

```
Claude Code (token via claude setup-token)  --\
Google Calendar (OAuth, --calendar-auth)     ---> clawdmeter-daemon --> GeekMagic screen
Open-Meteo (weather, no key needed)         --/     (always running)      (smalltv-mod)
```

Each data source is independent and optional — run with just `--push` for
usage only, or add `--calendar`/`--weather` for the rest.

---

References: github.com/kittipitch/smalltv-mod · github.com/kittipitch/clawdmeter-daemon
