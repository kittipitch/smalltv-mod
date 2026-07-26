# Making a GeekMagic Screen Show Your Claude Code Quota — Full Guide

Source (Thai original): https://share.aiceo.im/geekmagic-claude-meter/

## Goal

A tiny desk display showing your Claude Code (CC) usage in real time — the %
used of your 5-hour and 7-day limits — without opening chat to check.

## The pieces

- **GeekMagic SmallTV Ultra** — a tiny pixel-display desk gadget, ESP8266 chip
  (WiFi 2.4GHz only)
- **Stock firmware** — only does clock/weather/photo album; no Claude usage
  support (that's PRO-model-only, for crypto/stocks)
- **smalltv-mod** — alternative community firmware (GitHub: giovi321) that adds
  a "Claude usage" mode
- **clawdmeter-daemon** — a small program that runs on your Mac, pulls your
  Claude quota, and pushes it over WiFi to the device

Repos:
- https://github.com/giovi321/smalltv-mod
- https://github.com/giovi321/clawdmeter-daemon

## Steps

### 1. Connect the device to your home WiFi (first time)

1. Power it on — the device broadcasts a hotspot named `GIFTV`
2. Connect your Mac to WiFi `GIFTV` (ignore "no internet" warnings)
3. Open `http://192.168.4.1`
4. Network → Scan → pick your home WiFi (**2.4GHz only**) → enter password → Save

### 2. Flash the smalltv-mod firmware (2 steps for the Ultra model)

Download from `smalltv-mod`'s GitHub Releases:
- `smalltv-mod-loader.bin`
- `smalltv-mod-firmware.bin`

**Step 1**: go to the device's existing settings page (its LAN IP) → Firmware
Upgrade → upload `loader.bin` → device restarts, broadcasts a new open hotspot
`SmallTV-Loader` (no password)

**Step 2**: connect to WiFi `SmallTV-Loader` → `192.168.4.1` → upload the full
`firmware.bin` → restart → now running the mod firmware

⚠️ **macOS auto-switches WiFi away mid-upload.** During the large-file upload
(~680KB) macOS automatically switches back to your home WiFi (it considers a
no-internet network "worse" than one with internet) — fix: combine "join the
device's WiFi" and "upload the file" into a single uninterrupted step with
automatic retry (3 rounds got it through)

Note: the ESP8266 chip remembers previously-joined WiFi credentials at the chip
level (separate from the firmware) — after a firmware change it may auto-join
your home WiFi again without you re-entering anything

⚖️ **Trade-off to know before deciding**: the mod firmware is unofficial, and
flashing it removes all original modes (clock/weather/album), leaving only 3
new modes: stock/crypto ticker, Claude usage, plane radar — confirm with
whoever owns the device before doing this, since it's an all-or-nothing swap.

### 3. Set the display mode + install the Mac-side sender

The new firmware has a web config page and an API (a channel for
program-to-program communication, no browser clicks needed) for direct
commands — set the device's mode to "Claude usage" via that API in one shot.

The screen alone has nothing to show yet — you need a Mac-side program
constantly pulling your Claude quota and sending it to the screen. That's
`clawdmeter-daemon`:

1. Download it from GitHub: `giovi321/clawdmeter-daemon`
2. Install (needs a newer Python than what ships with macOS — pull a newer one
   via Homebrew)
3. It reads your Claude Code token automatically from the macOS Keychain (the
   built-in macOS password vault) — no new token or signup needed
4. Set it to auto-start every login via macOS's LaunchAgent mechanism (the
   standard way to run background programs at boot)
5. Set the delivery mode to "push" — the Mac finds the device itself (via
   mDNS — how devices on the same LAN announce themselves to each other
   without knowing IPs ahead of time) and pushes data to it

🔑 **The most convenient part**: no need to run `claude setup-token` or request
a separate token at all — the daemon reads straight from Keychain, since
you're already logged into Claude Code on this machine.

## How much power / quota does this cost?

**Power**: the GeekMagic device itself barely draws anything (ESP8266 + small
screen, roughly nightlight-level). The real power draw is the **Mac**, since
the daemon must keep running on it to feed the screen — if the Mac sleeps, this
stops (the screen just freezes on the last number until the Mac wakes).

- 0.5–2W — what the screen itself draws (nightlight-level)
- Mac — the actual power variable; must not be allowed to sleep
- 5 minutes — quota-check frequency (tuned down from the default 60s)

**Claude Code quota**: a fact most people miss — checking your quota costs a
tiny sliver of quota itself, because the daemon has to fire a real API request
to Claude to read the "remaining quota" values off the response headers —
there's no way to read this without a real request. It uses the same account
token you already use for work, not a separate key, so there's no extra dollar
cost, but it does quietly eat a little of your time-based quota (5h/7d) too.

Found in the actual code:
- `model: claude-haiku-4-5` (the cheapest one)
- `max_tokens: 1`
- `message: "hi"`
- Default frequency: every 60 seconds → ~1,440 checks/day

✅ **Tuned**: changed frequency from every 60 seconds → every 5 minutes
(cuts self-consumption ~5x), trading slightly slower on-screen updates —
adjust up or down anytime by changing `--interval` and restarting the daemon.

## Summary — system overview + ready-to-use prompt

Every piece connects into one loop — Claude Code on the Mac already has a
login token stored in Keychain, the daemon reads that token to ask how much
quota is left, finds the screen itself over WiFi, and sends the number to it.

```
Claude Code (token in Keychain)
  -> clawdmeter-daemon (always running in background)
  -> GeekMagic screen (smalltv-mod)
```

- 14% / 18% — quota at time of testing (5h / 7d)
- 20s — data-push frequency to the screen
- 0 — times you had to open a Terminal yourself

## Ready-to-use prompt (paste into Claude Code chat)

Instead of following the guide step by step, paste this straight into a Claude
Code chat:

```
I already have a GeekMagic SmallTV Ultra connected to my home WiFi.
Please make this screen show my Claude Code usage quota (5h / 7d) in real
time by:

1. Finding the screen's IP on the LAN, then flashing the community firmware
from https://github.com/giovi321/smalltv-mod over the existing one
(the Ultra model needs a 2-step flash: loader.bin first, then the full
firmware.bin, via the device's Firmware Upgrade page)
2. Setting the screen's mode to "Claude usage" via the new firmware's API
3. Installing https://github.com/giovi321/clawdmeter-daemon on this Mac
- have it read the Claude Code token from macOS Keychain automatically
  (don't request a new token)
- set it to auto-run every time I log into this machine (LaunchAgent)
- use push mode (auto-discover the screen via mDNS, no need to set an IP)
- check quota every 5 minutes (not the 60s default, so it doesn't eat quota
  too aggressively itself)

Explain every step in plain language before doing it, and if any step needs
my confirmation (like flashing over the existing firmware), ask first.
```

Using AI as the team for the whole job — from the hardware connection to
flashing firmware — without ever opening a Terminal yourself, just saying it
in words.

---

References: github.com/giovi321/smalltv-mod · github.com/giovi321/clawdmeter-daemon
