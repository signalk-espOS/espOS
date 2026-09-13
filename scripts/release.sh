#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Cut an espOS release: bump version.txt, commit, and tag.
#
#   scripts/release.sh 0.7.0
#   scripts/release.sh 0.7.0 --dry-run
#
# It does NOT push. Review `git show` and `git tag -n99 v<version>`, then push
# the branch and the tag yourself.
#
# Why a script for two commands: a tag and version.txt that disagree make a
# device report a version that matches no release, and the build only warns
# about it. Doing both from one place is the cheapest way to keep them equal.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

usage() { echo "usage: scripts/release.sh <major.minor.patch> [--dry-run]" >&2; exit 2; }

version="${1:-}"
dry_run=""
[ "$#" -ge 1 ] || usage
[ "$#" -le 2 ] || usage
if [ "$#" -eq 2 ]; then
    [ "$2" = "--dry-run" ] || usage
    dry_run=1
fi

if ! printf '%s' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "release.sh: '$version' is not major.minor.patch" >&2
    exit 1
fi

tag="v$version"

if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    echo "release.sh: $tag already exists" >&2
    exit 1
fi

# A release cut from a dirty tree is a release nobody can reproduce.
if [ -n "$(git status --porcelain)" ]; then
    echo "release.sh: working tree is not clean" >&2
    git status --short >&2
    exit 1
fi

current=$(git rev-parse --abbrev-ref HEAD)
echo "release.sh: $current at $(git rev-parse --short HEAD) → $tag"

# The range the release notes cover. With no previous tag this is the first
# release, and the whole history is what is new -- a first tag used to get no
# notes at all, which is the one release where "what is this?" most needs an
# answer.
previous=$(git describe --tags --abbrev=0 2>/dev/null || true)
if [ -n "$previous" ]; then
    notes_from="$previous"
    notes_label="Changes since $previous"
else
    notes_from=""
    notes_label="Changes in this first release"
fi

echo
echo "$notes_label:"
if [ -n "$notes_from" ]; then
    git log --no-merges --pretty='  %s' "$notes_from..HEAD"
else
    git log --no-merges --pretty='  %s' HEAD
fi
echo

if [ -n "$dry_run" ]; then
    echo "release.sh: --dry-run, stopping before the commit"
    exit 0
fi

printf '%s\n' "$version" > version.txt
git add version.txt

# The manifests move with version.txt, and docs/releasing.md says so: every
# component carries the release version, and every espOS-to-espOS dependency
# carries ^<release version>. Doing it here rather than by hand because the
# failure is quiet and total -- pre-1.0, ^0.7.0 EXCLUDES 0.8.0, so ranges left
# behind publish a set of components the dependency solver cannot install
# together, and nothing notices until someone tries.
for manifest in components/*/idf_component.yml; do
    python3 - "$manifest" "$version" <<'PYEOF'
import re, sys

path, version = sys.argv[1], sys.argv[2]
with open(path) as f:
    lines = f.readlines()

# Line-based rather than one regex over the whole file. A regex that walks
# from a dependency key to the next `version:` runs PAST a dependency that has
# none -- an espOS component pinned only by override_path, say -- and rewrites
# the next dependency's pin instead. That turns espressif/esp_hosted into
# ^0.8.0, which is not a version of anything.
own_done = False
in_sibling = False          # inside a signalk-espos/espos_* block
sibling_key = None
sibling_indent = 0
bumped_sibling = False
out = []

def indent_of(line):
    return len(line) - len(line.lstrip(" \t"))

for line in lines:
    stripped = line.strip()
    # The component's own version: the first top-level `version:`.
    if not own_done and re.match(r'^version: *"', line):
        out.append(re.sub(r'"[^"]*"', f'"{version}"', line, count=1))
        own_done = True
        continue
    if in_sibling:
        # The block ends at the first line indented no deeper than its key.
        if stripped and indent_of(line) <= sibling_indent:
            if not bumped_sibling:
                sys.exit(f"{path}: {sibling_key} has no version: to bump")
            in_sibling = False
        elif re.match(r'^[ \t]+version: *"', line):
            out.append(re.sub(r'"[^"]*"', f'"^{version}"', line, count=1))
            bumped_sibling = True
            continue
    m = re.match(r'^([ \t]+)(signalk-espos/espos_[a-z_]+): *$', line)
    if m:
        if in_sibling and not bumped_sibling:
            sys.exit(f"{path}: {sibling_key} has no version: to bump")
        in_sibling, sibling_key = True, m.group(2)
        sibling_indent, bumped_sibling = len(m.group(1)), False
    out.append(line)

if in_sibling and not bumped_sibling:
    sys.exit(f"{path}: {sibling_key} has no version: to bump")
if not own_done:
    sys.exit(f"{path}: no top-level version: found")

with open(path, "w") as f:
    f.writelines(out)
PYEOF
    git add "$manifest"
done
# version.txt may already hold this version -- the first tag of a version
# developed under it, or a re-cut after an aborted run. Nothing to commit is
# then correct, not an error: tag the commit that is already there.
if git diff --cached --quiet; then
    echo "release.sh: version.txt already $version, tagging HEAD"
    notes_end="HEAD"
else
    git commit -q -m "chore: release $version"
    # The release commit is not a change in the release; end the notes before it.
    notes_end="HEAD^"
fi

# Annotated, not lightweight: an annotated tag carries who cut it and when,
# and is what `git describe` prefers.
if [ -n "$notes_from" ]; then
    notes=$(git log --no-merges --pretty='- %s' "$notes_from..$notes_end")
else
    notes=$(git log --no-merges --pretty='- %s' "$notes_end")
fi
git tag -a "$tag" -m "espOS $version" -m "$notes"

echo
echo "release.sh: tagged $tag at $(git rev-parse --short HEAD)"
echo "Review, then push both:  git push origin $current && git push origin $tag"
echo "Consumers: bump their espos submodule to this commit and say so —"
echo "  git -C espos fetch --tags && git -C espos checkout $tag"
echo "  git commit -m 'chore: bump espos to $tag'"
