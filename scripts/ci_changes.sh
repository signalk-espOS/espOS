#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Say whether a change can affect a firmware build, for ci.yml to skip the
# builds, host tests and fuzzers when it cannot.
#
#   scripts/ci_changes.sh <base> [<head>]   # prints code=true or code=false
#
# <base> is a commit or a ref, and must be an ancestor of <head>. Only
# documentation counts as "cannot": Markdown anywhere, docs/ and mkdocs.yml.
# Nothing in the build reads them, and the cheap jobs that check them (Kconfig
# names in prose, tutorial caps, the docs site) still run.
#
# Anything this script cannot prove says code=true: a base that is missing
# (none found, or not fetched), one that is not an ancestor (a force-push, or
# a base branch that moved on since the merge commit was made), a git error,
# or an empty diff. A skipped build that should have run is the failure that
# matters; an unneeded one only costs time.
set -euo pipefail

base="${1:-}"
head="${2:-HEAD}"

if [ -z "$base" ] \
   || ! git rev-parse -q --verify "$base^{commit}" >/dev/null \
   || ! git merge-base --is-ancestor "$base" "$head" 2>/dev/null; then
    echo "code=true"
    exit 0
fi

# --no-renames: a rename lists only its new name, so moving a source file
# into docs/ would otherwise read as a documentation change.
if ! files=$(git diff --no-renames --name-only "$base" "$head") || [ -z "$files" ]; then
    echo "code=true"
    exit 0
fi

if printf '%s\n' "$files" | grep -qvE '^docs/|^mkdocs\.yml$|\.md$'; then
    echo "code=true"
else
    echo "code=false"
fi
