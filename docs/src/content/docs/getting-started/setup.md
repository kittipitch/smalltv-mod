---
title: First-time setup
description: Join WiFi, reach the web UI, and configure what the SmallTV shows.
---

On first boot the device has no WiFi saved, so it starts its own hotspot and waits for you to point it at your network. Everything after that happens in the web UI.

## Join WiFi

1. On first boot the screen shows **SETUP MODE** and creates an open hotspot named `SmallTV-Setup`.
2. Join it from your phone or PC. A captive portal should open on its own. If it does not, browse to `http://192.168.4.1`.
3. Open the **WiFi** tab, press **Scan**, pick your 2.4 GHz network, enter the password, and press **Save and connect**. The device reboots and joins your network. You can save up to 4 networks; at boot the device tries the visible ones **in the order
you saved them**, so put the network you want first, then the ones the scan cannot see.
4. It shows the joined network, its IP, MAC and `http://<hostname>.local` on screen for
about **4 seconds** right after power-on — unplug and replug to see it again. After that
the hostname lives on the **WiFi tab → Device name**. Browse to either one. New devices get a unique default hostname like `smalltv-3fa2` so several SmallTVs can share a network; rename it in the WiFi tab.

The ESP8266 is 2.4 GHz only — see [Sharing your laptop's WiFi](/smalltv-mod/getting-started/sharing-wifi/) if you have no 2.4 GHz network. If you later set a password on the device's **own** setup hotspot (WiFi tab), use at least 8 characters, or leave it blank to keep that hotspot open.

### If the setup page won't finish loading

On some computers the setup page only half-loads over the `SmallTV-Setup`
hotspot: it stays on **Loading...**, the tabs don't respond, and a **Radar** tab
you didn't expect is still visible (the page hides it only once it has fully
loaded). The device is fine. Try the page from a phone first. If that is not an
option, save the network straight from a terminal while still joined to
`SmallTV-Setup`:

```bash
curl -s -m 20 -H 'Content-Type: application/json' \
  -d '{"wifi":[{"ssid":"YOUR_SSID","pass":"YOUR_PASSWORD"}]}' \
  http://192.168.4.1/api/config
```

A reply of `{"ok":true,"reboot":true}` means it saved; the device reboots and
joins that network. Then continue at step 4 above.

- Keep the single quotes. A password containing `'` breaks them; use the phone
  route for that one.
- On Windows use Git Bash or WSL, or the phone route; PowerShell mangles the
  quotes.
- This replaces the whole saved-network list. On a fresh device that list is
  empty, so nothing is lost; on a configured one, include every network you want
  to keep, in priority order.

## Add something to show

For the Claude usage meter you also need the daemon on your PC. The command-by-command
path with expected output at every step is
[Daemon setup, start to finish](/smalltv-mod/getting-started/first-setup/). Reference
pages:
[What the daemon needs installed](/smalltv-mod/getting-started/daemon-requirements/),
then the daemon's
[Windows/macOS beginner guide](https://github.com/kittipitch/clawdmeter-daemon/blob/main/GUIDE-WINDOWS-MAC.md)
(it has a Linux section too).


Open the **Usage** tab and point the device at the [clawdmeter-daemon](https://github.com/kittipitch/clawdmeter-daemon) running on your PC — either by setting the daemon's URL here (pull), or by leaving it blank and running the daemon with `--push-to <hostname>.local` (push). The quota pages for z.ai, Codex, Antigravity and OpenRouter come from the same daemon and appear in the carousel once it has actually pushed data for them. For an agenda and weather panel instead, fill in the **Agenda & weather** tab.

## Web UI reference

The UI is a single page served from the device. Saving applies most changes live; changing the WiFi network reboots. The header shows a chip naming the board the firmware targets (ESP8266, ESP32-C2, or ESP32).

### Status

Live device info: mode, IP, signal, free heap, uptime, and the last reset reason.

### WiFi

Scan and save up to 4 networks; the device tries the saved networks in list order, visible ones first and falls over to the others if the connection drops. Also sets the device hostname (its `.local` mDNS name) and the setup hotspot name and password.

### Display

The mode selector (Claude usage, the quota and agenda/weather pages, Pictures, Plane radar, or Carousel, which rotates through the ticked features on a timer), plus brightness with optional auto-brightness, orientation, and backlight polarity.

When a mode or the Carousel rotates — and at what interval — is one shared
setting: the **rotation interval** field in the Display tab (labelled "Each
page shows for (s)" or "Each picture shows for (s)" depending on the mode).
It drives the Carousel's per-page dwell, the per-photo dwell in Pictures mode,
and the page 1↔2 flip in Next event mode when page 2 is ticked and there are
more than 3 events (standalone Next event used to show only page 1; page 2,
events 4–6, was carousel-only before). Pages that are single and static —
usage, weather, forecast — don't use it, so the field stays hidden there.

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
