# Making a GeekMagic Screen Show Your Claude Code / Codex / Antigravity / z.ai Quota, Agenda, and Weather — Full Guide

Source (Thai original): https://share.aiceo.im/geekmagic-claude-meter/

## Goal

A tiny desk display showing:
- Your Claude Code (CC), Codex CLI, Antigravity CLI, and/or z.ai usage in
  real time — quota %, reset/expiry countdowns
- Your next few Google Calendar events (agenda, up to 6 across two pages)
- Local weather, rain chance, and air quality (AQI/PM2.5)
- Nearby aircraft (plane radar), centred on your home or weather location
- (Optional) a stock/crypto ticker

...without opening chat, your calendar app, or a weather site to check.

## The pieces

- **GeekMagic SmallTV / SmallTV Ultra** — a tiny pixel-display desk gadget,
  ESP8266 chip (WiFi 2.4GHz only)
- **Stock firmware** — only does clock/weather/photo album; no quota-page
  support (that's PRO-model-only, for crypto/stocks)
- **This firmware fork ([`kittipitch/smalltv-mod`](https://github.com/kittipitch/smalltv-mod))**
  — community firmware (originally by giovi321) adding a Claude usage mode,
  then extended in this fork with z.ai/Codex/Antigravity quota pages, an
  Agenda + Weather/AQI mode, plane radar, device write-auth (secret key),
  display color correction, user-configurable carousel order, and a
  usage-bar direction option
- **[`clawdmeter-daemon`](https://github.com/kittipitch/clawdmeter-daemon)**
  — a small program that runs on a computer you control (Mac, Linux, or
  Windows), reads your Claude/Codex/Antigravity/z.ai quota (and optionally
  your Google Calendar and local weather), and pushes it all over WiFi to
  the device

## What the device can show

The web UI (`http://<device-ip>/`) lets you turn any of these on and pick a
`carousel` that rotates through whichever are enabled (drag to reorder), or
pin the display to one mode:

- **Claude usage** — animated mascot + big 5h/7d percentages, fill bars
  (direction configurable — left-to-right by default), reset countdowns
- **z.ai** — 5h cycle % and a monthly MCP-tools quota %, each with a reset
  countdown
- **Codex** — weekly quota % + reset countdown, plus a second card for free
  "rate-limit reset" credits and when the soonest one expires unused
- **Antigravity** — two cards: your Gemini Pro family's quota % + reset
  countdown, and your Gemini Flash family's, each labeled with which
  version is currently backing the number (e.g. "3.6 Flash" — picks the
  newest version your account has, not just whichever is numerically
  tightest). Non-Gemini models this account may also have access to
  (Claude, GPT-OSS variants run through `agy`) aren't shown here — they
  have their own dedicated pages already. The shown percentage is capped
  at 99% (the bar/color underneath still reflects the real value), and
  the number itself turns red exactly when the real quota is fully
  exhausted (100%) — white otherwise — so a capped 99% is never mistaken
  for a real 99%
- **Agenda** — up to 6 upcoming Google Calendar events (3 per page, a
  second page auto-appears if you have more than 3), color-coded by which
  calendar each event came from
- **Weather + AQI** — current temperature, condition (icon + word + WMO
  code), rain probability, US AQI (6-band color scale), PM2.5, and your
  city name
- **Plane radar** — nearby aircraft as heading triangles with callsign/
  altitude labels, centred on your home location (or the weather location
  above, if you haven't set a separate one for radar)
- **Stock/crypto ticker** — optional, off by default if you don't configure
  any symbols
- A small clock overlay (top-right) on every quota page

All data (usage, agenda, weather, z.ai, Codex, Antigravity) is **pushed** to
the device by the daemon running on your computer — the device itself never
talks to any of those APIs directly. Plane radar is the one exception: the
device fetches aircraft data itself, directly from the free adsb.fi feed.

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

⚠️ **If you ever need the UART/serial recovery path (bricked device, no
working web server) and the serial port disappears on your computer**: first
check the cable. The device's own USB-C cable is a real serial adapter (WCH
CH340 bridge, `idVendor 0x1A86`, `idProduct 0x7523`), no case-opening needed
— but only if the cable actually carries data. A charge-only cable, or
routing through a USB hub/dock, can leave the CH340 fully absent from your
computer's USB device list (not "no driver," just not there) even though the
device has power. Try a real USB-C-to-USB-C data cable plugged directly into
your computer, not through a dock or hub. One extra gotcha if your computer
has other WCH-brand USB hardware (some docks use WCH hub chips too, not just
WCH serial bridges): matching on vendor ID alone isn't enough to confirm
you've found the right device — check `bDeviceClass` is `255`
(vendor-specific/serial) and `idProduct` is `0x7523`, not `9` (USB hub) on
some unrelated chip that happens to share the vendor. **This isn't a
guaranteed fix** — it's the first thing to rule out, not a confirmed root
cause. If it doesn't come back after trying a direct data cable, the port
disappearing after a UART flash is a known open issue on this hardware with
no confirmed cause yet.

### 2.5 Make the device's IP address permanent (optional, recommended)

By default your router hands the device a new IP address occasionally (after
a reboot, or just from normal DHCP lease renewal), which can break your
daemon's connection to it if you've told the daemon a specific IP instead of
the device's hostname. Fix this once, permanently, at the router:

1. Find your device's MAC address: in the device's own web UI go to
   `http://<device-ip>/api/status` and look for a field like `"mac"`, or
   check the sticker/label on the device if there is one.
2. Log into your home router's admin page (commonly `http://192.168.0.1` or
   `http://192.168.1.1` — check the label on the router itself if unsure).
3. Find the **DHCP settings** section — often called "DHCP Reservation",
   "Address Reservation", "Static DHCP", or "IP-MAC Binding" depending on
   your router brand.
4. Add a reservation: paste in the device's MAC address, and either type in
   the IP it currently has or let the router keep its current one. Save.
5. Reboot the device once so it re-requests an address and gets the
   reserved one for good.

An alternative, **confirmed reliable on Linux only**: use the device's
**hostname** instead of its IP wherever you configure the daemon
(`--push-to smalltv-f661.local`, or whatever hostname your device shows in
its own web UI) — hostname resolution survives an IP change on its own, no
router step needed. This is what this project's own Linux reference
deployment actually uses. On **macOS**, this is unverified — Bonjour is
built in, but not every resolver path on every Mac reliably reaches it. On
**Windows**, `.local` names typically don't resolve at all unless Apple's
Bonjour service (ships with iTunes, or standalone "Bonjour Print Services")
is installed separately — the OS has no mDNS resolver by default. If the
hostname doesn't work for you, the DHCP reservation above is the fix that
works everywhere regardless of platform.

### 3. Set the device's modes

In the web UI: pick which modes to include in the carousel (Usage, z.ai,
Codex, Antigravity, Radar, Agenda & weather, Ticker), drag them into
whatever rotation order you want, or pin one mode permanently. This can be
done anytime from `http://<device-ip>/`, no reflash needed.

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

**Optional: Plane radar**
1. Radar's own home lat/lon (Radar tab) is optional — leave it at 0/0 and
   it automatically uses the location you set for Weather above. Only fill
   it in if you want radar centred somewhere different (e.g. a nearby
   airport instead of home).
2. No daemon flag needed — the device fetches aircraft data itself,
   directly from the free [adsb.fi](https://adsb.fi) API, no key required.

**Optional: z.ai quota**
1. Log in at [z.ai](https://z.ai), open your account/profile menu →
   **API Keys**, and copy an existing key or create one (plain static key,
   no login flow — generate it on any machine with a browser, then use it
   wherever the daemon runs)
2. Add `--zai --zai-key <your-key>` to your daemon command line (or set
   `CLAWDMETER_ZAI_KEY`) — this hits an endpoint z.ai hasn't publicly
   documented, so treat it as best-effort

**Optional: Codex CLI quota**
1. Run `codex login` on the daemon's own machine, if you haven't already —
   this rides your existing ChatGPT plan, no separate API key
2. **Headless / no browser on the daemon machine?** Run
   `codex login --device-auth` instead — it prints a URL + code to open on
   any *other* device with a browser; once you approve there, this
   machine's login completes on its own
3. Add `--codex` to your daemon command line. Free to poll — it just reads
   Codex's own already-cached rate-limit state, no model call

**Optional: Antigravity CLI quota**
1. Install `agy` on the daemon's own machine (native binary, no Node/npm):
   ```
   curl -fsSL https://antigravity.google/cli/install.sh | bash
   ```
2. Authenticate it. **Headless / no browser on the daemon machine?** `agy`'s
   login needs a real interactive terminal, but works fine over SSH given a
   real pty:
   ```
   ssh <daemon-host>
   tmux new-session -s agyauth 'agy'
   ```
   Pick **Google OAuth**, open the printed URL in any browser, sign in, then
   type the code it shows back into that same terminal (never paste it
   anywhere else — it's a real, time-limited OAuth code). Answer the two
   one-time prompts (data-sharing opt-out, trust this directory), then exit
   with `Ctrl+C` twice — the daemon starts its own `agy` process per poll,
   it doesn't need this session left running.
3. Add `--antigravity` to your daemon command line. **Unlike Codex, this
   isn't free** — every poll fires a real, cheap-model prompt, so the
   default interval is kept long (30 minutes) on purpose

See `clawdmeter-daemon`'s own `README.md` for the full detail/troubleshooting
on any of the four (Claude, z.ai, Codex, Antigravity) if something above
doesn't work.

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

**Codex CLI quota**: free to check — it just reads state Codex's own CLI
already caches locally, no model call at all.

**Antigravity CLI quota**: the opposite of Codex — its local quota data only
populates after a real prompt has run, so every poll fires one (the
cheapest available model). Small real cost, which is why the default
interval is 30 minutes rather than something short.

**z.ai quota**: a plain HTTP GET against your account's own usage endpoint —
no completion call, so effectively free like Codex.

## System overview

```
Claude Code (token via claude setup-token)  --\
Codex CLI (codex login)                      --\
Antigravity CLI (agy, real prompt per poll)   ---> clawdmeter-daemon --> GeekMagic screen
z.ai (API key)                               --/     (always running)      (smalltv-mod)
Google Calendar (OAuth, --calendar-auth)     --/
Open-Meteo (weather, no key needed)         --/
```

Plane radar is the one exception — the device fetches aircraft data itself,
directly from adsb.fi, not through the daemon.

Each data source is independent and optional — run with just `--push` for
usage only, or add `--calendar`/`--weather`/`--zai`/`--codex`/`--antigravity`
for the rest.

---

References: github.com/kittipitch/smalltv-mod · github.com/kittipitch/clawdmeter-daemon
