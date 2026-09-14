<!--
Title as a Conventional Commit: `fix(wifi): ...`, `feat(sk): ...`, `docs: ...`
(types and scopes in CONTRIBUTING.md). The title IS the CHANGELOG.md entry:
pull requests are squash-merged and release-please builds the release notes
from the titles, so write it for someone reading those notes. A change a
consumer has to react to puts `!` after the type (`feat(sk)!: ...`) and a
`BREAKING CHANGE: <what to change>` line at the end of this description.
-->

## What and why

<!-- What changes for a device or a consumer firmware, and what made it necessary. Link the issue. -->

## Host tests run

<!-- Paste the summary line of `test/host/run_all.sh` (or the projects you ran).
     A new state machine / parser / wire format / REST behaviour comes with a test; say where it is. -->

- [ ] `test/host/run_all.sh` passes
- [ ] new or changed host test: <!-- test/host/<project>/... -- or "none needed, because ..." -->

## Hardware tested on

<!-- target + board + IDF version, and what you exercised; or "host-only change". -->

## Docs

- [ ] `docs/` updated where behaviour changed (`docs/rest-api.md` is a contract)
- [ ] the title reads as its CHANGELOG.md line (it becomes one; do not edit CHANGELOG.md)
- [ ] new files carry the SPDX header (`reuse lint`), commits are signed off (`git commit -s`)
