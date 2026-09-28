#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Build the from_registry example exactly as a newcomer gets it: downloaded
# from the Espressif Component Registry at a published version, into an empty
# directory, with nothing from this tree on the component search path.
#
# The example's own CI build (scripts/build_example.sh) compiles the tree it
# ships in through override_path, under bare component names, from source
# manifests. A registry install differs in all three -- managed_components/,
# namespaced names, manifests the component manager has rewritten -- and bugs
# that only exist there shipped in a release before anyone downloaded it.
#
# Usage: scripts/registry_smoke.sh <version|v-tag|""> <target>
#        "" builds the newest published version.
set -euo pipefail

version="${1#v}"
target="${2:?idf target}"
cd "$(dirname "$0")/.."

spec="signalk-espos/espos_core:from_registry"
[ -n "$version" ] && spec="signalk-espos/espos_core==$version:from_registry"

work="$PWD/build/registry-smoke/$target"
project="$work/from_registry"

# A release takes a few minutes to appear, and its components appear one at a
# time in dependency order, so the example can resolve before everything it
# depends on does. Retry the download and the dependency resolution only; a
# build that fails after resolving is a real failure and is not retried.
attempts=40
for i in $(seq 1 "$attempts"); do
    rm -rf "$work"
    mkdir -p "$work"
    if (cd "$work" && idf.py create-project-from-example "$spec") \
        && (cd "$project" \
            && espsecure generate-signing-key --version 2 --scheme rsa3072 \
                   secure_boot_signing_key.pem \
            && idf.py set-target "$target"); then
        break
    fi
    if [ "$i" = "$attempts" ]; then
        echo "registry_smoke.sh: $spec did not resolve after $attempts attempts" >&2
        exit 1
    fi
    echo "registry_smoke.sh: not resolvable yet (attempt $i/$attempts), retrying in 30 s"
    sleep 30
done

# A registry index that has not caught up can satisfy the example's ^X.Y.Z
# with an OLDER patch of any component, and the build would then prove nothing
# about this release. The components release in lockstep, so every espOS one
# must resolve to the requested version -- or, for "the newest", to one
# version between them.
python3 - "$project/dependencies.lock" "$version" <<'PY'
import re
import sys

lock, want = sys.argv[1], sys.argv[2]
resolved, current = {}, None
for line in open(lock):
    key = re.match(r"^  (signalk-espos/espos_\w+):\s*$", line)
    if key:
        current = key.group(1)
        continue
    if re.match(r"^  \S", line):
        current = None
    elif current:
        m = re.match(r"^    version:\s*['\"]?([^'\"\s]+)", line)
        if m:
            resolved[current] = m.group(1)
if not resolved:
    sys.exit("registry_smoke.sh: no espOS component in dependencies.lock")
versions = set(resolved.values())
expected = {want} if want else {max(versions, key=lambda v: [int(x) for x in re.findall(r"\d+", v)])}
wrong = {name: v for name, v in resolved.items() if v not in expected}
if wrong or len(versions) != 1:
    sys.exit(f"registry_smoke.sh: espOS components resolved to mixed or unexpected "
             f"versions (want {want or 'one version'}): {sorted(resolved.items())}")
print(f"registry_smoke.sh: {len(resolved)} espOS components resolved to {versions.pop()}")
PY

(cd "$project" && idf.py build)
