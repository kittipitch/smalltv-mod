#!/usr/bin/env bash
# Answer one question mechanically: is EVERY unit running firmware we can
# identify, and does that firmware correspond to a commit that is actually
# published?
#
# Exists because of the "mysterious firmware" incident: 6a12 was found running a
# build nobody could name. FW_VERSION only moves at a release cut, so it can
# never identify a build -- two units months apart both said "1.0.0-kitt23".
# GIT_SHA fixed identification; this script is what makes anyone LOOK.
#
# Failure modes it catches, each of which produced a real incident or near-miss:
#   NO-SHA   firmware predates GIT_SHA -> unidentifiable by construction
#   DIRTY    sha carries '+' -> built from a tree with uncommitted edits
#   UNKNOWN  sha is not a commit in this repo -> built from a lost checkout
#   UNPUSHED sha exists locally but is not on origin/main -> not reproducible
#            by anyone else; the exact shape of the stale-checkout incident
#
# Exit 0 only if every reachable unit is identified AND published.
set -uo pipefail
cd "$(dirname "$0")/.."

# name:mdns-hostname, space-separated. Your own fleet does not belong in a
# public repo, so list it in ONE of these instead -- both are gitignored:
#   scripts/fleet_units.local   a file holding the same "name:host name:host" string
#   SMALLTV_UNITS               the same string in the environment
# Add a unit the day it is flashed, not later.
UNITS_FILE="$(dirname "$0")/fleet_units.local"
UNITS="${SMALLTV_UNITS:-}"
if [ -z "$UNITS" ] && [ -r "$UNITS_FILE" ]; then
  UNITS="$(grep -v '^[[:space:]]*#' "$UNITS_FILE" | tr '\n' ' ')"
fi
if [ -z "$UNITS" ]; then
  echo "no units configured." >&2
  echo "  write them to $UNITS_FILE, one 'name:hostname' per line," >&2
  echo "  or export SMALLTV_UNITS='smalltv-3fa2:smalltv-3fa2.local ...'" >&2
  exit 2
fi

# Optional jump host for units on another LAN: SMALLTV_VIA=ubuntu_office
VIA="${SMALLTV_VIA:-}"

fetch() {  # $1 = host -> /api/status body on stdout
  if [ -n "$VIA" ]; then
    timeout 30 ssh -o ConnectTimeout=10 -o BatchMode=yes "$VIA" \
      "curl -s --max-time 8 http://$1/api/status" 2>/dev/null
  else
    curl -s --max-time 8 "http://$1/api/status" 2>/dev/null
  fi
}

git fetch -q origin 2>/dev/null || echo "warn: could not fetch origin; UNPUSHED checks use the last known origin/main"

fail=0; unreachable=0
printf '%-16s %-10s %-9s %-22s %s\n' UNIT SHA STATE VERSION NOTE
printf '%s\n' "----------------------------------------------------------------------------------"

for entry in $UNITS; do
  name="${entry%%:*}"; host="${entry##*:}"
  body="$(fetch "$host")"
  if [ -z "$body" ]; then
    printf '%-16s %-10s %-9s %-22s %s\n' "$name" "-" "UNREACHED" "-" "no answer on $host"
    unreachable=$((unreachable+1)); continue
  fi

  sha=$(printf '%s' "$body"    | grep -oE '"sha":"[^"]*"'     | cut -d'"' -f4)
  ver=$(printf '%s' "$body"    | grep -oE '"version":"[^"]*"' | cut -d'"' -f4)

  if [ -z "$sha" ]; then
    printf '%-16s %-10s %-9s %-22s %s\n' "$name" "-" "NO-SHA" "${ver:--}" \
      "pre-GIT_SHA build: UNIDENTIFIABLE -- reflash to fix"
    fail=$((fail+1)); continue
  fi

  case "$sha" in
    *+) printf '%-16s %-10s %-9s %-22s %s\n' "$name" "$sha" "DIRTY" "${ver:--}" \
          "built from a tree with uncommitted edits"
        fail=$((fail+1)); continue;;
  esac

  if ! git cat-file -e "${sha}^{commit}" 2>/dev/null; then
    printf '%-16s %-10s %-9s %-22s %s\n' "$name" "$sha" "UNKNOWN" "${ver:--}" \
      "not a commit in this repo -- built from a checkout we no longer have"
    fail=$((fail+1)); continue
  fi

  if git merge-base --is-ancestor "$sha" origin/main 2>/dev/null; then
    printf '%-16s %-10s %-9s %-22s %s\n' "$name" "$sha" "OK" "${ver:--}" \
      "$(git log -1 --format=%s "$sha" | cut -c1-40)"
  else
    printf '%-16s %-10s %-9s %-22s %s\n' "$name" "$sha" "UNPUSHED" "${ver:--}" \
      "commit exists locally but is NOT on origin/main"
    fail=$((fail+1))
  fi
done

echo
[ "$unreachable" -gt 0 ] && echo "note: $unreachable unit(s) unreachable from here (try SMALLTV_VIA=<host>)"
if [ "$fail" -gt 0 ]; then
  echo "FAIL: $fail unit(s) running firmware that cannot be identified or reproduced."
  exit 1
fi
echo "PASS: every reachable unit runs an identified, published commit."
