#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Check that espos_core and espos_net can read espOS's version from a PACKED
manifest.

A copy installed from the component registry has no espOS checkout around it,
so it falls back to its own idf_component.yml for the version -- and the
component manager rewrites that file when it packs it: keys sorted, comments
gone, the version unquoted. A pattern written against the source form matched
nothing there, and every registry build reported espOS 0.0.0-unknown.

The pattern is read from each component's CMakeLists.txt rather than repeated
here, so this checks the code that runs, and the archives are the ones
check_manifests.sh just packed.
"""

import pathlib
import re
import sys
import tarfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
COMPONENTS = ("espos_core", "espos_net")

# string(REGEX REPLACE "<pattern>" "\\1" ...) in the component's CMakeLists.txt
CMAKE_REPLACE = re.compile(r'string\(REGEX REPLACE "((?:[^"\\]|\\.)*)" "\\\\1"')


def cmake_pattern(component: str) -> str:
    text = (ROOT / "components" / component / "CMakeLists.txt").read_text()
    found = CMAKE_REPLACE.search(text)
    if found is None:
        raise SystemExit(f"{component}: no manifest-version pattern in CMakeLists.txt")
    # CMake string escapes -> the regex CMake actually compiles.
    return found.group(1).replace('\\"', '"').replace("\\t", "\t")


def packed_version_line(pack: pathlib.Path, component: str, version: str) -> str:
    with tarfile.open(pack / f"{component}_{version}.tgz") as archive:
        member = archive.extractfile("./idf_component.yml")
        if member is None:
            raise SystemExit(f"{component}: archive has no idf_component.yml")
        manifest = member.read().decode()
    return next((l for l in manifest.splitlines() if l.startswith("version:")), "")


def main() -> int:
    version = (ROOT / "version.txt").read_text().strip()
    pack = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/registry-pack"
    failed = False
    for component in COMPONENTS:
        pattern = cmake_pattern(component)
        line = packed_version_line(pack, component, version)
        got = re.sub(pattern, r"\1", line) if re.fullmatch(pattern, line) else ""
        if got != version:
            print(f"{component}: packed manifest line {line!r} parses as {got!r}, "
                  f"expected {version!r}", file=sys.stderr)
            failed = True
        else:
            print(f"{component}: packed manifest reads as espOS {got}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
