#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
#
# Publish release images to a branch a web page can read, and undo it.
#
# GitHub serves release downloads with no Access-Control-Allow-Origin, on the
# 302 and on its target, so a browser cannot fetch firmware from a release at
# all. raw.githubusercontent.com does send `access-control-allow-origin: *`
# and honours Range, so an orphan branch of the same repository, laid out as
# <tag>/<asset>, is a CORS-readable mirror of what the releases carry. The
# registry advertises a file there only when it exists under exactly that name
# (signalk-espOS/registry scripts/reindex.mjs).
#
# The branch is a publishing surface, not history: one commit, rewritten on
# every publish, holding the tags the flasher can still offer. Publishing is
# additive -- a run for an old tag must not delete a newer one -- and pruning
# follows an explicit keep list (tools/espos_release_window.py).
#
#   espos_mirror_release_assets.sh publish  --remote URL --staged DIR --keep FILE
#                                           [--branch B] [--state DIR] [--readme FILE]
#   espos_mirror_release_assets.sh rollback --remote URL [--branch B] [--state DIR]
#
# --staged holds one directory per tag to publish (<tag>/<asset>...); each
# replaces that tag's directory on the branch. --keep lists the tags to keep,
# one per line; every other top-level directory is pruned, but a staged tag is
# always kept. --state (default $RUNNER_TEMP) carries what `rollback` needs
# from `publish`: the tip it replaced, the commit it pushed, and a clone of the
# branch before the push. `rollback` undoes one run's publish, leased against
# the commit that run pushed, so it never discards a later publish.
#
# Why each piece is the way it is, found by running it:
#   * --force-with-lease, never --force. A concurrency group is not a
#     guarantee (GitHub keeps one pending job per group and drops the rest),
#     so the push itself must refuse to overwrite a branch that moved.
#   * Retry only when the branch MOVED. A network, auth or hook failure fails
#     the same command; retrying those blames concurrency for something else.
#   * The snapshot is a full clone, never --depth: a no-history branch cloned
#     shallow cannot be pushed back ("shallow update not allowed").
#   * The snapshot is re-taken after every re-read and must match the base, or
#     a rollback restores a tip from before another release existed and
#     deletes its directory.
#   * The base is recorded before anything that can fail, and the pushed
#     commit before the push, so a run killed mid-push can still be undone and
#     a failed clone never turns "restore" into "delete the branch".
#   * Only `ls-remote` exit 2 means "no such branch"; anything else means the
#     remote could not be read, and publishing over it would erase every other
#     release's images.
set -euo pipefail

die() { echo "::error::$*" >&2; exit 1; }

cmd="${1:-}"; shift || true
remote="" staged="" keep_file="" branch="release-assets" readme=""
state="${RUNNER_TEMP:-}"
while [ $# -gt 0 ]; do
    case "$1" in
        --remote) remote="$2"; shift 2 ;;
        --staged) staged="$2"; shift 2 ;;
        --keep) keep_file="$2"; shift 2 ;;
        --branch) branch="$2"; shift 2 ;;
        --state) state="$2"; shift 2 ;;
        --readme) readme="$2"; shift 2 ;;
        *) die "unknown argument '$1'" ;;
    esac
done
[ -n "$remote" ] || die "--remote is required"
[ -n "$state" ] || die "--state is required outside GitHub Actions"
case "$branch" in
    ""|-*|*..*|*[!A-Za-z0-9._/-]*) die "refusing branch name '$branch'" ;;
esac
mkdir -p "$state"
work="$state/mirror-work"
snapshot="$state/mirror-before"
base_file="$state/mirror-base"
pushed_file="$state/mirror-pushed"

valid_tag() {
    [[ "$1" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

publish() {
    [ -d "$staged" ] || die "--staged '$staged' is not a directory"
    [ -r "$keep_file" ] || die "--keep '$keep_file' cannot be read"
    # Absolute before anything changes directory: every path below is read
    # from inside the work tree. A relative --keep that stopped resolving made
    # every tag look unlisted, and each publish pruned all the others.
    staged=$(cd "$staged" && pwd)
    keep_file=$(realpath -- "$keep_file")
    if [ -n "$readme" ]; then
        [ -r "$readme" ] || die "--readme '$readme' cannot be read"
        readme=$(realpath -- "$readme")
    fi

    local tags=() t
    while IFS= read -r t; do
        valid_tag "$t" || die "refusing to publish '$t': tags must be vX.Y.Z"
        compgen -G "$staged/$t/*.bin" >/dev/null || die "nothing to publish for $t"
        tags+=("$t")
    done < <(find "$staged" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V)
    [ "${#tags[@]}" -gt 0 ] || die "no tag directories under $staged"

    rm -rf "$work" "$snapshot" "$pushed_file"
    mkdir -p "$work"
    cd "$work"
    git init -q -b "$branch"
    git config user.name "github-actions[bot]"
    git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

    local base=""
    take_snapshot() {
        # The base FIRST: the rollback deletes the branch when there is none,
        # so skipping this write on a failed clone would delete a branch that
        # existed.
        printf '%s' "$base" > "$base_file"
        rm -rf "$snapshot"
        [ -n "$base" ] || return 0
        if ! git clone -q --single-branch --branch "$branch" "$remote" "$snapshot" 2>/dev/null; then
            echo "::warning::could not snapshot $branch; a failure after the push" \
                 "will leave it advertising ${tags[*]}"
            return 0
        fi
        local head
        head=$(git -C "$snapshot" rev-parse HEAD)
        if [ "$head" != "$base" ]; then
            echo "::warning::$branch moved between reading it ($base) and" \
                 "snapshotting it ($head); discarding the snapshot rather than" \
                 "risk a rollback to the wrong commit"
            rm -rf "$snapshot"
        fi
    }

    read_branch() {
        git fetch -q --depth 1 "$remote" "$branch"
        git checkout -q FETCH_HEAD -- .
        base=$(git rev-parse FETCH_HEAD)
    }

    local lsr=0
    git ls-remote --exit-code --heads "$remote" "$branch" >/dev/null 2>&1 || lsr=$?
    case "$lsr" in
        0) echo "$branch exists; adding ${tags[*]} to it"; read_branch ;;
        2) echo "$branch does not exist yet; creating it" ;;
        *) die "could not read $branch (git ls-remote exited $lsr). Refusing to" \
               "publish: pushing now would delete the other releases' images." ;;
    esac
    take_snapshot

    stage() {
        if [ -n "$readme" ]; then cp "$readme" ./README.md; fi
        local t d
        for t in "${tags[@]}"; do
            rm -rf -- "${t:?}"
            mkdir -p "$t"
            cp "$staged/$t"/*.bin "$t/"
        done
        # Prune what the flasher can no longer offer. Only top-level
        # directories that are not dotfiles, and never a tag being published.
        # The keep list is read into memory once: a grep per directory reports
        # an unreadable file exactly like an unlisted tag.
        local keep=() k
        mapfile -t keep < "$keep_file"
        while IFS= read -r d; do
            for k in "${keep[@]}" "${tags[@]}"; do
                [ "$k" = "$d" ] && continue 2
            done
            echo "pruning $d"
            rm -rf -- "$d"
        done < <(find . -mindepth 1 -maxdepth 1 -type d -not -name '.*' -printf '%f\n' | sort -V)
    }

    push() {
        git add -A
        git commit -q -m "firmware for ${tags[*]}"
        # Recorded BEFORE the push: a run killed mid-push may have landed it,
        # and the rollback leases against this. A commit that never landed is a
        # stale lease the remote refuses, which makes the rollback a no-op.
        git rev-parse HEAD > "$pushed_file"
        # An empty lease means "expect the branch not to exist".
        git push -q --force-with-lease="$branch:$base" "$remote" "HEAD:refs/heads/$branch"
    }

    stage
    local attempt=1 tip
    until push; do
        tip=$(git ls-remote "$remote" "refs/heads/$branch" 2>/dev/null | cut -f1) || true
        if [ "$tip" = "$(git rev-parse HEAD)" ]; then
            echo "$branch is already at this run's commit; the push landed"
            break
        fi
        if [ "$tip" = "$base" ]; then
            die "the push failed but $branch is still at '${tip:-absent}'; nothing" \
                "moved, so this is not a lease rejection (check auth, network," \
                "branch protection above)"
        fi
        if [ -z "$tip" ] && [ -n "$base" ]; then
            die "the push failed and $branch could not be read afterwards;" \
                "refusing to retry"
        fi
        [ "$attempt" -lt 3 ] || die "$branch moved under this run 3 times; giving" \
            "up rather than discard another release's images"
        echo "::warning::$branch moved since it was read (now ${tip:-deleted});" \
             "re-reading and re-staging, attempt $((attempt + 1))"
        attempt=$((attempt + 1))
        # A fresh orphan per attempt: a fixed name fails the second time round
        # ("a branch named ... already exists").
        git checkout -q --orphan "rebuild-$attempt"
        git rm -rq --cached . >/dev/null 2>&1 || true
        find . -mindepth 1 -maxdepth 1 -not -name '.git' -exec rm -rf {} +
        base=""
        if [ -n "$tip" ]; then read_branch; fi
        take_snapshot
        stage
    done

    local t f
    for t in "${tags[@]}"; do
        for f in "$t"/*.bin; do echo "published $branch/$f"; done
    done
}

rollback() {
    if [ ! -f "$pushed_file" ]; then
        echo "the mirror did not push; nothing to roll back"
        return 0
    fi
    local pushed base
    pushed=$(cat "$pushed_file")
    base=$(cat "$base_file" 2>/dev/null || true)
    # Every undo is leased against the commit THIS run pushed: if the branch
    # moved on since, a later release published and its images stand.
    if [ -z "$base" ]; then
        echo "$branch did not exist before this run; deleting it"
        if git push -q --force-with-lease="$branch:$pushed" "$remote" ":refs/heads/$branch"; then
            echo "$branch deleted"
        else
            echo "::warning::$branch moved since this run pushed; leaving it"
        fi
        return 0
    fi
    if [ ! -d "$snapshot/.git" ]; then
        echo "::warning::no snapshot to restore; $branch may advertise images" \
             "whose release has none"
        return 0
    fi
    if git -C "$snapshot" push -q --force-with-lease="$branch:$pushed" \
            "$remote" "HEAD:refs/heads/$branch"; then
        echo "$branch restored to $base, its state before this run"
    else
        echo "::warning::$branch moved since this run pushed; leaving it rather" \
             "than discard a later release's images"
    fi
}

case "$cmd" in
    publish) publish ;;
    rollback) rollback ;;
    *) die "usage: $0 publish|rollback --remote URL ..." ;;
esac
