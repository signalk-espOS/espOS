#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""A tracked file an example needs must survive packing.

`files.exclude` in a component manifest is a blunt instrument: it is matched
against paths, not against intent, so a pattern aimed at build output can take
source with it. That is not hypothetical -- `examples/*/sdkconfig*` was written
to drop the GENERATED sdkconfig and also stripped the tracked
`sdkconfig.defaults[.<target>]` files from every published archive since the
manifests were written.

Nothing noticed for three releases, because the examples that lost files are
the ones whose settings come from espos_project_prologue() anyway. The example
it broke is `from_registry`, the one WITHOUT the prologue, where the sdkconfig
carries the nine settings espos_core refuses to build without: the download
succeeded and then failed to configure, naming settings whose file had been
removed on the way out.

So: every file an example directory tracks in git must appear in the archive,
unless it is build output. Comparing the two is the only way to see an exclude
that is quietly too wide -- the manifest reads as reasonable either way.
"""
from __future__ import annotations

import fnmatch
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COMPONENTS = ROOT / "components"

# Build output and local state. These are the things an exclude SHOULD remove;
# anything else tracked under examples/ is source and has to ship.
NOT_SOURCE = (
    "sdkconfig",  # the generated one; sdkconfig.defaults* is source
    "sdkconfig.old",
    "dependencies.lock",
)
NOT_SOURCE_DIRS = ("build", "managed_components")


def tracked_example_files(component: Path) -> list[str]:
    """Paths (relative to the component) of tracked files under examples/."""
    out = subprocess.run(
        ["git", "ls-files", "examples"],
        cwd=component,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    keep = []
    for rel in out:
        parts = Path(rel).parts
        # examples/<name>/<...>
        if len(parts) >= 3 and any(
            p == d or p.startswith(d + "-") for p in parts[2:] for d in NOT_SOURCE_DIRS
        ):
            continue
        if Path(rel).name in NOT_SOURCE:
            continue
        keep.append(rel)
    return keep


def excludes(manifest: Path) -> list[str]:
    """The files.exclude patterns, read without a YAML dependency."""
    text = manifest.read_text()
    m = re.search(r"^files:\n(?:.*\n)*?\s*exclude:\n((?:\s*-\s*.*\n)+)", text, re.M)
    if not m:
        return []
    return [
        p.strip().strip('"').strip("'")
        for p in re.findall(r"^\s*-\s*(.+)$", m.group(1), re.M)
    ]


def main() -> int:
    problems: list[str] = []
    checked = components = 0

    for comp in sorted(COMPONENTS.iterdir()):
        manifest = comp / "idf_component.yml"
        if not manifest.exists() or not (comp / "examples").is_dir():
            continue
        components += 1
        pats = excludes(manifest)

        for rel in tracked_example_files(comp):
            checked += 1
            hit = next((p for p in pats if fnmatch.fnmatch(rel, p)), None)
            if hit:
                problems.append(f"{comp.name}: {rel}\n      excluded by '{hit}'")

    if problems:
        print("Tracked example source files that `files.exclude` removes from the")
        print("published archive:\n")
        for p in problems:
            print(f"  {p}")
        print(
            "\nEach of these is in git and will NOT be in the component archive, so a\n"
            "user who runs `idf.py create-project-from-example` gets an example that\n"
            "is missing part of itself. Narrow the pattern (exclude `sdkconfig`, not\n"
            "`sdkconfig*`), or add the file to NOT_SOURCE in this script if it really\n"
            "is build output."
        )
        return 1

    print(
        f"check_published_files: {checked} tracked example files across "
        f"{components} components, all reach the archive"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
