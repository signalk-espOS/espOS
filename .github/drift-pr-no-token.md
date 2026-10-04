<!--
SPDX-FileCopyrightText: 2026 Dirk Wahrheit
SPDX-License-Identifier: Apache-2.0

Appended to a bump pull request only when the job had to fall back to
GITHUB_TOKEN. A file rather than printf lines in the workflow: the text is full
of backticks, and shellcheck reads a backtick inside a single-quoted string as
an expansion somebody meant to happen (SC2016).
-->

> [!WARNING]
> Opened with `GITHUB_TOKEN`, so **no workflow ran on this pull request**.
> GitHub suppresses that so a workflow cannot trigger itself, which means the
> build meant to prove this bump has not run. Close and reopen the pull
> request to start it, or set a `DRIFT_PR_TOKEN` secret.
