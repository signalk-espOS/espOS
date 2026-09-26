#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Fail on a bare espOS component name tested against BUILD_COMPONENTS.

BUILD_COMPONENTS holds CMake TARGET names, and the component manager namespaces
those on install: a registry copy of espos_wifi builds as
`signalk-espos__espos_wifi`. So `if(espos_wifi IN_LIST comps)` is true in-tree
and silently FALSE for every registry install -- espos_start() brought up no
WiFi, no SignalK and no OTA on a firmware that linked all three, with no error
and no warning (espOS #138).

Why a checker and not just the fix: espOS's own CI cannot catch a regression
here. The from_registry example uses override_path, so it builds under the bare
names too -- the one configuration that would notice is the one CI does not
have. This is cheap, exact, and runs everywhere.

Use espos_has_component(<var> <bare-name>) from the calling component's own
cmake/espos_components.cmake instead; it matches COMPONENT_NAME, which is the
bare name under either spelling.

Also checks that every shipped copy of that file is byte-identical. Each
component carries its own because one installed from the registry cannot reach a
sibling's directory, and copies that drift would give the same silent
divergence this whole checker exists to prevent.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# `if(espos_foo IN_LIST <anything>)` -- the variable name varies (comps, _comps).
# \s+ spans newlines, because CMake allows the condition to wrap and a check
# split across two lines is the same bug. The optional quotes matter too: CMake
# takes `if("espos_wifi" IN_LIST comps)` as the same literal element, so a quoted
# name is the identical defect and would otherwise slip past.
BARE = re.compile(r"\"?\b(espos_[a-z0-9_]+)\b\"?\s+IN_LIST\b", re.S)

def main() -> int:
    bad = []
    scanned = sorted(ROOT.glob("components/*/CMakeLists.txt"))
    # project_include.cmake and the component cmake/ files can hold the same
    # check, so scan them too rather than only the obvious file.
    scanned += sorted(ROOT.glob("components/*/project_include.cmake"))
    scanned += sorted(ROOT.glob("components/*/cmake/*.cmake"))
    for f in scanned:
        # Strip comments first, then scan the whole file: a wrapped condition
        # would slip past a line-at-a-time scan.
        lines = f.read_text().splitlines()
        stripped = "\n".join("" if l.lstrip().startswith("#") else l for l in lines)
        for m in BARE.finditer(stripped):
            n = stripped.count("\n", 0, m.start()) + 1
            bad.append((f.relative_to(ROOT), n, m.group(1), lines[n - 1].strip()))

    if bad:
        print("error: bare espOS component name tested against a target list.")
        print("These are silently FALSE for a registry install (espOS #138).")
        print("Use espos_has_component(<var> %s) instead.\n" % "<name>")
        for path, n, name, line in bad:
            print(f"  {path}:{n}: {line}")
        return 1

    # Every copy must be identical. A component installed from the registry
    # cannot include a sibling's file, so the duplication is deliberate -- but
    # duplication that drifts is how one component ends up resolving names
    # differently from the next.
    copies = sorted(ROOT.glob("components/*/cmake/espos_components.cmake"))
    if copies:
        ref = copies[0]
        ref_bytes = ref.read_bytes()
        drifted = [c for c in copies[1:] if c.read_bytes() != ref_bytes]
        if drifted:
            print("error: shipped copies of espos_components.cmake have diverged.")
            print(f"reference: {ref.relative_to(ROOT)}")
            for c in drifted:
                print(f"  differs: {c.relative_to(ROOT)}")
            return 1

    n_ok = sum(
        1
        for f in ROOT.glob("components/*/CMakeLists.txt")
        if "espos_has_component(" in f.read_text()
    )
    print(f"ok: no bare component-name checks; {n_ok} file(s) use espos_has_component(); "
          f"{len(copies)} identical copies of espos_components.cmake")
    return 0

if __name__ == "__main__":
    sys.exit(main())
