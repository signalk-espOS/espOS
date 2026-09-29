# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""The mirror keeps what the flasher can offer, and nothing it still offers
is pruned."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from espos_release_window import load_releases, window  # noqa: E402


def rel(tag: str, pre: bool = False, draft: bool = False) -> dict:
    return {"tag_name": tag, "prerelease": pre, "draft": draft}


class WindowTest(unittest.TestCase):
    def test_three_newest_stable(self):
        releases = [rel(f"v1.{n}.0") for n in range(6)]
        self.assertEqual(window(releases, "v1.5.0"), ["v1.5.0", "v1.4.0", "v1.3.0"])

    def test_superseded_prerelease_does_not_push_out_a_stable(self):
        # A fixed "newest 3 tags" would drop v1.7.0, which the flasher still
        # offers as its third stable release.
        releases = [
            rel("v2.0.0"),
            rel("v1.9.0", pre=True),
            rel("v1.8.0"),
            rel("v1.7.0"),
            rel("v1.6.0"),
        ]
        self.assertEqual(window(releases, "v2.0.0"), ["v2.0.0", "v1.8.0", "v1.7.0"])

    def test_prereleases_ahead_of_stable_are_kept_beside_three_stable(self):
        # Three prereleases ahead: the two newest count, and all three stable
        # releases still do -- the sixth tag is needed, which KEEP=5 would miss.
        releases = [
            rel("v2.0.0", pre=True),
            rel("v1.9.0", pre=True),
            rel("v1.8.0", pre=True),
            rel("v1.7.0"),
            rel("v1.6.0"),
            rel("v1.5.0"),
            rel("v1.4.0"),
        ]
        self.assertEqual(
            window(releases, "v2.0.0"),
            ["v2.0.0", "v1.9.0", "v1.7.0", "v1.6.0", "v1.5.0"],
        )

    def test_only_prereleases(self):
        releases = [rel(f"v0.{n}.0", pre=True) for n in range(4)]
        self.assertEqual(window(releases, "v0.3.0"), ["v0.3.0", "v0.2.0"])

    def test_the_tag_being_published_is_always_kept(self):
        # Re-publishing an old release mirrors it even outside the window.
        releases = [rel(f"v1.{n}.0") for n in range(6)]
        self.assertEqual(
            window(releases, "v1.0.0"), ["v1.0.0", "v1.5.0", "v1.4.0", "v1.3.0"]
        )

    def test_window_alone_for_a_backfill(self):
        releases = [rel(f"v1.{n}.0") for n in range(5)]
        self.assertEqual(window(releases, ""), ["v1.4.0", "v1.3.0", "v1.2.0"])

    def test_new_tag_not_yet_listed(self):
        releases = [rel("v1.1.0"), rel("v1.0.0")]
        self.assertEqual(window(releases, "v1.2.0"), ["v1.2.0", "v1.1.0", "v1.0.0"])

    def test_drafts_and_non_plain_tags_are_ignored(self):
        releases = [
            rel("v1.3.0", draft=True),
            rel("v1.2.0-rc1", pre=True),
            rel("nightly"),
            rel("v1.1.0"),
        ]
        self.assertEqual(window(releases, "v1.1.0"), ["v1.1.0"])

    def test_numeric_not_lexical_order(self):
        releases = [rel("v1.10.0"), rel("v1.9.0"), rel("v1.2.0"), rel("v1.11.0")]
        self.assertEqual(
            window(releases, "v1.11.0"), ["v1.11.0", "v1.10.0", "v1.9.0"]
        )


class LoadTest(unittest.TestCase):
    def test_paginated_arrays_back_to_back(self):
        text = '[{"tag_name": "v1.0.0"}]\n[{"tag_name": "v0.9.0"}]\n'
        self.assertEqual(
            [r["tag_name"] for r in load_releases(text)], ["v1.0.0", "v0.9.0"]
        )


if __name__ == "__main__":
    unittest.main()
