---
title: Sharing your laptop's WiFi
description: Give the device a 2.4 GHz network to join when your router cannot, by sharing your computer's connection.
---

The device needs a **2.4 GHz** network. Every board this firmware runs on is 2.4 GHz only — the ESP8266, the ESP32-C2 and the classic ESP32 alike. None of them can see a 5 GHz SSID, and no setting changes that.

That is usually fine. It becomes a problem when:

- **Your router is 5 GHz only**, or its 2.4 GHz radio is switched off.
- **Band steering is on.** One SSID covers both bands and the router decides. The device may join, drop, or never appear, seemingly at random.
- **The network isolates clients.** Common on campus, hotel and guest WiFi. The device joins and gets an IP, but your laptop cannot reach its web UI, so you cannot configure it or push updates to it.
- **A captive portal is in the way.** The device has no browser and cannot accept terms.
- **WPA2-Enterprise (802.1X).** Most of these boards cannot join one at all.
- **There is no network**, because you are on a train, in a hotel room, or at a workshop.

In all of those cases, the fix is the same: **make your computer the 2.4 GHz network**. It joins the internet however it likes, and hands out a small private network the device can live on. You keep working, the device is reachable, and nothing depends on the venue's router.

:::caution[One WiFi card means one band]
A laptop with a single WiFi card generally **cannot stay on 5 GHz and host a 2.4 GHz hotspot at the same time**. You have to choose. If you want to share on 2.4 GHz, your own connection has to be on 2.4 GHz too — see [Doing it on one WiFi card](#doing-it-on-one-wifi-card-without-losing-your-connection) for why.

On **macOS** it is stricter: Internet Sharing will not share a WiFi connection over WiFi at all, on any band. You need a different uplink first.

A second radio — a USB WiFi dongle — is the only way to have both properly.
:::

## The quick options

### Phone hotspot

The simplest one, and worth trying first. Enable the personal hotspot and **force it to 2.4 GHz** — iOS calls this "Maximize Compatibility", Android usually has a 2.4/5 GHz choice in hotspot settings. Join your laptop to it too, and both ends can see each other.

Watch the data plan if you have the device pulling updates.

### Windows

Settings → **Network & Internet** → **Mobile hotspot**. Set **Band** to **2.4 GHz** — it defaults to 5 GHz on many machines, which the device cannot see.

### macOS

System Settings → **General** → **Sharing** → **Internet Sharing**. Note the significant catch: macOS will not share a WiFi connection *over* WiFi. You need a different uplink — Ethernet, a USB-C dock, or an iPhone tethered over USB — and then you can share it to WiFi. If WiFi is your only uplink, use a phone hotspot or a USB WiFi dongle instead.

### Linux (NetworkManager)

One command:

```bash
nmcli device wifi hotspot ifname wlan0 ssid smalltv-net password "choose8chars"
```

On a laptop with a single WiFi card this **disconnects you from your own network**, because NetworkManager will not run a hotspot and a client connection on one radio. If that is acceptable — you only need the device configured, not the internet — it is the fastest route. Otherwise read on.

## Doing it on one WiFi card, without losing your connection

Some cards can be a client and an access point at the same time. Check yours:

```bash
iw phy phy0 info | grep -A6 "valid interface combinations"
```

Look for a line offering `{ managed }` **and** `{ AP }` together. Then look at the number right after it:

```
* #{ managed } <= 1, #{ AP, P2P-client, P2P-GO } <= 1, #{ P2P-device } <= 1,
  total <= 3, #channels <= 1
```

`#channels <= 1` is the constraint that catches people out. The access point **cannot pick its own channel** — it has to beacon on whatever channel your client connection is already using. So if your laptop is on 5 GHz, the AP would have to be on 5 GHz too, and the device could not see it.

**Which means: to share on one radio, your laptop's own connection must be on a 2.4 GHz channel.** If your router offers both bands, join the 2.4 GHz one first, then start the AP.

Do not expect to work around this by forcing a 5 GHz AP. Regulatory rules in most regions mark 5 GHz channels "no initiating radiation" (`no IR`) — you may listen but not transmit first — and many cards enforce this in firmware where no kernel setting reaches. Some rules advertise an `IR-CONCURRENT` exemption that is supposed to lift this while a client is associated on that channel; on at least some Intel cards it does not apply in practice, and `hostapd` refuses with:

```
Frequency 5220 (primary) not allowed for AP mode, flags: 0x30073 NO-IR
```

2.4 GHz is the band that works. Since the device is 2.4 GHz only, that is no loss here.

The outline, once you are on a 2.4 GHz channel:

1. Add a second virtual interface in AP mode — `iw dev wlan0 interface add ap0 type __ap` — and give it its own locally-administered MAC. NetworkManager cannot do client+AP on one device by itself, which is why the extra interface is needed. Tell NM to ignore it.
2. Run `hostapd` on that interface, with `channel` set to whatever channel your client connection is on.
3. Run `dnsmasq` bound to that interface for DHCP and DNS.
4. NAT the hotspot subnet out of your real interface, and turn on IP forwarding.

Two details that cost real time if missed:

- **Let `hostapd` bring the AP interface up.** Doing `ip link set ap0 up` yourself fails with `RTNETLINK answers: Device or resource busy`, because the interface has no beacon configuration yet. Assign its IP address *after* `hostapd` starts.
- **Clamp TCP MSS.** WiFi-to-WiFi forwarding silently drops large segments without it. The symptom is confusing: DNS resolves, small pages load, anything big hangs forever.

```bash
iptables -t mangle -A FORWARD -o wlan0 -p tcp --tcp-flags SYN,RST SYN \
  -j TCPMSS --clamp-mss-to-pmtu
```

If Docker is installed, insert your `FORWARD` accept rules at the **head** of the chain (`-I`, not `-A`), or Docker's rules will drop your clients' traffic.

## The clean answer: a USB WiFi dongle

A second radio removes every constraint above at once. The dongle hosts the 2.4 GHz access point while your built-in card stays on 5 GHz at full speed, on any channel, with no shared airtime. Any cheap 2.4 GHz USB adapter with AP-mode support works.

Worth it if you flash and debug these devices often, or if you want the device on an isolated network permanently.

## After the hotspot is up

Join the device to it the normal way — see [First-time setup](/smalltv-mod/getting-started/setup/). Then find its address from your own DHCP leases rather than guessing:

```bash
# Linux, dnsmasq
cat /var/lib/misc/dnsmasq.leases

# or ask the AP interface who is associated
iw dev ap0 station dump | grep ^Station
```

Then browse to that IP. Because the device is on *your* network, there is no client isolation and no portal in the way, so the web UI and OTA updates work normally.

A last thing worth knowing: a laptop hotspot is rarely persistent across reboots. If the device is a permanent fixture, put it on a real 2.4 GHz network, or make the hotspot a service that starts at boot.
