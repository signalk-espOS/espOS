# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""A newer IDF release is reported as patch or minor, a pre-release never is,
and a pin that is not a release tag fails instead of reading as current."""

import contextlib
import io
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import espos_idf_updates  # noqa: E402
from espos_idf_updates import compare, parse_release, render  # noqa: E402

TAGS = [
    "v5.5.2", "v6.0-dev", "v6.0-beta1", "v6.0-rc1", "v6.0", "v6.0.1",
    "v6.0.2", "v6.0.3", "v6.1-dev", "v6.1-beta1", "v6.1-rc1", "v6.1",
]


class ParseRelease(unittest.TestCase):
    def test_x_y_is_x_y_0(self):
        self.assertEqual(parse_release("v6.1"), (6, 1, 0))

    def test_pre_releases_are_not_releases(self):
        for tag in ("v6.1-rc1", "v6.1-beta1", "v6.1-dev", "6.1", "v6.1.0.1"):
            self.assertIsNone(parse_release(tag), tag)

    def test_numeric_not_text_order(self):
        self.assertLess(parse_release("v6.9.9"), parse_release("v6.10"))


class Compare(unittest.TestCase):
    def test_current(self):
        r = compare("v6.1", TAGS)
        self.assertEqual((r["patch"], r["minor"]), (None, None))

    def test_patch_and_minor_separately(self):
        r = compare("v6.0.1", TAGS)
        self.assertEqual(r["patch"], "v6.0.3")
        self.assertEqual(r["minor"], "v6.1")

    def test_minor_only(self):
        r = compare("v6.0.3", TAGS)
        self.assertEqual((r["patch"], r["minor"]), (None, "v6.1"))

    def test_rc_never_offered(self):
        r = compare("v6.0.3", [t for t in TAGS if t != "v6.1"])
        self.assertEqual((r["patch"], r["minor"]), (None, None))

    def test_older_branch_ignored(self):
        # A v5.5.x patch published after v6.0 is not something to move to.
        r = compare("v6.0.3", ["v6.0.3", "v5.5.9"])
        self.assertEqual((r["patch"], r["minor"]), (None, None))

    def test_unreadable_pin_fails(self):
        with self.assertRaises(ValueError):
            compare("release/v6.0", TAGS)

    def test_render_names_a_new_major(self):
        self.assertTrue(render(compare("v6.1", TAGS + ["v7.0"])).splitlines()[1].startswith("major   v7.0"))
        self.assertTrue(render(compare("v6.0.3", TAGS)).splitlines()[1].startswith("minor   v6.1"))

    def test_render_says_current(self):
        self.assertIn("current", render(compare("v6.1", TAGS)))


class ExitStatus(unittest.TestCase):
    """1 tells the workflow to file an issue, so a failed check must not use it."""

    def run_main(self, root, tags=None, error=None):
        with mock.patch.object(espos_idf_updates, "upstream_tags", return_value=tags, side_effect=error), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return espos_idf_updates.main(["--root", str(root)])

    def test_newer_release_is_1(self):
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, ".idf-version").write_text("v6.0.3\n")
            self.assertEqual(self.run_main(d, TAGS), 1)

    def test_upstream_failure_is_2(self):
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, ".idf-version").write_text("v6.0.3\n")
            err = subprocess.CalledProcessError(128, "git ls-remote")
            self.assertEqual(self.run_main(d, error=err), 2)

    def test_missing_pin_is_2(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(self.run_main(d, TAGS), 2)

    def test_unreadable_pin_is_2(self):
        with tempfile.TemporaryDirectory() as d:
            pathlib.Path(d, ".idf-version").write_text("release/v6.0\n")
            self.assertEqual(self.run_main(d, TAGS), 2)


if __name__ == "__main__":
    unittest.main()
