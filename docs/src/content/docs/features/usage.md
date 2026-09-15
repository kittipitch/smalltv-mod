---
title: Claude usage meter
description: Show your Claude 5-hour and 7-day usage with an animated mascot, fed over WiFi from your PC.
---

Switch **Display → Mode** to **Claude usage** and the device shows your Claude consumption. It is the idea of a desk usage meter, on the SmallTV, over WiFi. The device's USB is power only, so nothing is wired between it and your PC.

![Data flow: your computer asks Anthropic over HTTPS, then pushes two percentages to the SmallTV over your home WiFi every 20 seconds; the device never reaches the internet itself](/smalltv-mod/assets/daemon-dataflow.svg)

## What it shows

![Three screen states: mascot before any data, two percentage bars while pushes arrive, and the same bars with a dimmed CLAUDE heading once the feed goes quiet](/smalltv-mod/assets/usage-screen-states.svg)

The screen has three states, and knowing them saves a lot of guessing during setup.

- **Animated mascot** — no usage data has arrived since the device booted. A fresh
  device sits here until the daemon's first push. Its mood, from calm to working to
  dancing, reflects how fast your session is burning.
- **Stats** — data is flowing: your 5-hour and 7-day utilization as big percentages with
  fill bars that shade green to amber to red as you approach the cap, plus reset
  countdowns.
- **Stale** — data arrived once and then stopped (PC asleep, daemon stopped, network
  dropped). The numbers stay on screen because they are the last real reading; the
  `CLAUDE` heading dims to mark them old, and the reset countdowns keep ageing. The
  threshold is twice the **Refresh data (s)** value on the device's Usage tab plus 20 s —
  about **4 minutes 20 seconds** at the default 120 s. The heading brightens again on the
  next push.

## Setup

The PC-side daemon is a separate project, [clawdmeter-daemon](https://github.com/kittipitch/clawdmeter-daemon). It reads the OAuth token Claude Code already stored and sends your usage to the device. The token never leaves your machine; the device only ever receives a few percentages over your LAN.

1. On the PC that runs Claude Code:

   ```sh
   git clone https://github.com/kittipitch/clawdmeter-daemon
   cd clawdmeter-daemon
   ./install.sh                 # Windows: install.bat -- makes .venv, installs deps, autostart

   # push to the SmallTV (the normal choice, works even behind client isolation):
   .venv/bin/python clawdmeter_daemon.py --push-to smalltv-XXXX.local
   # Windows: .venv\Scripts\python clawdmeter_daemon.py --push-to smalltv-XXXX.local
   ```

   Use the venv's interpreter: a bare `python` has none of the dependencies. Within about
   30 seconds the mascot is replaced by two bars. It runs with a tray/menu-bar icon on
   Windows, macOS and Linux, and can start at login. Full walkthrough:
   [what the daemon needs installed](/smalltv-mod/getting-started/daemon-requirements/)
   and the [clawdmeter-daemon README](https://github.com/kittipitch/clawdmeter-daemon).

2. In the web UI open **Display → Mode → Claude usage** (a fresh device boots into
   Carousel, so pin the mode while testing). Then on the **Usage** tab leave **Usage
   daemon URL** blank for push — set it to `http://<that-pc-ip>:8787/` only if you chose
   serve-and-pull instead. Save.

The mascot animations are a curated subset of the [claudepix](https://claudepix.vercel.app) pixel-art set, re-rendered on the ST7789.

### Multiple devices (auto-discovery)

Each device advertises a `_clawdmeter._tcp` mDNS service. The daemon can use this to discover every SmallTV on the LAN and push to all of them at once, no per-device address needed:

```sh
python clawdmeter_daemon.py --push
```

IMPORTANT: mDNS is link-local and does not cross routers, subnets, or VLANs. If the daemon PC and the devices sit on different subnets, do one of:

- run the daemon on the devices' subnet
- enable an mDNS reflector between the subnets
- list the device IPs explicitly instead of relying on discovery, with repeatable or comma-separated `--push-to`, or via the tray icon's **Configure push targets** dialog
