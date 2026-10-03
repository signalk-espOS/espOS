# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Report third-party registry components newer than this tree pins.

Dependabot cannot do this: the ESP-IDF component manager is not one of its
ecosystems, so nothing watches `idf_component.yml`. The usual answer is to
re-resolve within the declared ranges and diff `dependencies.lock` -- which is
what espos-p4-cockpit's dependency-drift.yml does, and what espOS cannot use,
because espOS pins its third-party dependencies EXACTLY. There is nothing to
re-resolve, so a lock diff is empty however far behind the pins have fallen.

So the question here is the other one: not "did the lock move?" but "is there
anything newer than what we pinned?". That is a registry query per dependency,
and it is the notification that was missing while espOS sat on esp_hosted
2.12.x for the weeks 3.x was out.

Reports only. Taking a bump means editing the manifests and proving it builds,
which is a pull request, not a cron job.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request

REGISTRY = "https://components.espressif.com/api/components"

# `name: "range"` on one line, or `name:` then an indented `version: "range"`.
_INLINE = re.compile(r'^  ([a-z0-9_]+/[a-z0-9_.-]+):\s*"([^"]+)"\s*$', re.M)
_BLOCK = re.compile(
    r'^  ([a-z0-9_]+/[a-z0-9_.-]+):\s*\n(?:\s*#.*\n)*\s+version:\s*"([^"]+)"', re.M
)

# Ours, and moved by release-please rather than by anyone reading this report.
OURS = "signalk-espos/"


def parse_version(v: str) -> tuple:
    """Order an Espressif component version.

    Their format is MAJOR.MINOR.PATCH[~REVISION][-PRERELEASE]. The `~` is the
    component revision -- cjson ships `1.7.19~2` -- and sorts after a bare
    patch. A prerelease sorts BEFORE its release, so 1.0.0-rc1 < 1.0.0, which
    is what keeps `newest` from being a release candidate.
    """
    core, _, rest = v.partition("-")
    core, _, rev = core.partition("~")
    nums = [int(x) for x in re.findall(r"\d+", core)[:3]]
    while len(nums) < 3:
        nums.append(0)
    # No prerelease sorts above any prerelease: (1,) beats (0, ...).
    pre = (1,) if not rest else (0, rest)
    return (*nums, int(rev or 0), pre)


def is_prerelease(v: str) -> bool:
    # A hyphen anywhere is a prerelease: Espressif's revision suffix uses `~`
    # and the build metadata separator `+` cannot precede one.
    return "-" in v


def manifest_deps(root: pathlib.Path) -> dict[str, set[str]]:
    """Every third-party dependency and the ranges the tree declares for it."""
    found: dict[str, set[str]] = {}
    for f in sorted(root.rglob("idf_component.yml")):
        if "managed_components" in f.parts or "build" in f.parts:
            continue
        text = f.read_text()
        for pat in (_INLINE, _BLOCK):
            for name, rng in pat.findall(text):
                if name.startswith(OURS) or name == "idf":
                    continue
                found.setdefault(name, set()).add(rng)
    return found


def newest_published(name: str, *, opener=urllib.request.urlopen) -> str | None:
    """The newest non-prerelease version on the registry, or None."""
    try:
        with opener(f"{REGISTRY}/{name}", timeout=30) as r:
            data = json.load(r)
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, ValueError):
        return None
    versions = [v.get("version") for v in data.get("versions", []) if v.get("version")]
    stable = [v for v in versions if not is_prerelease(v)] or versions
    if not stable:
        return None
    return max(stable, key=parse_version)


def classify(ranges: set[str], newest: str | None) -> tuple[str, str]:
    """(state, detail). state is one of: current, behind, unpinned, unknown."""
    if newest is None:
        return "unknown", "the registry did not answer"
    exact = {r[2:] for r in ranges if r.startswith("==")}
    if exact and len(exact) == len(ranges):
        if len(exact) > 1:
            return "behind", f"pinned inconsistently: {', '.join(sorted(exact))}"
        pinned = exact.pop()
        if parse_version(pinned) < parse_version(newest):
            return "behind", f"pinned {pinned}, newest {newest}"
        return "current", f"pinned {pinned}"
    # Not exact: the solver takes the newest the range permits, whenever it
    # next runs. Worth reporting whether or not anything has moved yet.
    return "unpinned", f"declared {', '.join(sorted(ranges))}, newest {newest}"


def report(root: pathlib.Path, *, fetch=newest_published) -> list[dict]:
    rows = []
    for name, ranges in sorted(manifest_deps(root).items()):
        newest = fetch(name)
        state, detail = classify(ranges, newest)
        rows.append(
            {"name": name, "ranges": sorted(ranges), "newest": newest,
             "state": state, "detail": detail}
        )
    return rows


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=".", type=pathlib.Path)
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args(argv)

    rows = report(args.root)
    if args.json:
        print(json.dumps(rows, indent=2))
    else:
        for r in rows:
            print(f"{r['state']:9} {r['name']:34} {r['detail']}")
    # Exit code says whether there is something to act on, so a workflow can
    # branch on it: 1 = behind or unpinned, 0 = everything current or unknown.
    # Unknown is NOT a failure -- a registry outage must not open an issue
    # claiming a component moved.
    return 1 if any(r["state"] in ("behind", "unpinned") for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
