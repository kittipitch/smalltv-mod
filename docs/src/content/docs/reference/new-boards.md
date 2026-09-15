---
title: Bringing up a new board
description: The investigation to do before flashing this firmware onto an unknown SmallTV-family variant — identify the model, parse its firmware images, and prove the OTA size budget before the first upload.
---

The same cube keeps reappearing under new vendor names, with new stock firmware
and, sometimes, a different updater inside. This page is the procedure for
bringing this firmware up on a variant that is **not** already listed in
[Hardware and variants](/smalltv-mod/getting-started/hardware/): what to
examine on the stock device, in what order, and why — **before** the first
flash, not after.

It exists because of a real failure. The SD PRO clone's stock updater performs
no free-space check: hand it an image larger than its slot and it replies
`HTTP 200 OK`, writes past the end anyway, and the device never boots again —
dark screen, no WiFi, no access point. Two units were bricked that way, on a
board whose USB-C port is power only, which left the internal UART header as
the only way back in. Every step below is a check that would have prevented it.

:::caution[The one rule]
Never upload a full-featured image to an updater you have not characterized.
An unknown vendor updater's `200 OK` proves the upload was *received*, not
that it *fit*. Until you have real evidence of the OTA size budget — evidence,
not a spec sheet or a forum post — the only thing safe to upload is nothing,
and the second-safest is a tiny loader image well under a size that has
demonstrably installed before.
:::

## Step 0 — secure a way back before you change anything

- **Confirm a recovery path exists.** Does the USB-C port enumerate as a
  serial device? If not, the board has no USB-serial chip and recovery from a
  bad flash means locating its internal UART pads. Find them (and ideally test
  them) **before** the first flash, not while holding a dead unit.
- **Back up whatever you can.** If you have UART access, dump the full flash
  (`esptool.py read_flash 0x0 0x400000 stock-backup.bin`). There is no way to
  read the running firmware back over HTTP, and vendors often publish only
  some versions, so this dump may be the only copy of what is on the device.

## Step 1 — identify the exact model, from the device itself

Do not identify by looks: several of these devices are visually identical and
take different images. Ask the stock firmware over the network:

```bash
IP=<device-ip>
curl -s http://$IP/                | grep -oiE 'V[0-9]+\.[0-9]+\.[0-9]+'
curl -s http://$IP/config          | head -c 300   # a vendor JSON API here is a strong tell
curl -s -o /dev/null -w '%{http_code}\n' http://$IP/api/status   # 404 = stock, 200 = already smalltv-mod
```

Then find the vendor's own repository — usually linked from the stock web
UI's footer or an "about" panel — and check it for published firmware images.
Do this **even if you think you already know the vendor**: with the SD PRO,
the `/config` response pointed at [`JUZIPi-tech/SD_PRO`](https://github.com/JUZIPi-tech/SD_PRO),
which made it possible to analyze two real vendor images instead of guessing
at the updater's behaviour.

While you are in the stock UI, read its JavaScript and note the actual OTA
route and multipart field name. Do not conclude "no OTA" from `/update`
returning 404 — the route may simply have moved (the SD PRO uses
`POST /update_ota` with a field named `update`; the original SmallTV uses
`POST /update` with `file`).

## Step 2 — parse every firmware image you can get

Collect every vendor firmware version published, plus your own built
candidate, and parse the ESP8266 image header on all of them. This needs
nothing but Python — no device, no hardware risk:

```python
import struct, hashlib

def parse(path):
    d = open(path, 'rb').read()
    print(path, f"{len(d):,} B", hashlib.sha256(d).hexdigest()[:16])
    assert d[0] == 0xE9, "not an ESP8266 image"
    nseg, mode, szfreq = d[1], d[2], d[3]
    entry = struct.unpack('<I', d[4:8])[0]
    print(f"  segments={nseg} mode=0x{mode:02x} size/freq=0x{szfreq:02x} entry=0x{entry:08x}")
    off = 8
    for i in range(nseg):
        addr, ln = struct.unpack('<II', d[off:off+8]); off += 8
        print(f"    seg{i}: addr=0x{addr:08x} len={ln:,}"); off += ln
    print(f"  second image / trailing data at 0x{off:x}")
```

This answers three questions directly from the bytes, with no assumptions:

- **Is the vendor's firmware built with the same toolchain as ours?** Compare
  the entry point, the IRAM segment address (`0x4010f000` for
  Arduino-ESP8266-core builds), and whether a second `0xE9` header sits at
  offset `0x1000` (the eboot-plus-sketch layout). If the vendor image has the
  *same* shape as yours, your build's format is not the problem — whatever
  fails is about **size or transport**, not encoding.
- **How big is the image the device is actually running?** That size is real,
  demonstrated proof of what the vendor's updater can install — proof no
  datasheet or forum post gives you. Note that on the Arduino ESP8266 core the
  OTA slot is computed from the *current* sketch's own end
  (`ESP.getFreeSketchSpace()`, 4 KB-aligned), so a bigger installed image
  means **less** room for the next upload, not more.
- **Does the fourth header byte match your build?** It encodes the flash mode
  and size/frequency nibbles; a mismatch there is a classic ESP8266 boot loop.

## Step 3 — prove the size budget, don't compute it

You will rarely know a new vendor's flash partition layout or filesystem
offsets with confidence, and you do not need to. What you *can* always
establish, with no assumptions at all, is a **correlation from real installs**:

- An image size that is **known to have installed successfully on this exact
  unit** — the vendor's own update, or an earlier successful install — is a
  **safe upper bound**. Anything at or under that size is at least as likely
  to install as what already worked.
- An image size that **failed or bricked a unit** is a data point about size
  first. Check it against the safe bound above before blaming the image
  format, the pins, or the hardware.

On the SD PRO this looked like: the vendor's own V1.0.6 image (488,304 B) had
installed cleanly on the very unit that later bricked on a 718 KB upload, and
the ~316 KB loader — well under that proven bound — went through twice with
zero failures. The precise numeric budget only mattered later, for deciding
which features to compile out of the second-stage image; it was never needed
to choose the first safe move.

:::caution[Never trust a bare `200 OK` as a size check]
A stock Arduino updater refuses an oversized image outright
(`Updater::begin()` fails with `UPDATE_ERROR_SPACE` and nothing is written).
You can test whether a vendor updater still has that check without risking the
device's contents being *believed*: if it answers `OK` to a
deliberately-oversized upload and to a normal one alike, its `OK` carries no
information, and every size decision must come from Step 3's evidence instead.
:::

:::note[Don't stop at the first plausible theory]
When a first flash goes wrong, the most satisfying explanation is not
automatically the right one. On the SD PRO, a boot-strap-pin theory (GPIO15
driven by the display CS line) fully explained the symptom — and was wrong: a
second unit built specifically to avoid driving that pin died identically.
**When a fix doesn't change the outcome, the theory was wrong, not the fix.**
Parsing the actual bytes involved is what surfaced the real mechanism (an
oversized upload) after the plausible hardware theory did not.
:::

## Step 4 — first flash through a loader, never directly

Once you have a proven safe size, the first upload should still be the
smallest thing that works: the [loader](/smalltv-mod/reference/building/#the-smalltv_loader-env),
a ~316 KB image with WiFi and an OTA endpoint and nothing else — no display
code, no `pinMode` calls, so it cannot trip over an unknown pin map either.

```bash
# 1) the loader, through the vendor's own OTA route (field name from Step 1)
curl -F "update=@smalltv-mod-loader.bin" http://$IP/update_ota
```

The screen going dark and the device leaving the network is **expected** —
the loader drives no display. Confirm it is alive by its fingerprint:

```bash
curl -o /dev/null -w '%{http_code}\n' http://$IP/update   # 200 = loader's upload form
curl -o /dev/null -w '%{http_code}\n' http://$IP/         # 404 = loader (stock served a page here)
```

```bash
# 2) the real firmware, through the loader's own updater
curl -F "firmware=@smalltv-mod-firmware.bin" http://$IP/update
```

The loader runs Arduino's standard update server, so this second hop has a
**genuine** size check: it refuses honestly if the image is still too big, and
its `Update Success! Rebooting...` page is emitted only after the write has
been verified — unlike a vendor `OK`, that message is evidence.

If the full image does not fit even through the loader, compile features out and
re-check the resulting size against Step 3's bound before assuming the hardware is
the problem. `WITH_RADAR=0` is the usual lever and is already the default on every
target. Dropping TLS (`WITH_TLS=0`) would free ~107 KB but costs the GitHub
self-updater, so treat it as a last resort rather than a starting point — there is
no longer a separate slim build.

## Step 5 — write down what you found

A variant solved this way should never need solving twice. Record, ideally as
a pull request to these docs:

- the identification tells: version string format, the `/config` shape, the
  vendor repo, the OTA route and field name;
- the parsed image facts: toolchain shape, installed-image sizes, the proven
  size bound;
- the pin map and any panel quirks, once the firmware is running;
- the recovery path: UART pad locations, and whether USB-C carries data.

The [SD PRO section](/smalltv-mod/getting-started/hardware/#sd-pro-juzipi-clone-esp8266)
in Hardware and variants is the template: everything a future owner of that
board needs, learned once, written down once.
