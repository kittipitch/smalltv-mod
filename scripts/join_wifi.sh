#!/usr/bin/env bash
# Submit WiFi credentials to a device currently in SETUP MODE (its own
# "SmallTV-Setup" AP), so you don't have to type them into the captive-portal
# web UI by hand after every UART flash/erase cycle.
#
# Your computer must already be joined to the device's "SmallTV-Setup" AP
# (open network, no password) before running this -- that part isn't
# automated, see .env.example for why.
set -euo pipefail

cd "$(dirname "$0")/.."

if [ ! -f .env ]; then
  echo "No .env found. Copy .env.example to .env and fill in WIFI_SSID (and WIFI_PASS if needed)." >&2
  exit 1
fi
set -a
# shellcheck disable=SC1091
source .env
set +a

if [ -z "${WIFI_SSID:-}" ]; then
  echo "WIFI_SSID is empty in .env." >&2
  exit 1
fi

AP_IP="${AP_IP:-192.168.4.1}"

echo "Checking we're actually on the device's setup AP ($AP_IP)..."
if ! curl -s -m 3 "http://$AP_IP/api/status" >/dev/null; then
  echo "Can't reach $AP_IP -- join the device's \"SmallTV-Setup\" WiFi network first." >&2
  exit 1
fi

payload=$(WIFI_SSID="$WIFI_SSID" WIFI_PASS="${WIFI_PASS:-}" python3 -c '
import json, os
entry = {"ssid": os.environ["WIFI_SSID"]}
if os.environ.get("WIFI_PASS"):
    entry["pass"] = os.environ["WIFI_PASS"]
print(json.dumps({"wifi": [entry]}))
')

echo "Submitting WiFi credentials for \"$WIFI_SSID\"..."
resp=$(curl -s -m 5 -X POST "http://$AP_IP/api/config" -H "Content-Type: application/json" -d "$payload")
echo "$resp"

if echo "$resp" | grep -q '"reboot":true'; then
  echo "Device is rebooting to join \"$WIFI_SSID\". Give it ~10-15s, then check the router's client list or try http://<hostname>.local"
else
  echo "Unexpected response -- WiFi may not have been saved. Check the device manually." >&2
  exit 1
fi
