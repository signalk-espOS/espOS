#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Build one example project for one target, the way CI does it: inside the
# example directory, with idf.py's default build/ and sdkconfig (both
# git-ignored there). On a shared development host prefer scripts/build.sh
# with -B pointing outside the tree; this script exists so the CI workflow
# needs no quoting at all.
#
# Usage: scripts/build_example.sh <example-dir> <target>
set -euo pipefail
dir="${1:?example directory}"
target="${2:?idf target}"
cd "$(dirname "$0")/.."
[ -f "$dir/CMakeLists.txt" ] || { echo "build_example.sh: no CMakeLists.txt in $dir" >&2; exit 2; }
cd "$dir"

# An example that does not use the prologue has no key generated for it, on
# purpose: espos_ota/project_include.cmake refuses to invent a signing key for
# someone else's project, because a device then trusts a key nobody kept
# (docs/ota.md). The prologue does generate one, so every other example builds
# without this. Make a throwaway here so CI can build that example too -- it
# signs a binary nothing will ever flash, and *.pem is git-ignored.
#
# BEFORE set-target, not after: IDF wires "key is missing" into the ninja graph
# while generating the build directory, so a key that appears afterwards leaves
# that rule in place and the build fails on it anyway. Which also means the
# generated sdkconfig does not exist yet -- hence reading the project's
# defaults.
#
# espsecure directly, not `idf.py secure-generate-signing-key`: idf.py
# initialises the project to run a command, so calling it here created
# build/ and an sdkconfig at the DEFAULT target, and the set-target below
# then had no effect -- an esp32 build where CI asked for esp32c6, whose
# signing scheme is ECDSA rather than RSA, which the sdkconfig lint then
# (correctly) rejected. espsecure touches no project state.
if grep -qs '^CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES=y' sdkconfig.defaults \
   && [ ! -f secure_boot_signing_key.pem ]; then
    echo "build_example.sh: generating a throwaway signing key for $dir"
    espsecure generate-signing-key --version 2 --scheme rsa3072 secure_boot_signing_key.pem
fi

idf.py set-target "$target"
idf.py build
