# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""A newer IDF release is reported as patch or minor, a pre-release never is,
and a pin that is not a release tag fails instead of reading as current."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

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

    def test_render_says_current(self):
        self.assertIn("current", render(compare("v6.1", TAGS)))


if __name__ == "__main__":
    unittest.main()
