#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Which releases a firmware's browser-download mirror has to keep.

The hosted flasher offers, per board, the three newest stable releases and up
to two prereleases newer than the newest stable one
(signalk-espos-manager web/src/flash/catalogue.ts, KEEP_STABLE / KEEP_BETA).
Every one of those needs its images on the mirror branch, because a browser
can only download from there. So the mirror keeps exactly that set, plus the
tag being published -- a release is always mirrored when it is published, even
an old one, which the next publish then prunes.

A fixed "newest N tags" is not the same set: with prereleases between stable
releases it prunes a stable release the flasher still offers.

Channel comes from GitHub's prerelease flag, as in the registry's index. Tags
must be plain vX.Y.Z (release-firmware.yml refuses anything else); a release
whose tag is not is ignored here, since the registry cannot index it anyway.

Usage: espos_release_window.py RELEASES_JSON TAG
  RELEASES_JSON  the output of `gh api repos/OWNER/REPO/releases --paginate`
                 (a JSON array, or several concatenated), or - for stdin
  TAG            the tag being published, or "" for the window alone
                 (a backfill publishes only tags already inside it)
Prints the tags to keep, one per line, newest first.
"""

from __future__ import annotations

import json
import re
import sys
from typing import Iterable

KEEP_STABLE = 3
KEEP_PRERELEASE = 2

PLAIN_TAG = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")


def version_key(tag: str) -> tuple[int, int, int] | None:
    """(major, minor, patch) for a plain vX.Y.Z tag, None for anything else."""
    m = PLAIN_TAG.match(tag)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def window(releases: Iterable[dict], current: str = "") -> list[str]:
    """Tags to keep, newest first. `current`, when given, is always among them."""
    stable: list[tuple[tuple[int, int, int], str]] = []
    pre: list[tuple[tuple[int, int, int], str]] = []
    for r in releases:
        if r.get("draft"):
            continue
        tag = r.get("tag_name", "")
        key = version_key(tag)
        if key is None:
            continue
        (pre if r.get("prerelease") else stable).append((key, tag))
    stable.sort(reverse=True)
    pre.sort(reverse=True)

    keep = stable[:KEEP_STABLE]
    newest_stable = stable[0][0] if stable else None
    keep += [p for p in pre if newest_stable is None or p[0] > newest_stable][
        :KEEP_PRERELEASE
    ]
    tags = [tag for _, tag in sorted(keep, reverse=True)]
    if current and current not in tags:
        tags.insert(0, current)
    return tags


def load_releases(text: str) -> list[dict]:
    """`gh api --paginate` prints one JSON array per page, back to back."""
    out: list[dict] = []
    decoder = json.JSONDecoder()
    pos = 0
    text = text.strip()
    while pos < len(text):
        value, end = decoder.raw_decode(text, pos)
        out.extend(value if isinstance(value, list) else [value])
        pos = end
        while pos < len(text) and text[pos].isspace():
            pos += 1
    return out


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip().split("\n\n")[-1], file=sys.stderr)
        return 2
    source = sys.stdin.read() if argv[1] == "-" else open(argv[1]).read()
    for tag in window(load_releases(source), argv[2]):
        print(tag)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
