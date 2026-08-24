# Handoff

Last updated: 2026-08-24. Machine: `zenbooksilver` (Linux notebook).

## State

`main` is at `a57310b`, **3 commits unpushed**, and this laptop holds the only
copy of them. `git log --branches --not --remotes` lists them. Push before
trusting any other machine with this repo.

## What happened this session

`git pull` failed with `fatal: refusing to merge unrelated histories`. The cause
was two independent root commits with the same commit message:

- `526071e` — the local root, carrying **upstream giovi321/smalltv-mod** content.
- `76930b0` — `origin/main`'s root, carrying **this fork's** content (SD PRO
  target, slim build envs, the `WITH_TLS` toggle).

They were merged with `--allow-unrelated-histories`. All 16 shared files came
through as add/add conflicts and were resolved as a **union merge** (`b788027`).
In practice the `origin/main` side was a strict superset in nearly every hunk —
it contained the local side's code verbatim inside the new
`#ifdef SDPRO_CS_GND` / `#if WITH_TLS` branches — so keeping it lost nothing.
The one fact unique to the local side, that the Ultra's OTA install goes through
`smalltv_loader`, was merged back into the board table in `README.md`.

`a57310b` then fixed passages *outside* the conflict hunks that no longer matched
the merged content — the Ultra full-vs-slim image claim, the "SD PRO untested"
section, the board count, and fork repo URLs.

**This cannot recur.** `origin/main` is now an ancestor of `HEAD`
(`git merge-base --is-ancestor origin/main HEAD` passes), so both roots sit in
one history and the next pull is an ordinary fast-forward — *once the merge is
pushed*. Until then the remote still has only its own root.

## Verification

All seven PlatformIO envs build clean after the merge. There is **no Makefile**
in this repo; build with `pio` directly. `default_envs = smalltv`, so a bare
`pio run` builds only that one — name the rest explicitly:

```sh
pio run -e smalltv_loader -e smalltv_c2 -e smalltv_esp32 \
        -e smalltv_sdpro -e smalltv_sdpro_slim -e smalltv_slim
```

Measured image sizes, which the docs now quote exactly:

| env | bytes |
|---|---|
| `smalltv` | 723,632 |
| `smalltv_slim` | 579,616 |
| `smalltv_loader` | 315,920 |
| `smalltv_sdpro` | 718,656 |
| `smalltv_sdpro_slim` | 579,632 |
| `smalltv_c2` | 1,437,296 |
| `smalltv_esp32` | 1,495,504 |

The Ultra loader slot is ~700 KB (716,800 B), so the full image is 6,832 B too
big and the slim build is the only one that fits on the second hop. This is why
`flashing.md` was wrong before `a57310b`.

## Backups

Kept outside the repo, per the backup rule — nothing was deleted:

- `../smalltv-mod.bak_1787567358` — full repo copy taken before the merge.
- `../smalltv-mod_merge_baks/` — the 16 per-file `.bak_*` copies the conflict
  resolution made, moved out of the working tree so they would not be committed.

## Traps that cost time

- **The two roots share a commit message.** `git log --oneline` on each side looks
  identical, which makes the histories appear related when they are not. Compare
  trees (`git rev-parse HEAD^{tree}`), not subjects.
- **Conflict resolution leaves the rest of the file stale.** Union-merging 16
  files fixed every marker and still left four documents contradicting
  themselves, because the contradictions were in untouched shared text. After a
  merge like this, re-read whole documents, not just the hunks.
- **A bare `pio run` is not a full build here** — `default_envs` hides six targets.

## Hardware notes for the next session

- The **SmallTV-ultra has no USB-serial chip**; its USB-C is power only. Plugging
  it into a laptop never produces a `/dev/ttyUSB*`. Confirmed here: `lsusb -t`
  showed no serial device and the kernel never bound `ch341`/`cp210x`/`ftdi_sio`.
  Serial access needs the internal UART header plus an external 3.3 V adapter.
  Only the ESP32-C2 (onboard CH340C) and the NM-TV-154 enumerate over USB.
- A device was broadcasting **`SmallTV-Setup` on channel 1** at full signal, i.e.
  already running smalltv-mod and waiting in its captive portal at
  `http://192.168.4.1`.
- **This laptop cannot host a 2.4 GHz hotspot while online.** `wlo1` is an Intel
  AX201 whose interface combination is `#{managed} <= 1, #{AP,…} <= 1,
  #channels <= 1`. With the uplink on `@JumboPlus5GHz` (5540 MHz), a 2.4 GHz AP
  is a second channel and is refused; starting one drops the uplink, leaving no
  internet to share. There is no ethernet port. The saved `SmallTVHotspot`
  profile (AP, band bg, ch 6, `ipv4.method=shared`) is bound to
  `wlx1c61b436da71`, a USB WiFi dongle that **the user does not have**. The
  remaining options are to move `wlo1` to a 2.4 GHz SSID and lock the AP to that
  same channel, or to skip the hotspot and use an existing 2.4 GHz network.

## Open

- Push `main` to `origin` (not yet done — needs the user's go-ahead).
- Decide how to reach the Ultra: the live `SmallTV-Setup` portal, or a UART
  adapter that is not currently on hand.
