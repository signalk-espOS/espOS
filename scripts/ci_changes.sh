#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Say whether a change can affect a firmware build, for ci.yml to skip the
# builds, host tests and fuzzers when it cannot.
#
#   scripts/ci_changes.sh <base> [<head>]   # prints code=true or code=false
#
# <base> is a commit or a ref; the comparison is against the merge base, the
# way a pull request's diff is shown. Only documentation counts as "cannot":
# Markdown anywhere, docs/ and mkdocs.yml. Nothing in the build reads them,
# and the cheap jobs that check them (Kconfig names in prose, tutorial caps,
# the docs site) still run.
#
# Anything this script cannot prove says code=true: a base that is missing
# (a new branch's push has an all-zero "before"), a history too shallow for a
# merge base, or an empty diff. A skipped build that should have run is the
# failure that matters; an unneeded one only costs time.
set -euo pipefail

base="${1:?base commit or ref}"
head="${2:-HEAD}"

if ! git rev-parse -q --verify "$base^{commit}" >/dev/null \
   || ! git merge-base "$base" "$head" >/dev/null 2>&1; then
    echo "code=true"
    exit 0
fi

files=$(git diff --name-only "$base...$head")
if [ -z "$files" ]; then
    echo "code=true"
    exit 0
fi

if printf '%s\n' "$files" | grep -qvE '^docs/|^mkdocs\.yml$|\.md$'; then
    echo "code=true"
else
    echo "code=false"
fi
