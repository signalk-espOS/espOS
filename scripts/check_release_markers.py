#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Check that release-please will bump every espOS version, and nothing else.

Releases are lockstep: every component manifest carries version.txt's version,
and every espOS-to-espOS dependency carries ^<that version>. release-please
bumps version.txt itself; the manifests it bumps only because
release-please-config.json lists them as "generic" extra files AND each
version line inside them sits in an

    # x-release-please-start-version
    version: "..."
    # x-release-please-end

block. Either half missing fails quietly and totally: the release leaves one
component, or one sibling range, at the old version -- and pre-1.0 ^0.7.0
EXCLUDES 0.8.0, so the published set cannot be installed together. Nothing
notices until someone tries.

A block around anything else is the opposite mistake: the generic updater
rewrites every version-looking string in it, so an `idf:` or
`espressif/cjson:` pin inside one would become the espOS version.
"""
import glob
import json
import re
import sys

START = "# x-release-please-start-version"
END = "# x-release-please-end"
CONFIG = "release-please-config.json"


def main():
    errors = []
    config = json.load(open(CONFIG))
    listed = {
        f["path"]
        for f in config["packages"]["."].get("extra-files", [])
        if f.get("type") == "generic"
    }
    manifests = sorted(glob.glob("components/*/idf_component.yml"))

    for m in manifests:
        if m not in listed:
            errors.append(f"{m}: not in {CONFIG} extra-files, so a release would not bump it")
    for path in sorted(listed):
        if not glob.glob(path):
            errors.append(f"{CONFIG}: extra-file {path} does not exist")

    for path in manifests:
        lines = open(path).read().split("\n")
        own_seen = False
        in_sibling, sibling_indent = False, 0
        block = None  # index of an open start marker
        for i, line in enumerate(lines):
            stripped = line.strip()
            indent = len(line) - len(line.lstrip(" "))
            where = f"{path}:{i + 1}"

            if stripped == START:
                if block is not None:
                    errors.append(f"{where}: start marker inside another block")
                block = i
                continue
            if stripped == END:
                if block is None:
                    errors.append(f"{where}: end marker without a start")
                elif i - block != 2 or not re.match(r'^\s*version: *"', lines[block + 1]):
                    errors.append(f"{path}:{block + 1}: a marker block must hold exactly one version: line")
                block = None
                continue

            if in_sibling and stripped and indent <= sibling_indent:
                in_sibling = False
            if re.match(r'^\s+signalk-espos/espos_[a-z_0-9]+: *$', line):
                in_sibling, sibling_indent = True, indent

            is_own = not own_seen and re.match(r'^version: *"', line)
            is_sibling = in_sibling and re.match(r'^\s+version: *"', line)
            if is_own:
                own_seen = True
            if (is_own or is_sibling) and block is None:
                kind = "the component's version" if is_own else "a sibling espOS range"
                errors.append(f"{where}: {kind} is not inside an x-release-please-start-version block")
        if block is not None:
            errors.append(f"{path}:{block + 1}: start marker never closed")
        if not own_seen:
            errors.append(f"{path}: no top-level version:")

    version = open("version.txt").read().strip()
    manifest_version = json.load(open(".release-please-manifest.json")).get(".")
    if manifest_version != version:
        errors.append(f".release-please-manifest.json says {manifest_version}, version.txt says {version}")

    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    print(f"check_release_markers.py: {len(manifests)} manifests listed and marked, release-please at {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
