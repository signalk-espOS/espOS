#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Regenerate the committed dependencies.lock, and refuse to produce a wrong one.
#
# espOS commits its lock because it builds as an application, and nothing
# re-solves it on its own: the component manager rewrites only the entries a
# CHANGED manifest touches, so an entry nobody edits stays as it was for ever.
# That is not theoretical -- #174 corrected esp-sr and left espos_* at 0.13.0
# and esp_wifi_remote at `*`, and the 0.15.0 release bumped fourteen manifests
# and left all fourteen lock entries behind (#176).
#
# Two traps this script exists to avoid:
#
#   1. The lock records `target: esp32s3`. Regenerating on another target
#      rewrites it to that target's closure, so the file stops describing what
#      it claims to.
#   2. Building a NARROW project (a host test, one example) rewrites the lock
#      to that project's closure and silently drops esp-sr, esp-dl, dl_fft,
#      esp_new_jpeg, esp_websocket_client, cjson, littlefs and mdns. The result
#      is a smaller, plausible, wrong lock -- so the components are asserted
#      back, not assumed.
#
# Usage: scripts/regen_lock.sh            regenerate in place
#        scripts/regen_lock.sh --check    regenerate and fail if it differed
# Needs the ESP-IDF environment.
set -euo pipefail
cd "$(dirname "$0")/.."

check=0
[ "${1:-}" = "--check" ] && check=1

TARGET=esp32s3
BUILD=build/lockgen

# The generated config goes under the build directory, not to ./sdkconfig.
# IDF refuses to change target over an existing sdkconfig, and defaults apply
# only to a fresh one, so this needs a config it owns -- but deleting the
# tree's would throw away whatever target and local edits the person running
# it was working with, which is a rude thing for a lock refresh to do.
rm -rf "$BUILD"
idf.py -B "$BUILD" -D SDKCONFIG="$BUILD/sdkconfig" set-target "$TARGET" >/dev/null

got=$(sed -n 's/^target: //p' dependencies.lock)
[ "$got" = "$TARGET" ] || { echo "regen_lock.sh: lock says target '$got', wanted '$TARGET'" >&2; exit 1; }

missing=()
for c in espressif/esp-sr espressif/esp-dl espressif/dl_fft espressif/esp_new_jpeg \
         espressif/esp_websocket_client espressif/cjson joltwallet/littlefs espressif/mdns; do
  grep -q "^  $c:" dependencies.lock || missing+=("$c")
done
if [ ${#missing[@]} -gt 0 ]; then
  echo "regen_lock.sh: regenerated lock is missing ${missing[*]}" >&2
  echo "regen_lock.sh: that is the narrow-closure trap -- the lock was not built from the whole app" >&2
  exit 1
fi

if [ "$check" = 1 ]; then
  if ! git diff --quiet -- dependencies.lock; then
    echo "regen_lock.sh: dependencies.lock does not match the manifests." >&2
    echo "Run scripts/regen_lock.sh and commit the result. Diff:" >&2
    git --no-pager diff -- dependencies.lock >&2
    exit 1
  fi
  echo "regen_lock.sh: lock matches the manifests ($TARGET)"
else
  echo "regen_lock.sh: regenerated for $TARGET"
  git --no-pager diff --stat -- dependencies.lock
fi
