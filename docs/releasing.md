# Releasing

espOS is consumed as a git submodule (`espos/` in a firmware project), so a
"release" is a tag other repositories can point at, and `version.txt` is what
the device reports.

## Cutting one

Nobody edits `CHANGELOG.md` or a version number by hand.
[release-please](https://github.com/googleapis/release-please)
(`.github/workflows/release-please.yml`) runs on every push to `main` and
keeps one pull request open, **`chore: release <version>`**, which

* prepends the new `CHANGELOG.md` section, built from the Conventional
  Commit subjects merged since the last release — the PR titles, because
  pull requests are squash-merged (`feat` → Added, `fix` → Fixed, `perf` and
  `refactor` → Changed; `docs`, `build`, `ci`, `test` and `chore` stay out);
* bumps `version.txt` and every espOS version in the component manifests
  (below);
* picks the version: a `feat` raises the minor, a `fix` the patch, and a
  breaking change (`!` after the type, or a `BREAKING CHANGE:` line in the
  squashed body) the minor as well while espOS is below 1.0.

**Merging that pull request is the release.** The next run tags the merge
commit `v<version>`, creates the GitHub release with the new section as its
notes, and publishes the components to the registry. Until a maintainer
merges it, nothing is released. The release PR can be edited before merging
— lead the notes with anything that requires a consumer to change its own
code.

There are no binaries to attach — espOS is source consumed as a submodule,
and the tag remains the deliverable; the release is a readable front page
for it.

The `[Unreleased]` block further down `CHANGELOG.md` is the hand-written
changelog from before the switch. `last-release-sha` in
`release-please-config.json` keeps the first generated section from repeating
it; when the 0.8.0 release PR opens, fold that block into its section by hand
and remove `last-release-sha`.

## What a device reports

`espos_project_prologue()` sets `PROJECT_VER` from `git describe --tags
--dirty --always`, falling back to `version.txt` when the checkout has no
tags (a tarball, or a release that has not been tagged yet). Firmwares built
on espOS get the same treatment for their own version, since they call the
same prologue. So:

| Build | `GET /api/v1/system/info` reports |
|---|---|
| the tagged commit | `0.7.0` |
| three commits later | `0.7.0-3-gabc1234` |
| with uncommitted changes | `0.7.0-3-gabc1234-dirty` |
| no tags at all | `0.7.0` (from version.txt) |

That distinction is the whole point of tagging. Without it every build
between two releases reports the same number, and "which firmware is on that
box" has no answer short of comparing binaries. The build warns when the
nearest tag and `version.txt` disagree.

## Consumers

A firmware project pins espOS by submodule commit. Bump it to a *tag*, and
say which one:

```sh
git -C espos fetch --tags
git -C espos checkout v0.7.0
git commit -am "chore: bump espos to v0.7.0"
```

The submodule still records a SHA — that is how submodules work — but the
commit message makes the release readable in `git log`, and
`git -C espos describe --tags` on any checkout then answers which espOS is
in it. A bump commit that says `bump espos to c6fd455` answers nothing
without a second repository to hand.

Consumers version themselves independently; espOS's version is not theirs.

## Versioning

Semantic-ish, judged against what a *consumer firmware* sees:

* **patch** — fixes, docs, internal changes. A consumer bumps and rebuilds.
* **minor** — new components, new config keys, new API endpoints. Additive:
  a consumer bumps and rebuilds, and may then use the new thing.
* **major** — a consumer has to change its own code: a removed or renamed
  public function, a changed struct field, a config key that no longer
  exists, an `/api/v1` change.

espOS is pre-1.0, so minor is doing the work major will do later. Say plainly
in the release notes when a bump requires consumer changes — that is the
number people actually need.

## Registry publishing

Every `components/espos_*` directory is also a component on the [Espressif
Component Registry](https://components.espressif.com), under the
`signalk-espos` namespace: `signalk-espos/espos_config`, `signalk-espos/espos_sk`, and so on. A firmware
that does not want the submodule adds what it needs and the component
manager pulls the rest:

```sh
idf.py add-dependency "signalk-espos/espos_sk^0.7"
```

`espos_sk`'s manifest names `espos_config`, `espos_httpd`, `espos_wifi` and
`espos_health` as dependencies, so that one line installs the core. The
registry names each download `signalk-espos__<name>` in the build; a component's
own `REQUIRES espos_config` still resolves, because the component manager
maps a short name onto the namespaced component when only that one exists.

### One version for everything

Every manifest carries `version:` equal to `version.txt`, and every
dependency between espOS components is `^<that version>` with an
`override_path` to the sibling directory. The `override_path` is what an
in-tree build — and a firmware that vendors espOS as a submodule — uses: the
manager takes the checkout next to the manifest and never asks the registry
for an espOS component. The version range is what a registry consumer sees,
and lockstep versions keep it from ever mixing two espOS releases in one
firmware.

Lockstep is maintained by the release PR, not by hand. release-please bumps
`version.txt` itself, and bumps a manifest because two things hold:

* the manifest is listed under `extra-files` in `release-please-config.json`
  (type `generic`);
* each espOS version line in it — the component's own `version:` and every
  `signalk-espos/espos_*` dependency's `version:` — sits inside a marker
  block:

  ```yaml
  # x-release-please-start-version
  version: "0.7.0"
  # x-release-please-end
  ```

  The generic updater rewrites every version inside such a block, keeping the
  `^` of a range. Blocks wrap single lines on purpose: one around the
  dependencies would rewrite the `idf:` and `espressif/*` pins as well.

Either half missing fails quietly: pre-1.0, `^0.7.0` excludes `0.8.0`, so a
release that leaves one manifest or one range behind publishes components that
cannot be installed together. `scripts/check_release_markers.py` fails CI when
a manifest is not listed, a version line is not marked, or a block holds
anything but one version line — a new component copies an existing manifest's
markers and adds its path to the config.

The manifests are the registry's contract; a manifest that fails to pack
fails the release. CI runs `compote component pack` for every component on
each pull request, and the tag check that compares the tag to `version.txt`
covers the manifests as well.

### Publishing a release

`.github/workflows/publish.yml` is called by `release-please.yml` once
merging a release PR has created the `v*` tag. It is called rather than
triggered by the tag: a tag release-please creates with the workflow's own
token starts no other workflow. It re-checks that the tag, `version.txt` and
every manifest agree — a registry version is immutable, and that check is
worth repeating rather than assuming the release PR had the right numbers —
then uploads each component with `compote component upload`, taking the
registry token from the `IDF_COMPONENT_API_TOKEN` repository secret. (The
component manager reads it from the environment under that exact name; there
is no `--token` flag.)

The `signalk-espos` namespace is registered on
[components.espressif.com](https://components.espressif.com); the token has to
belong to an account that owns it. Without the secret even a dry run fails on
the first component.

Upload order is computed, not written down: the registry resolves a
component's dependencies when it accepts the upload, so a component must not
arrive before the ones it names. `scripts/registry_order.py` topologically
sorts the manifests, and `--check` fails the release *before* anything is
uploaded, since half the components published is the one state that cannot be
rolled back. This paragraph used to carry a hand-written list of eleven
components while the tree had nineteen; every component added after it was
written was missing from it.

A registry version is immutable; `compote component upload --dry-run` (needs
the token) validates without creating one, and is the right rehearsal for a
first publish or a manifest change. A published version that turns out wrong
is yanked with a message, never deleted, and fixed by the next patch release.

What the registry ships is the packed archive: the component directory
minus `build/`, `sdkconfig*`, `managed_components/`, `dependencies.lock`
and the manager's own defaults (`.git`, `__pycache__`, ...). Anything a
component needs at build time — `espos_config`'s generator under `tools/`,
`espos_httpd`'s `www/index.html`, the `config/*.json` descriptors — lives
inside the component directory for exactly this reason.

## Building a consumer's firmware

`.github/workflows/build-firmware.yml` is reusable: a firmware built on espOS
calls it and keeps a workflow of about fifteen lines rather than two hundred.

```yaml
jobs:
  firmware:
    uses: signalk-espOS/espOS/.github/workflows/build-firmware.yml@main
    with:
      target: esp32p4
      name: p4-cockpit
      require: storage      # partitions the merged image must contain
    secrets:
      signing_key: ${{ secrets.SIGNING_KEY_PEM }}
```

It reads the IDF pin from the consumer's own `.idf-version` (or the espOS
submodule's), builds, merges the flash images with
`tools/espos_merge_firmware.py`, and stages two assets: the merged image for
a cable, and the app image on its own for an OTA. They are not the same file
and picking the wrong one fails confusingly, which is why both are published.

**The signing key is the part that matters.** A device only accepts an OTA
signed with the key whose public half it was flashed with, so a release
signed by a per-run throwaway installs fine over USB and then rejects every
future update — on the boat, months later, with no way back except a cable.
The workflow therefore refuses to build a release without `signing_key`,
unless the repository variable `ESPOS_ALLOW_UNSIGNED_RELEASE` is `true`, in
which case it warns and sets its `unsigned` output so the release notes can
say so.

### `--require`, and why it is not a warning

`espos_merge_firmware.py` reads the offset map the build already produced
(`flasher_args.json`), so the offsets can never drift from what the partition
table needs. A partition the build did not produce is skipped — usually
harmless, because an absent `otadata` just means "boot the first slot".

It is not harmless for a data partition, and the trouble is that the image
still **boots**. An espOS firmware merged without its `storage` partition
comes up and serves a placeholder page that reads as a firmware bug; one
merged without a wake-word model comes up with a dead wake word that no OTA
can repair. `--require storage` turns that into a build failure:

```
error: --require storage was given, but 'storage.bin' was not produced by
this build. Merging without it would still yield a bootable image -- which
is exactly why this is an error rather than a warning.
```

## The template repository

[signalk-espOS/espos-template](https://github.com/signalk-espOS/espos-template)
is generated, never edited by hand: `scripts/sync_template.sh <checkout> <tag>`
copies `components/espos_core/examples/minimal` into a checkout of the template
with espOS as the `espos/` submodule, and the release then bumps that
submodule to the tag and commits ("chore: sync to espOS vX.Y.Z"). Change the
example, not the template; CI builds the example on every target, the
template's own CI builds the copy once it is pushed.
