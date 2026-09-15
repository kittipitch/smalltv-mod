---
title: Keep the daemon running (autostart and services)
description: Make clawdmeter-daemon start by itself on Windows, macOS, a Linux desktop and a headless Pi — and check that it really did.
---

The device only has current numbers while the daemon is running. Close the laptop, quit
the program, reboot — after about 4 minutes 20 seconds the usage page keeps the last
reading but dims its `CLAUDE` heading to mark it stale. That is not a device fault; the
feed has gone quiet.

So the last setup step is making it start by itself.

## One command, every OS

```sh
.venv/bin/python clawdmeter_daemon.py --install            # register autostart (on macOS this also STARTS it now)
.venv/bin/python clawdmeter_daemon.py --autostart-status   # what is registered
.venv/bin/python clawdmeter_daemon.py --uninstall          # unregister autostart; does NOT stop a running daemon
```

Windows: `.venv\Scripts\python` instead. The `install.sh` / `install.bat` wrappers do
this for you as their last step, so if you used one, autostart is already registered.

It registers the interpreter you ran it with — that is why using the `.venv` one matters.

:::caution[`--install` remembers no flags — and a remembered transport is sticky]
The autostart entry runs plain `--tray`; the transport comes from
`~/.clawdmeter-daemon.json` first, and only then from the environment. So:

- **Fresh install:** put `CLAWDMETER_PUSH_URL=smalltv-XXXX.local` in `.env` *before* the
  first start.
- **Already started once with no target?** It saved `serve`, and adding the variable
  afterwards will not override that. Run it once explicitly —
  `.venv/bin/python clawdmeter_daemon.py --push-to smalltv-XXXX.local` — which selects and
  remembers push; stop that process, then restart the autostart copy (per-OS restart
  commands are in each section below).

Otherwise the daemon serves on `:8787` at every login and the device never leaves the
mascot.
:::

| OS | Mechanism | Where it lives |
|---|---|---|
| Windows | `HKCU\…\Run` value, windowless `pythonw` | Task Manager → Startup |
| macOS | LaunchAgent | `~/Library/LaunchAgents/com.giovi321.clawdmeter.plist` |
| Linux, desktop | XDG autostart entry | `~/.config/autostart/clawdmeter-daemon.desktop` |
| Linux, headless | `systemd --user` unit (write it yourself, below) | `~/.config/systemd/user/clawdmeter.service` |

## Windows

`install.bat` registers it. Check **Task Manager → Startup apps** for *clawdmeter*, then
sign out and back in: the mascot should appear in the tray (look in the `⌃` overflow and
drag it onto the taskbar to pin it).

If nothing appears, read `%USERPROFILE%\.clawdmeter-daemon.log` — paste that path into
Explorer's address bar. Then try `.\start-daemon.bat` from PowerShell. If the environment
itself is broken (a Microsoft Store Python is the usual culprit), keep the old one and
rebuild: `Rename-Item .venv .venv-old`, `.\install.bat`, then sign out and back in.

`CLAWDMETER_PYTHONW` only affects a manual `start-daemon.bat` launch — the registered
autostart command does not read it.

Remove it with `uninstall.bat`.

## macOS

`./install.sh` writes the LaunchAgent **and starts it immediately** — no need to also run
`start-daemon.sh`. Confirm:

```sh
launchctl list | grep clawdmeter        # a PID and exit code 0
```

Because it is already running, a `.env` you edit afterwards is not picked up: **Quit**
from the menu-bar icon and run `./start-daemon.sh`, or
`launchctl kickstart -k gui/$(id -u)/com.giovi321.clawdmeter`.

That restart picks up a new **token**. A new **push target** does not work that way once
a transport has been saved — see the caution above; you need one explicit `--push-to`
run.

The generated LaunchAgent has no `PATH`, which is fine for Claude usage, Weather,
Calendar, z.ai and OpenRouter — they are HTTP only. Features that shell out to another
program (`codex`, `agy` for Antigravity, `trans` for non-English calendar titles) need a
hand-written plist with a `PATH`; there is a complete one in the daemon's
[AUTHENTICATION.md](https://github.com/kittipitch/clawdmeter-daemon/blob/main/AUTHENTICATION.md).

Remove it with `./uninstall.sh`.

## Linux with a desktop

`./install.sh` writes an XDG autostart entry, which fires at your next **graphical**
login — not on reboot into a console. The tray icon needs the AppIndicator + GTK packages
the installer names for your distro, plus the *AppIndicator and KStatusNotifier Support*
extension on GNOME/Wayland. Without them the daemon still runs, just with no icon.

## Linux headless — a Raspberry Pi, a server, a NAS

The autostart entry above does nothing without a desktop session. Use a `systemd --user`
service instead: it starts at boot with nobody logged in, restarts itself if it dies, and
logs to both the journal and `~/.clawdmeter-daemon.log`.

This assumes the clone is at `~/clawdmeter-daemon`; if not, replace every
`%h/clawdmeter-daemon`. The daemon reads the `.env` beside its own script wherever it is
started from, so the unit carries no target and no secret:

```sh
mkdir -p ~/.config/systemd/user
cat > ~/.config/systemd/user/clawdmeter.service <<'EOF'
[Unit]
Description=clawdmeter-daemon (Claude usage -> SmallTV)

[Service]
WorkingDirectory=%h/clawdmeter-daemon
ExecStart=%h/clawdmeter-daemon/.venv/bin/python %h/clawdmeter-daemon/clawdmeter_daemon.py --no-discover --no-tray
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable --now clawdmeter
loginctl enable-linger "$(whoami)"
```

If `loginctl` refuses over SSH, use `sudo loginctl enable-linger "$(whoami)"` — the
service still runs as your own user.

Four things that decide whether this survives:

- **`loginctl enable-linger`** — without it the service dies when your SSH session ends,
  and never starts at boot. This is the single most common headless mistake.
- **`--no-tray`** — there is no tray to draw, and no way to see a red icon, so check the
  log instead (below).
- **Keep secrets out of the unit file.** The `.env` in the working directory is enough.
  If you prefer them elsewhere, put them in `~/.config/clawdmeter/token.env` (`chmod 600`)
  and add `EnvironmentFile=%h/.config/clawdmeter/token.env` under `[Service]` — the unit
  fails to start if you name a file that does not exist.
- **`systemd --user` runs with a minimal `PATH`** that excludes `~/.local/bin` and npm's
  global directory. If you enable `--codex`, `--antigravity` or non-English calendar
  titles, run `command -v codex agy lsof trans`, then list those directories explicitly —
  literal paths, no `~` or `$HOME`:
  `Environment="PATH=%h/.local/bin:%h/.npm-global/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"`.
  A missing tool does say so in the journal, so check there before guessing.

Check it:

```sh
systemctl --user status clawdmeter
journalctl --user -u clawdmeter -n 100
```

Two patterns matter (the journal adds its own prefix to each line). A
`5h=<percent>% sr=<minutes> 7d=<percent>% wr=<minutes> st=<status>` line **repeating every
60 s**, and a single `Pushing to http://<device>/api/usage OK` near the start — that one
is printed once when the device first answers, and again after an outage, **not** on every
push. So do not sit on `-f` waiting for it to repeat. A `Push … failed:` or
`Push … HTTP 403` line every 20 s is the failure case.

Remove it again — `--uninstall` does not know about this unit, and killing the process
just makes systemd restart it:

```sh
systemctl --user disable --now clawdmeter
mv ~/.config/systemd/user/clawdmeter.service ~/.config/systemd/user/clawdmeter.service.off
systemctl --user daemon-reload
```

Leave lingering on if any other user service needs to run while you are logged out.

## Did it actually work?

Reboot, wait a minute, and look at the device: two percentage bars with a bright `CLAUDE`
heading mean yes. If the heading is dim, or the mascot is still up:

| Symptom | Check |
|---|---|
| Nothing registered | macOS/Linux desktop: `.venv/bin/python clawdmeter_daemon.py --autostart-status`; Windows: `.venv\Scripts\python clawdmeter_daemon.py --autostart-status`; headless Linux, which that flag does not inspect: `systemctl --user is-enabled clawdmeter` and `systemctl --user is-active clawdmeter` |
| Registered but not running | The log: `~/.clawdmeter-daemon.log`, `%USERPROFILE%\.clawdmeter-daemon.log`, or `journalctl --user -u clawdmeter`. **macOS:** an import error dies before logging — read `~/Library/Logs/clawdmeter.err.log` instead |
| Running, log has `Polling Claude every 60s` and no `5h=` line (on macOS, a `Keychain read failed: … exit status 44` line after each poll says the same thing) | If no API or network error follows it, there is no usable Claude token: redo the login step in [Daemon setup, start to finish](/smalltv-mod/getting-started/first-setup/), then [AUTHENTICATION.md](https://github.com/kittipitch/clawdmeter-daemon/blob/main/AUTHENTICATION.md) |
| Running with `5h=` lines, device unchanged | Wrong push target, or the daemon came up in serve mode because `--install` remembered no flags (see the warning above) |
