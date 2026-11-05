#!/usr/bin/env bash
# Gate a built image before it is allowed near hardware.
#
# Usage: scripts/preflash_check.sh <env>        e.g. smalltv_sdpro
#
# Refuses the image unless ALL of these hold:
#   1. working tree is clean          -- otherwise GIT_SHA is stamped with '+'
#   2. HEAD is published on origin    -- otherwise nobody can reproduce the build
#   3. bin's stamped sha is HEAD, or an ancestor of HEAD with only NOT_IN_IMAGE
#      files changed since              -- catches "built before committing"
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
# Paths that cannot change the built image. Keep this a short list of names: the
# build also reads platformio.ini, lib/, boards and scripts/git_sha.py.
NOT_IN_IMAGE='\.md$|^docs/|^scripts/test_[^/]*\.py$|^scripts/preflash_check\.sh$'
STAMP=
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
  # The stamp is the one hex string in the image that names a real commit. Other
  # strings can look like hex, so a token that is no commit is not a stamp.
  STAMPS=$(strings "$BIN" | grep -E '^[0-9a-f]{7,40}$' | sort -u | while read -r t; do
    git cat-file -e "$t^{commit}" 2>/dev/null && echo "$t"
  done)
  case "$(printf '%s' "$STAMPS" | grep -c .)" in
    0) say FAIL "image stamp names no commit in this repo -- rebuild"; fail=1 ;;
    1) STAMP=$STAMPS
       if [ "$(git rev-parse "$STAMP")" = "$(git rev-parse HEAD)" ]; then
         say ok "image stamped $STAMP (matches HEAD)"
       elif ! git merge-base --is-ancestor "$STAMP" HEAD; then
         say FAIL "image stamp $STAMP is not an ancestor of HEAD $HEAD_SHA -- rebuild"; fail=1
       else
         # A commit that cannot reach the image must not force a rebuild, which would
         # move a unit off its release sha. Allow only named paths: anything else fails.
         CHANGED=$(git diff --no-renames --name-only "$STAMP" HEAD | grep -vE "$NOT_IN_IMAGE")
         if [ -n "$CHANGED" ]; then
           say FAIL "files that can change the image moved since stamp $STAMP -- rebuild:"
           printf '%s\n' "$CHANGED" | sed 's/^/        /' | head -10
           fail=1
         else
           say ok "image stamped $STAMP; only docs and gate files changed up to HEAD $HEAD_SHA"
         fi
       fi ;;
    *) say FAIL "image holds several commit-like stamps: $(printf '%s' "$STAMPS" | tr '\n' ' ')-- rebuild"; fail=1 ;;
  esac

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
echo "CLEARED for flash: $ENV @ $STAMP"
echo "After flashing, confirm the device reports it:  /api/status -> \"sha\":\"$STAMP\""
