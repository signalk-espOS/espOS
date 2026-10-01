# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""release-please-config.json, against what release-please is able to write.

This exists because of a release outage that produced no useful error. #152
added `.github/workflows/release-firmware.yml` to `extra-files`, for the
`x-release-please-version` pin in its header comment. Every release-please run
from that merge onward died with

    release-please failed: Error adding to tree: <sha>

and nothing else -- the action swallows the HTTP cause. Four pull requests
merged, including a breaking change, and no release pull request ever appeared,
because writing a file under `.github/workflows/` needs a permission no
`GITHUB_TOKEN` has: `workflows: write` exists only as a GitHub App permission
and has no `permissions:` key at all. There is nothing to configure around it.

So the rule is structural rather than stylistic, and belongs in CI: a pin that
has to stay current goes in a file release-please can actually rewrite --
`docs/releasing.md` carries this one -- and the copy inside the workflow names
no version.
"""

import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONFIG = ROOT / "release-please-config.json"


def extra_file_paths(config: dict) -> list[str]:
    paths = []
    for package in config.get("packages", {}).values():
        for entry in package.get("extra-files", []):
            paths.append(entry["path"] if isinstance(entry, dict) else entry)
    return paths


class ReleasePleaseConfigTest(unittest.TestCase):
    def setUp(self):
        self.config = json.loads(CONFIG.read_text())
        self.paths = extra_file_paths(self.config)

    def test_no_extra_file_under_github_workflows(self):
        offenders = [p for p in self.paths if p.startswith(".github/workflows/")]
        self.assertEqual(
            offenders,
            [],
            "release-please cannot write files under .github/workflows/ "
            f"(no GITHUB_TOKEN may): {offenders}. Every run would fail with "
            '"Error adding to tree". Put the version-bearing copy in a file it '
            "can rewrite, such as docs/releasing.md.",
        )

    def test_every_extra_file_exists(self):
        """A path that does not exist is the other way this list goes wrong,
        and it is equally invisible until a release is due."""
        missing = [p for p in self.paths if not (ROOT / p).exists()]
        self.assertEqual(missing, [], f"extra-files names paths that do not exist: {missing}")

    def test_every_extra_file_is_annotated(self):
        """An entry with no `x-release-please` annotation bumps nothing, so it
        is either a mistake or a file that lost its annotation in an edit."""
        unannotated = [
            p for p in self.paths if "x-release-please" not in (ROOT / p).read_text(errors="replace")
        ]
        self.assertEqual(
            unannotated, [], f"extra-files names files with no x-release-please annotation: {unannotated}"
        )

    def test_the_workflow_example_pin_names_no_version(self):
        """The counterpart of the first test: since that header cannot be
        bumped, it must not carry a version that would silently rot."""
        header = (ROOT / ".github/workflows/release-firmware.yml").read_text()
        example = [ln for ln in header.splitlines() if "release-firmware.yml@" in ln]
        self.assertTrue(example, "the header no longer shows how to call the workflow")
        for line in example:
            self.assertNotRegex(
                line,
                r"@v\d+\.\d+\.\d+",
                f"a literal version in a file release-please cannot bump will rot: {line.strip()}",
            )


if __name__ == "__main__":
    unittest.main()
