---
title: First-time setup
description: Join WiFi, reach the web UI, and configure what the SmallTV shows.
---

On first boot the device has no WiFi saved, so it starts its own hotspot and waits for you to point it at your network. Everything after that happens in the web UI.

## Join WiFi

1. On first boot the screen shows **SETUP MODE** and creates an open hotspot named `SmallTV-Setup`.
2. Join it from your phone or PC. A captive portal should open on its own. If it does not, browse to `http://192.168.4.1`.
3. Open the **WiFi** tab, press **Scan**, pick your 2.4 GHz network, enter the password, and press **Save and connect**. The device reboots and joins your network. You can save up to 4 networks; at boot the device joins the strongest one it can see.
4. It shows the joined network, its IP, and its `http://<hostname>.local` address on screen at boot. Browse to either one. New devices get a unique default hostname like `smalltv-3fa2` so several SmallTVs can share a network; rename it in the WiFi tab.

The ESP8266 is 2.4 GHz only. For an AP password use at least 8 characters, or leave it blank for an open hotspot.

## Add something to show

Open the **Usage** tab and point the device at the [clawdmeter-daemon](https://github.com/kittipitch/clawdmeter-daemon) running on your PC — either by setting the daemon's URL here (pull), or by leaving it blank and running the daemon with `--push-to <hostname>.local` (push). The quota pages for z.ai, Codex, Antigravity and OpenRouter come from the same daemon and appear in the carousel once it has actually pushed data for them. For an agenda and weather panel instead, fill in the **Agenda & weather** tab.

## Web UI reference

The UI is a single page served from the device. Saving applies most changes live; changing the WiFi network reboots. The header shows a chip naming the board the firmware targets (ESP8266, ESP32-C2, or ESP32).

### Status

Live device info: mode, IP, signal, free heap, uptime, and the last reset reason.

### WiFi

Scan and save up to 4 networks; the device joins the strongest visible one at boot and falls over to the others if the connection drops. Also sets the device hostname (its `.local` mDNS name) and the setup hotspot name and password.

### Display

The mode selector (Claude usage, the quota and agenda/weather pages, Plane radar, or Carousel, which rotates through the ticked features on a timer), plus brightness with optional auto-brightness, orientation, and backlight polarity.

#### Clock and night mode

The "Clock & night mode" card sets a timezone by IANA name (`Europe/Rome`, `Europe/Zurich`, `America/New_York`, and so on) from a dropdown; DST is handled automatically. Enabling the nightly window adds a From and To time (HH:MM) and a night brightness, where 0 turns the backlight fully off and any other value just dims it. Night mode is off by default. The device keeps rendering behind the dimmed or off backlight rather than sleeping, so WiFi, the web UI, and usage push stay up throughout.

Night mode only switches on once NTP has confirmed the clock within the last few minutes. If NTP can't be reached, the screen stays on and the device keeps retrying until it syncs or the window ends in the morning, so a device that never reaches NTP is never left stuck dark.

NTP is armed on every boot, whether or not night mode is enabled. It used to start only when night mode was on, to keep its heap allocation away from the ticker's TLS handshake; with the ticker gone that trade-off no longer exists, and the old behaviour had a nasty edge — a device with night mode off came back from any reboot with no clock at all, losing the usage page's clock overlay and the agenda's event times.

### Update

Check for and install the newest GitHub release (every board fetches its own image), upload a firmware file manually, export or import the full device configuration as a JSON file, reboot, or factory reset. The exported file contains the WiFi passwords in clear text, so treat it like a password.

## Modes

Each mode has its own page:

- [Claude usage meter](/smalltv-mod/features/usage/)
- [Plane radar](/smalltv-mod/features/radar/)
