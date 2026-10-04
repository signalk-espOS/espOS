# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""Rewrite one exact pin across every manifest that declares it.

The other half of espos_registry_updates.py: that one answers "is anything
newer than what we pinned?", this one takes the bump so the weekly job can
open a pull request instead of an issue nobody acts on.

Why a tool and not `sed`: a component is declared in two shapes and three
scalar spellings, the same component is pinned in several manifests that must
move together, and a rewrite that silently matched nothing looks exactly like
a rewrite that worked. So the patterns come from the reporting tool rather
than being written a second time -- two regexes for one file format drift
apart, and every manifest the pair disagreed about would be invisible to both.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from espos_registry_updates import _BLOCK, _INLINE, parse_version

SKIP = ("managed_components", "build")


def _scalar_span(match) -> tuple[int, int]:
    """Span of whichever of the three scalar alternatives matched."""
    # Groups are (name, "quoted", 'quoted', bare); exactly one of the last
    # three is not None. Replacing only that span keeps the author's quoting,
    # and the surrounding comments, untouched.
    for i in (2, 3, 4):
        if match.group(i) is not None:
            return match.span(i)
    raise AssertionError("a matched dependency with no scalar")


def bump_text(text: str, name: str, old: str, new: str) -> tuple[str, int]:
    """Return (rewritten, count). Only `==old` is replaced, never a range."""
    edits: list[tuple[int, int]] = []
    for pat in (_INLINE, _BLOCK):
        for m in pat.finditer(text):
            if m.group(1) != name:
                continue
            start, end = _scalar_span(m)
            if text[start:end] == f"=={old}":
                edits.append((start, end))
    # Right to left so earlier spans keep their offsets.
    out = text
    for start, end in sorted(edits, reverse=True):
        out = out[:start] + f"=={new}" + out[end:]
    return out, len(edits)


def manifests(root: pathlib.Path):
    for f in sorted(root.rglob("idf_component.yml")):
        if any(p in SKIP for p in f.relative_to(root).parts):
            continue
        yield f


def apply(root: pathlib.Path, name: str, old: str, new: str) -> list[pathlib.Path]:
    """Rewrite every `==old` for `name`. Returns the files changed."""
    changed = []
    for f in manifests(root):
        text = f.read_text(encoding="utf-8")
        out, n = bump_text(text, name, old, new)
        if n:
            f.write_text(out, encoding="utf-8")
            changed.append(f)
    return changed


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=".", type=pathlib.Path)
    ap.add_argument("--name", required=True, help="e.g. espressif/mdns")
    ap.add_argument("--from", dest="old", required=True, help="the pin to replace")
    ap.add_argument("--to", dest="new", required=True)
    args = ap.parse_args(argv)

    # --from is required and matched exactly, so a rerun over an already-bumped
    # tree changes nothing and says so, rather than being an edit whose effect
    # depends on when it ran.
    if parse_version(args.new) <= parse_version(args.old):
        print(
            f"espos_apply_bump: {args.new} is not newer than {args.old}",
            file=sys.stderr,
        )
        return 2

    changed = apply(args.root, args.name, args.old, args.new)
    if not changed:
        # The failure this tool exists to make loud. A rewrite that matched
        # nothing is indistinguishable from one that worked, and the workflow
        # downstream would commit an empty diff and open a pull request
        # claiming a bump it never made.
        print(
            f"espos_apply_bump: no manifest pins {args.name} =={args.old}",
            file=sys.stderr,
        )
        return 1
    for f in changed:
        print(f"bumped {args.name} {args.old} -> {args.new} in {f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
