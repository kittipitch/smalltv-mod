---
title: Daemon setup, start to finish
description: The whole path from a flashed SmallTV to numbers on screen, command by command, with the output each step should print and what to do when it does not.
---

Every command here is meant to be pasted. After each one you get the output it should
print, so you can tell at a glance whether to continue or stop. Total time: 10–15 minutes
if `claude` already works on this machine; installing the developer tools first takes
longer.

Pick your track: [macOS](#macos-start-to-finish) · [Windows](#windows-start-to-finish) ·
[Linux desktop or Raspberry Pi](#linux-or-raspberry-pi-start-to-finish).

![Your computer asks Anthropic, then pushes two percentages to the SmallTV over your WiFi](/smalltv-mod/assets/daemon-dataflow.svg)

## Before you start: three facts and one name

1. **The device cannot fetch its own usage.** A daemon on your computer does that. This
   guide has it **push** to the device, which is the simple direction; the firmware can
   also **pull** from a daemon URL you set on its Usage tab. If that program stops, the screen keeps showing the last
   reading — after about 4 minutes 20 seconds it dims the `CLAUDE` heading to mark it
   stale, rather than throwing the numbers away.
2. **You need a Claude subscription** (Pro, Max or Team). `claude setup-token` does not
   work on a free account, and API-console billing is a different thing that will not do.
3. **The device must already be on a 2.4 GHz network your computer can reach**
   ([first-time setup](/smalltv-mod/getting-started/setup/)). Two situations:
   - **At home**, with a 2.4 GHz SSID on the router: nothing extra to do.
   - **On the go** — hotel, workshop, a friend's flat — or a router that is 5 GHz only,
     band-steered, or isolates clients from each other: your laptop has to become the
     network first. Do [Sharing your laptop's WiFi](/smalltv-mod/getting-started/sharing-wifi/)
     before anything below. On macOS that also means an uplink other than WiFi (iPhone
     over USB, or Ethernet), because macOS will not share WiFi over WiFi.

**Get the device's name now — you need it in almost every command.** Unplug and replug the
device: for about 4 seconds after power-on the screen shows its IP and
`http://smalltv-XXXX.local`. That `smalltv-XXXX` is the name — it is also under **WiFi tab
→ Device name** card, field **Hostname**, once you can reach the web UI. It reads
something like `smalltv-3fa2`, and the address you use is that plus `.local`:

```sh
curl -sS http://smalltv-3fa2.local/api/status      # macOS and Linux
```

```powershell
curl.exe -sS http://smalltv-3fa2.local/api/status  # Windows PowerShell -- curl alone is an alias
```

One line of JSON comes back. Values differ per device; what matters is that it contains
`"fw":"smalltv-mod"`, `"mode":"sta"`, `"connected":true`, and an `ip` that matches your
network.

If that prints JSON, mDNS works here and you can use the name everywhere. If it prints
`curl: (6) Could not resolve host`, the name is not resolving on this machine: use the
`ip` from the device's boot screen or your router instead, and give the device a DHCP
reservation so it stops moving. On Linux or a Raspberry Pi, install the avahi packages
from step 1 of the Linux track first — a bare Debian cannot resolve `.local` at all.

`"fw":"smalltv-mod"` matters: on upstream `giovi321` firmware only the usage page exists,
and calendar/weather stay blank forever.

## macOS, start to finish

### 1. Python 3.10 or newer

```sh
python3 --version
```

```text
Python 3.12.7
```

Example — any 3.10 or newer is fine. On a Mac that has never had developer tools this
opens a dialog offering to install the Command Line Tools: accept it, wait, then run the
command again. `3.9.x`, or still nothing? Install Homebrew from [brew.sh](https://brew.sh), then
`brew install python`, then check again. Stock macOS Python is 3.9 and the daemon will not
run on it.

### 2. Get the code

```sh
git clone https://github.com/kittipitch/clawdmeter-daemon
cd clawdmeter-daemon
```

### 3. Claude login

Two parts: the login, and the push target.

**Everyone** does this, so autostart knows where to push:

```sh
cp .env.example .env
```

and sets one line in it:

```ini
CLAWDMETER_PUSH_URL=smalltv-3fa2.local
```

**The login:** if you already use `claude` on this Mac, you are done — the daemon reads
that login from the Keychain. Otherwise:

First check the tools it needs: `npm --version` and `git --version`. Missing? Install
Node.js LTS from [nodejs.org](https://nodejs.org) (or `brew install node`); `git` prompts
to install the Command Line Tools the first time you run it.

Install Claude Code, then log in once and quit with `/exit`:

```sh
npm install -g @anthropic-ai/claude-code
claude
```

For a Mac that will never run `claude` interactively, mint a long-lived token instead. It
is **printed**, not stored:

```sh
claude setup-token
```

Add the token to the `.env` you already made, so it holds both lines — type or paste it
straight in, never through a chat window:

```ini
CLAUDE_CODE_OAUTH_TOKEN=sk-ant-oat01-...
CLAWDMETER_PUSH_URL=smalltv-3fa2.local
```

### 4. Install

`.env` is read once at startup and the installer starts the daemon immediately, so write
`.env` **before** this:

```sh
chmod +x install.sh
./install.sh
```

Among the pip output, a first install prints these lines:

```text
Creating a virtualenv (.venv)...
Installing Python dependencies into .venv ...
Registering login autostart (tray)...
Autostart on: wrote and loaded LaunchAgent /Users/you/Library/LaunchAgents/com.giovi321.clawdmeter.plist
Installation complete. The tray daemon starts at your next login.
```

That last sentence is generic: on macOS `wrote and loaded LaunchAgent` is the truth — it
has already started, and a mascot appears in the menu bar within a few seconds. If you instead see
`Python 3.10+ required`, redo step 1 and run
`PYTHON=$(brew --prefix)/bin/python3 ./install.sh`.

### 5. Watch it work, once, in the foreground

```sh
.venv/bin/python clawdmeter_daemon.py --no-tray --push-to smalltv-3fa2.local
```

Example output — timestamps, values and line order vary:

```text
[2026-09-12 19:04:11] Polling Claude every 60s
[2026-09-12 19:04:11] mDNS discovery on - browsing for SmallTV devices
[2026-09-12 19:04:11] HTTP push every 20s (static targets + mDNS discovery)
[2026-09-12 19:04:11] clawdmeter-daemon: transport = push  (switch from the tray menu)
[2026-09-12 19:04:13] 5h=12% sr=143 7d=4% wr=9876 st=allowed
[2026-09-12 19:04:31] Pushing to http://smalltv-3fa2.local/api/usage OK
```

**Look at the device**: within ~30 s the mascot is replaced by two percentage bars. Then
Ctrl-C — the LaunchAgent copy keeps running.

| What you see instead | Cause | Fix |
|---|---|---|
| `ModuleNotFoundError: No module named 'httpx'` | wrong interpreter | use `.venv/bin/python`, not `python3` |
| `TypeError: unsupported operand type(s) for \|` | venv built with 3.9 | `rm -rf .venv`, then `PYTHON=$(brew --prefix)/bin/python3 ./install.sh` |
| `Polling Claude every 60s` then silence — or on macOS `Keychain read failed: … exit status 44` after every poll | no usable token; `claude` was never logged in on this Mac | step 3 |
| `Push http://…/api/usage failed: …` every 20 s | the name does not resolve, or the device is unreachable | re-check the `curl` at the top; try the raw IP |
| `5h=` lines and no `Push` line at all (nor `HTTP push every 20s`) | it started in **serve** mode — no target was given | pass `--push-to`; see the sticky-transport caution in [Keep the daemon running](/smalltv-mod/getting-started/keep-it-running/) |
| `Push http://smalltv-3fa2.local/api/usage HTTP 403` | **Update → Daemon source IP** on the device names a different machine | clear that field, or set it to this computer's LAN IP |
| Everything fine, device unchanged | Carousel is showing another page, Claude usage is unticked in the carousel list, or this is a different device | wait for it to rotate in, tick it, or pin **Display → Mode → Claude usage**; re-check the hostname |

### 6. Confirm it survives a reboot

```sh
launchctl list | grep clawdmeter
```

```text
12345	0	com.giovi321.clawdmeter
```

Example. What matters: a number in the first column (the PID) and `0` in the second
(last exit status), against the label `com.giovi321.clawdmeter`. A `-` in the first column
means it died at startup — and an import error dies *before* logging, so
`~/.clawdmeter-daemon.log` will be empty. The LaunchAgent's own stderr is where that
lands: `tail -20 ~/Library/Logs/clawdmeter.err.log`. Its stdout,
`~/Library/Logs/clawdmeter.out.log`, is also the only way to tell the LaunchAgent copy
from a foreground one, since both append to the same `~/.clawdmeter-daemon.log`. Details, and the headless case:
[Keep the daemon running](/smalltv-mod/getting-started/keep-it-running/).

## Windows, start to finish

### 1. Python 3.10+

Install from [python.org](https://www.python.org/downloads/), ticking **Add python.exe to
PATH**. Then in a **new** PowerShell:

```powershell
python --version
```

```text
Python 3.12.7
```

If the Microsoft Store opens instead, that is the alias stub: turn off the `python.exe`
and `python3.exe` toggles under **Settings → Apps → Advanced app settings → App execution
aliases**, then re-check.

### 2. Get the code, in PowerShell

Install **Git for Windows** and **Node.js LTS** first, then open a new PowerShell and
check `git --version` and `npm --version` both answer.

Shift+right-click the folder → **Open PowerShell window here**. Run the `.bat` files from
there rather than double-clicking, so the window stays open and you can read errors. Only
`start-daemon.bat` accepts daemon flags.

```powershell
git clone https://github.com/kittipitch/clawdmeter-daemon
cd clawdmeter-daemon
```

### 3. Claude login

**Everyone** does this first, so autostart knows where to push:

```powershell
Copy-Item .env.example .env
```

then set `CLAWDMETER_PUSH_URL=smalltv-3fa2.local` in it.

**The login:** already use `claude` here? Nothing more to do. Otherwise
`npm install -g @anthropic-ai/claude-code`, run `claude` once and log in. For an
unattended PC, `claude setup-token` prints a token — add it to that same `.env`:

```ini
CLAUDE_CODE_OAUTH_TOKEN=sk-ant-oat01-...
CLAWDMETER_PUSH_URL=smalltv-3fa2.local
```

### 4. Install

```powershell
.\install.bat
```

First-install example; your paths differ:

```text
Creating virtualenv .venv ...
Installing Python dependencies into .venv ...
Verifying the tray dependencies ...
Registering login autostart (tray) ...
Autostart on: HKCU\...\Run\clawdmeter = "C:\path\to\clawdmeter-daemon\.venv\Scripts\pythonw.exe" "C:\path\to\clawdmeter-daemon\clawdmeter_daemon.py" --tray
Installation complete. The tray daemon starts automatically at next login.
```

`Python 3.10+ required` here means step 1 picked up the wrong interpreter. And if a later
foreground run still raises `TypeError: unsupported operand type(s) for |`, an old venv is
being reused: `Rename-Item .venv .venv-old`, then `.\install.bat` again.

### 5. Watch it work

```powershell
.venv\Scripts\python clawdmeter_daemon.py --no-tray --push-to smalltv-3fa2.local
```

Same five lines as macOS above, same 30-second mascot-to-bars change on the device.
Ctrl-C when satisfied, then `.\start-daemon.bat` for the tray version.

### 6. Confirm

Task Manager → **Startup apps** lists *clawdmeter*. The tray icon starts in the
`⌃` overflow — drag it onto the taskbar. Nothing there? Open
`%USERPROFILE%\.clawdmeter-daemon.log` (paste that into Explorer's address bar).

## Linux or Raspberry Pi, start to finish

### 1. Packages

```sh
sudo apt update && sudo apt install python3-venv git avahi-daemon avahi-utils libnss-mdns
python3 --version
```

`python3 --version` must print 3.10 or newer. On an older Raspberry Pi OS or Debian that
still ships 3.9, either upgrade the release or install a newer Python plus its `venv`
package and run `PYTHON=/path/to/python3.12 ./install.sh`. The avahi packages are what make
`smalltv-XXXX.local` resolve; skip them and you must use the raw IP.

### 2. Code and config

```sh
git clone https://github.com/kittipitch/clawdmeter-daemon
cd clawdmeter-daemon
cp .env.example .env
```

Put the push target in `.env` — and on a headless box the token too, since there is no
tray icon to tell you it is missing. Get a token on any machine where Claude Code is
installed and you can sign in (`claude setup-token` prints one and saves nothing), then
copy it here:

```ini
CLAWDMETER_PUSH_URL=smalltv-3fa2.local
CLAUDE_CODE_OAUTH_TOKEN=sk-ant-oat01-...
```

```sh
chmod 600 .env
```

### 3. Install

```sh
chmod +x install.sh && ./install.sh
```

On a desktop this also writes `~/.config/autostart/clawdmeter-daemon.desktop`, which fires
at your next **graphical** login. On a headless box that entry never runs — use the
systemd unit in [Keep the daemon running](/smalltv-mod/getting-started/keep-it-running/#linux-headless--a-raspberry-pi-a-server-a-nas),
and do not forget `loginctl enable-linger "$(whoami)"`.

### 4. Watch it work

```sh
.venv/bin/python clawdmeter_daemon.py --no-tray --push-to smalltv-3fa2.local
```

Same lines, same 30-second change on the device. Press Ctrl-C when satisfied, then
`./start-daemon.sh` to keep it running in this session — the XDG entry only takes over at
your next graphical login. `.local` names need `avahi-daemon` running:

```sh
systemctl is-active avahi-daemon        # want: active
avahi-resolve -n smalltv-3fa2.local     # want: the name and an IP
```

## Now add the optional pages

Each is independent, off by default, and remembered once enabled — turn one off with its
`--no-…` flag, not by omitting it.

Google Calendar needs a service-account key and a shared calendar first; Weather needs
lat/lon saved on the device's **Agenda & weather** tab; Codex needs the `codex` CLI logged
signed in with your ChatGPT plan (`codex login`, not an API key — no extra cost). Run the
one you want once, on macOS or Linux:

```sh
.venv/bin/python clawdmeter_daemon.py --calendar --calendar-id you@gmail.com --push-to smalltv-3fa2.local
.venv/bin/python clawdmeter_daemon.py --weather --push-to smalltv-3fa2.local
.venv/bin/python clawdmeter_daemon.py --codex --push-to smalltv-3fa2.local
```

or on Windows:

```powershell
.venv\Scripts\python clawdmeter_daemon.py --calendar --calendar-id you@gmail.com --push-to smalltv-3fa2.local
.venv\Scripts\python clawdmeter_daemon.py --weather --push-to smalltv-3fa2.local
.venv\Scripts\python clawdmeter_daemon.py --codex --push-to smalltv-3fa2.local
```

That one run saves the feature. **A daemon already running does not pick it up** — restart
the long-lived copy afterwards: macOS
`launchctl kickstart -k gui/$(id -u)/com.giovi321.clawdmeter`; Windows quit the tray icon
and run `.\start-daemon.bat`; Linux desktop `./start-daemon.sh`; headless
`systemctl --user restart clawdmeter`.

Full walkthroughs: [Google Calendar](/smalltv-mod/getting-started/google-calendar/) and
[what the daemon needs installed](/smalltv-mod/getting-started/daemon-requirements/).
`--push` discovery alone feeds the **usage** page only — every optional page needs an
explicit `--push-to`.
