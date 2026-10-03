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
import http.client
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request

REGISTRY = "https://components.espressif.com/api/components"

# Stdlib only, like the repository's other manifest readers
# (tools/check_component_deps.py), so this runs by hand anywhere with no
# install. The cost is parsing YAML with regex, so both spellings accept all
# three scalar forms -- "x", 'x' and bare x -- and manifest_deps() reports a
# manifest it could not read rather than skipping it, because a dependency
# missed in silence is a checker that checks nothing.
_NAME = r"([a-z0-9_-]+/[a-z0-9_.-]+)"
_SCALAR = r"""(?:"([^"]+)"|'([^']+)'|([^\s#]+))"""
# `name: <scalar>` on one line.
_INLINE = re.compile(r"^  " + _NAME + r":[ \t]*" + _SCALAR + r"[ \t]*$", re.M)
# `name:` then an indented `version: <scalar>`, comments allowed between.
_BLOCK = re.compile(
    r"^  " + _NAME + r":[ \t]*\n(?:[ \t]*#.*\n)*[ \t]+version:[ \t]*" + _SCALAR, re.M
)
# Every NAMESPACED dependency key, whatever shape its value takes. Diffed
# against what the two patterns actually read, so an unreadable dependency is
# reported even when a readable one sits beside it in the same manifest -- a
# per-manifest "did anything parse?" test would have missed exactly that.
# Deliberately not matching `idf:`, which every manifest has and this tool
# never reads; counting it made the five components whose only dependency is
# idf look unreadable.
# Deliberately LOOSER than _NAME: anything namespace-shaped counts, including
# the near-misses _NAME rejects -- an uppercase namespace, a dot in it. If the
# reading patterns cannot take it, that is the case this cross-check exists to
# surface, and a key both patterns reject would otherwise be invisible to
# both.
_ANY_DEP = re.compile(r"^  ([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+):", re.M)

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
    # Revisions are numeric by convention, but the string comes from the
    # registry: a `~beta` must not raise ValueError and take the whole check
    # down with it.
    rev_n = int(rev) if rev.isdigit() else 0
    # No prerelease sorts above any prerelease: (1,) beats (0, ...). Within
    # prereleases, compare digit runs as numbers so rc10 follows rc2 rather
    # than preceding it the way a plain string compare would.
    pre = (1,) if not rest else (0, _prerelease_key(rest))
    return (*nums, rev_n, pre)


def _prerelease_key(tag: str) -> tuple:
    """Split a prerelease tag into comparable text/number runs.

    `rc2` -> (("rc", 0), ("", 2)), so rc2 < rc10. Numbers sort below text at
    the same position, which is semver's rule for a numeric identifier against
    an alphanumeric one.
    """
    return tuple(
        (m.group(1), int(m.group(2)) if m.group(2) else -1)
        for m in re.finditer(r"([^\d]*)(\d*)", tag)
        if m.group(0)
    )


def is_prerelease(v: str) -> bool:
    # A hyphen anywhere is a prerelease: Espressif's revision suffix uses `~`
    # and the build metadata separator `+` cannot precede one.
    return "-" in v


def manifest_deps(
    root: pathlib.Path, unparsed: list[str] | None = None
) -> dict[str, set[str]]:
    """Every third-party dependency and the ranges the tree declares for it.

    Appends to `unparsed` any manifest that declares dependencies none of the
    patterns could read. That is the failure worth shouting about: a dependency
    this misses is one nobody is watching.
    """
    found: dict[str, set[str]] = {}
    for f in sorted(root.rglob("idf_component.yml")):
        # Relative to root, not absolute: a checkout at .../work/build/espOS
        # would otherwise match "build" in every path and exclude the entire
        # tree, leaving a tool that reports nothing and looks content.
        rel = f.relative_to(root).parts
        if "managed_components" in rel or "build" in rel:
            continue
        # Explicit encoding: read_text() uses the locale, so on a non-UTF-8
        # machine every manifest containing an em dash would raise and take
        # the run down. The result must not depend on the host's locale.
        text = f.read_text(encoding="utf-8")
        read_here: set[str] = set()
        for pat in (_INLINE, _BLOCK):
            for match in pat.finditer(text):
                name = match.group(1)
                rng = next(g for g in match.groups()[1:] if g is not None)
                read_here.add(name)
                if name.startswith(OURS):
                    continue
                found.setdefault(name, set()).add(rng)
        if unparsed is not None:
            for name in sorted(set(_ANY_DEP.findall(text)) - read_here):
                unparsed.append(f"{f}: {name}")
    return found


def newest_published(name: str, *, opener=urllib.request.urlopen) -> str | None:
    """The newest non-prerelease version on the registry, or None."""
    try:
        with opener(f"{REGISTRY}/{name}", timeout=30) as r:
            data = json.load(r)
    # OSError covers urllib's URLError and HTTPError and a socket failing
    # mid-read; http.client's exceptions cover a truncated or malformed
    # response (IncompleteRead, BadStatusLine), which the previous list let
    # through; ValueError covers a body that is not JSON. None of these is a
    # component that moved, and the workflow now treats an uncaught exception
    # as a failed job, so any of them escaping would take the whole check down
    # over one unreachable component.
    except (OSError, http.client.HTTPException, ValueError):
        return None
    # External input: a registry that answers with a list, a string or an
    # error document must yield None, not an AttributeError that the workflow
    # now correctly treats as a crash.
    if not isinstance(data, dict):
        return None
    raw = data.get("versions")
    if not isinstance(raw, list):
        return None
    versions = [
        v["version"]
        for v in raw
        if isinstance(v, dict) and isinstance(v.get("version"), str) and v["version"]
    ]
    # No falling back to prereleases when there is no stable release: reporting
    # "behind, newest 2.0.0-rc1" would be advice to ship a release candidate.
    stable = [v for v in versions if not is_prerelease(v)]
    if not stable:
        return None
    return max(stable, key=parse_version)


def classify(ranges: set[str], newest: str | None) -> tuple[str, str]:
    """(state, detail). state is one of: current, behind, unpinned, unknown."""
    exact = {r[2:] for r in ranges if r.startswith("==")}
    all_exact = bool(exact) and len(exact) == len(ranges)

    if not all_exact:
        # Not exact: the solver takes the newest the range permits, whenever it
        # next runs. That is a property of the manifest, so it is reported even
        # when the registry is unreachable -- `unknown` is for a pin whose
        # standing we could not establish, and a range has no standing to
        # establish.
        tail = f"newest {newest}" if newest else "registry did not answer"
        return "unpinned", f"declared {', '.join(sorted(ranges))}, {tail}"

    if len(exact) > 1:
        # Ten manifests disagreeing about one component needs no registry.
        return "behind", f"pinned inconsistently: {', '.join(sorted(exact))}"

    pinned = exact.pop()
    if newest is None:
        return "unknown", f"pinned {pinned}, registry did not answer"
    if parse_version(pinned) < parse_version(newest):
        return "behind", f"pinned {pinned}, newest {newest}"
    return "current", f"pinned {pinned}"


def report(root: pathlib.Path, *, fetch=newest_published) -> list[dict]:
    rows = []
    unparsed: list[str] = []
    for name, ranges in sorted(manifest_deps(root, unparsed).items()):
        newest = fetch(name)
        state, detail = classify(ranges, newest)
        rows.append(
            {"name": name, "ranges": sorted(ranges), "newest": newest,
             "state": state, "detail": detail}
        )
    for f in unparsed:
        rows.append(
            {"name": f, "ranges": [], "newest": None, "state": "unparsed",
             "detail": "declared here but unreadable by this tool"}
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
    # branch on it: 1 = behind, unpinned or unparsed; 0 = everything current or
    # unknown. Unknown is NOT a failure -- a registry outage must not open an
    # issue claiming a component moved -- and unparsed IS, because a dependency
    # this tool cannot read is one nobody is watching.
    actionable = ("behind", "unpinned", "unparsed")
    return 1 if any(r["state"] in actionable for r in rows) else 0


if __name__ == "__main__":
    sys.exit(main())
