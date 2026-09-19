#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Every espOS component a CMakeLists hard-requires must be in its manifest.

A submodule build cannot catch this. espos_project_prologue() puts the whole
of components/ on EXTRA_COMPONENT_DIRS, so IDF resolves a sibling by name
whether or not idf_component.yml ever mentioned it. A firmware that installs
espOS from the component registry has no such directory: the manifest is the
only thing that gets the sibling downloaded, and a missing entry fails the
build with

    Failed to resolve component 'espos_time' required by 'espos_core'

which is what six components did when the registry path was first exercised.

REQUIRES and PRIV_REQUIRES both count -- both are needed at build time. Only
idf_component_optional_requires() is exempt, because it is skipped when the
component is absent, which is the point of it.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

COMPONENTS = Path(__file__).resolve().parent.parent / "components"


def strip_comments(text: str) -> str:
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def hard_requires(cmake: str) -> set[str]:
    """espos_* names this component needs at build time.

    Follows `set(reqs ...)` / `list(APPEND reqs ...)` into the REQUIRES that
    dereferences them, which is how several components build their list.
    """
    code = strip_comments(cmake)
    names: set[str] = set()

    # Variables assembled then used as ${reqs} / ${priv_reqs}.
    var_values: dict[str, set[str]] = {}
    for m in re.finditer(r"\b(?:set|list\(APPEND)\s*\(?\s*(\w+)([^)]*)\)", code):
        var, body = m.group(1), m.group(2)
        found = set(re.findall(r"\b(espos_[a-z0-9_]+)\b", body))
        if found:
            var_values.setdefault(var, set()).update(found)

    for m in re.finditer(r"\b(?:PRIV_)?REQUIRES\b([^)]*)", code):
        body = m.group(1)
        names |= set(re.findall(r"\b(espos_[a-z0-9_]+)\b", body))
        for var in re.findall(r"\$\{(\w+)\}", body):
            names |= var_values.get(var, set())

    return names


def declared(manifest: str) -> set[str]:
    return set(re.findall(r"signalk-espos/(espos_[a-z0-9_]+)", manifest))


def main() -> int:
    problems: list[str] = []
    checked = 0

    for comp in sorted(COMPONENTS.iterdir()):
        cmake_path, manifest_path = comp / "CMakeLists.txt", comp / "idf_component.yml"
        if not (cmake_path.exists() and manifest_path.exists()):
            continue
        checked += 1

        needs = hard_requires(cmake_path.read_text())
        needs.discard(comp.name)
        # Names that are not components at all (functions, option variables).
        needs = {n for n in needs if (COMPONENTS / n).is_dir()}

        missing = sorted(needs - declared(manifest_path.read_text()))
        if missing:
            problems.append(
                f"{comp.name}: requires {', '.join(missing)} but "
                f"idf_component.yml does not declare "
                f"{'them' if len(missing) > 1 else 'it'}"
            )

    if problems:
        print("Components whose manifest omits a build-time espOS dependency:\n")
        for p in problems:
            print(f"  {p}")
        print(
            "\nAdd each to the component's idf_component.yml, in the same form as its\n"
            "siblings (version with the release-please markers, plus override_path so\n"
            "an in-tree or submodule build keeps using the checkout).\n"
            "\nWithout it the component installs from the registry and then fails to\n"
            "build, which no submodule build can reproduce."
        )
        return 1

    print(f"check_component_deps: {checked} components, every hard dependency declared")
    return 0


if __name__ == "__main__":
    sys.exit(main())
