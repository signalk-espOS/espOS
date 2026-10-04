# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Report ESP-IDF releases newer than the one `.idf-version` names.

espos_registry_updates.py watches the components; nothing watched IDF itself.
CI, the firmware builds and every consumer read `.idf-version`, so a patch
release with a fix for this hardware sat unnoticed until someone happened to
look.

Two answers, because they need different work:
  patch  -- a newer X.Y.Z on the same X.Y. `.idf-version` moves; the
            `idf: ">=X.Y.0,<X.(Y+1).0"` range in the manifests already allows it.
  minor  -- a newer X.Y (or major). The manifests' idf range has to move too,
            and that is a statement to every consumer about what espOS supports.

Release tags only (vX.Y or vX.Y.Z). Betas, release candidates and `-dev` tags
are not releases, and reporting one would invite a move to it.

Reads tags with `git ls-remote`, not the GitHub API: no token, no rate limit,
and git is on every runner. Reports only.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys

UPSTREAM = "https://github.com/espressif/esp-idf"

_RELEASE = re.compile(r"^v(\d+)\.(\d+)(?:\.(\d+))?$")


def parse_release(tag: str) -> tuple[int, int, int] | None:
    """`v6.1` -> (6, 1, 0); anything that is not a release tag -> None."""
    m = _RELEASE.match(tag.strip())
    if not m:
        return None
    return int(m[1]), int(m[2]), int(m[3] or 0)


def fmt(v: tuple[int, int, int]) -> str:
    # IDF tags a .0 release as vX.Y, so print it the way it is tagged.
    return f"v{v[0]}.{v[1]}" if v[2] == 0 else f"v{v[0]}.{v[1]}.{v[2]}"


def compare(pinned: str, tags: list[str]) -> dict:
    """What is newer than `pinned`, split into same-minor and later-minor.

    Raises ValueError when `pinned` is not a release tag: a pin this cannot
    read must fail the job, not report "current".
    """
    have = parse_release(pinned)
    if have is None:
        raise ValueError(f".idf-version {pinned!r} is not a release tag like v6.0.3")
    releases = {v for v in (parse_release(t) for t in tags) if v is not None}
    patch = max((v for v in releases if v[:2] == have[:2] and v > have), default=None)
    minor = max((v for v in releases if v[:2] > have[:2]), default=None)
    return {
        "pinned": fmt(have),
        "patch": fmt(patch) if patch else None,
        "minor": fmt(minor) if minor else None,
    }


def upstream_tags(url: str = UPSTREAM) -> list[str]:
    out = subprocess.run(
        ["git", "ls-remote", "--tags", "--refs", url],
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    ).stdout
    return [line.split("refs/tags/", 1)[1] for line in out.splitlines() if "refs/tags/" in line]


def render(result: dict) -> str:
    lines = [f"pinned  {result['pinned']}  (.idf-version)"]
    if result["patch"]:
        lines.append(f"patch   {result['patch']}  move .idf-version; the manifests' idf range already allows it")
    if result["minor"]:
        lines.append(
            f"minor   {result['minor']}  needs .idf-version AND the idf range in the "
            "manifests, which tells every consumer what espOS supports"
        )
    if not result["patch"] and not result["minor"]:
        lines.append("current no newer ESP-IDF release")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=pathlib.Path, default=pathlib.Path("."))
    args = ap.parse_args(argv)
    pinned = (args.root / ".idf-version").read_text().strip()
    result = compare(pinned, upstream_tags())
    print(render(result))
    # 1 means "something to act on", mirroring espos_registry_updates.py, so
    # the workflow can tell it from a crash (anything above 1).
    return 1 if result["patch"] or result["minor"] else 0


if __name__ == "__main__":
    sys.exit(main())
