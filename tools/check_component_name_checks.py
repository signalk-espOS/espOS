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

Use espos_has_component(<var> <bare-name>) from espos_core's
project_include.cmake instead; it matches COMPONENT_NAME, which is the bare name
under either spelling.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
# `if(espos_foo IN_LIST <anything>)` -- the variable name varies (comps, _comps).
BARE = re.compile(r"\b(espos_[a-z0-9_]+)\s+IN_LIST\b")

def main() -> int:
    bad = []
    for f in sorted(ROOT.glob("components/*/CMakeLists.txt")):
        for n, line in enumerate(f.read_text().splitlines(), 1):
            if line.lstrip().startswith("#"):
                continue
            m = BARE.search(line)
            if m:
                bad.append((f.relative_to(ROOT), n, m.group(1), line.strip()))

    if bad:
        print("error: bare espOS component name tested against a target list.")
        print("These are silently FALSE for a registry install (espOS #138).")
        print("Use espos_has_component(<var> %s) instead.\n" % "<name>")
        for path, n, name, line in bad:
            print(f"  {path}:{n}: {line}")
        return 1

    n_ok = sum(
        1
        for f in ROOT.glob("components/*/CMakeLists.txt")
        if "espos_has_component(" in f.read_text()
    )
    print(f"ok: no bare component-name checks; {n_ok} file(s) use espos_has_component()")
    return 0

if __name__ == "__main__":
    sys.exit(main())
