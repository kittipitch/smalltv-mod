#!/usr/bin/env bash
# Gate a built image before it is allowed near hardware.
#
# Usage: scripts/preflash_check.sh <env>        e.g. smalltv_sdpro
#
# Refuses the image unless ALL of these hold:
#   1. working tree is clean          -- otherwise GIT_SHA is stamped with '+'
#   2. HEAD is published on origin    -- otherwise nobody can reproduce the build
#   3. bin's stamped sha == HEAD      -- catches "built before committing"
#   4. no '+' anywhere in the stamp
#   5. bin fits the OTA slot
#
# Written after 2026-09-11, where the order was edits -> build -> commit, so the
# image was stamped 40470e2+ : a SHA that is not a commit, carrying a suffix that
# reads "this image contains uncommitted work". It was about to be sent to the
# unit that is hardest to re-reach. Everything here is checkable by a machine,
# which is the point -- the written procedure had already existed and was skipped.
set -uo pipefail
cd "$(dirname "$0")/.."

ENV="${1:-}"
[ -z "$ENV" ] && { echo "usage: $0 <pio-env>   (e.g. smalltv_sdpro)"; exit 2; }
BIN=".pio/build/$ENV/firmware.bin"
OTA_SLOT=716800
fail=0
say() { printf '  %-5s %s\n' "$1" "$2"; }

echo "pre-flash gate: $ENV"

# 1. clean tree
if [ -n "$(git status --porcelain)" ]; then
  say FAIL "working tree dirty -- commit first, then rebuild (GIT_SHA stamps at BUILD time)"
  git status --porcelain | sed 's/^/        /' | head -10
  fail=1
else
  say ok "working tree clean"
fi

HEAD_SHA=$(git rev-parse --short HEAD)

# 2. HEAD published
git fetch -q origin 2>/dev/null || say warn "could not fetch origin; publish check may be stale"
if git merge-base --is-ancestor HEAD origin/main 2>/dev/null; then
  say ok "HEAD ($HEAD_SHA) is on origin/main"
else
  say FAIL "HEAD ($HEAD_SHA) is NOT on origin/main -- push before flashing, or the build is unreproducible"
  fail=1
fi

# 3/4. the stamp itself
if [ ! -f "$BIN" ]; then
  say FAIL "no image at $BIN -- build it"
  fail=1
else
  if strings "$BIN" | grep -qE "[0-9a-f]{7}\+"; then
    say FAIL "image carries a DIRTY stamp: $(strings "$BIN" | grep -oE '[0-9a-f]{7}\+' | sort -u | tr '\n' ' ')"
    fail=1
  else
    say ok "no dirty '+' stamp in image"
  fi
  if strings "$BIN" | grep -qF "$HEAD_SHA"; then
    say ok "image stamped $HEAD_SHA (matches HEAD)"
  else
    say FAIL "image does NOT contain HEAD's sha $HEAD_SHA -- built from a different commit; rebuild"
    fail=1
  fi

  # 5. size
  SZ=$(wc -c < "$BIN" | tr -d ' ')
  if [ "$SZ" -le "$OTA_SLOT" ]; then
    say ok "size $SZ B, OTA headroom $((OTA_SLOT - SZ)) B"
  else
    say FAIL "size $SZ B EXCEEDS the $OTA_SLOT B OTA slot"
    fail=1
  fi
  echo "  md5  $(md5 -q "$BIN" 2>/dev/null || md5sum "$BIN" | cut -d' ' -f1)   <- must match on the flashing host"
fi

echo
if [ "$fail" -ne 0 ]; then
  echo "REFUSED. Do not flash this image."
  exit 1
fi
echo "CLEARED for flash: $ENV @ $HEAD_SHA"
echo "After flashing, confirm the device reports it:  /api/status -> \"sha\":\"$HEAD_SHA\""
