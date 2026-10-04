# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""A bump reaches every manifest that pins the component, in either shape and
any quoting, and refuses loudly when it would reach none."""

import contextlib
import io
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from espos_apply_bump import apply, bump_text, main  # noqa: E402

BLOCK = """dependencies:
  idf: ">=6.0.0"
  espressif/mdns:
    # a comment between the key and the version, as the real manifests have
    version: "==1.11.3"
    rules:
      - if: "target != linux"
"""

INLINE_SINGLE = """dependencies:
  espressif/mdns: '==1.11.3'
"""

INLINE_BARE = """dependencies:
  espressif/mdns: ==1.11.3
"""


class BumpText(unittest.TestCase):
    def test_block_form_with_an_intervening_comment(self):
        out, n = bump_text(BLOCK, "espressif/mdns", "1.11.3", "1.14.0")
        self.assertEqual(n, 1)
        self.assertIn('version: "==1.14.0"', out)
        # The comment and the rules survive: only the scalar is replaced.
        self.assertIn("# a comment between", out)
        self.assertIn('- if: "target != linux"', out)

    def test_quoting_style_is_preserved(self):
        out, _ = bump_text(INLINE_SINGLE, "espressif/mdns", "1.11.3", "1.14.0")
        self.assertIn("'==1.14.0'", out)
        self.assertNotIn('"', out)
        out, _ = bump_text(INLINE_BARE, "espressif/mdns", "1.11.3", "1.14.0")
        self.assertIn("espressif/mdns: ==1.14.0", out)

    def test_a_range_is_never_touched(self):
        # The whole point of the exact-pin policy: a caret is a different
        # decision and this tool must not quietly convert one.
        text = 'dependencies:\n  espressif/mdns: "^1.11.0"\n'
        out, n = bump_text(text, "espressif/mdns", "1.11.3", "1.14.0")
        self.assertEqual(n, 0)
        self.assertEqual(out, text)

    def test_only_the_named_component_moves(self):
        text = (
            "dependencies:\n"
            '  espressif/mdns: "==1.11.3"\n'
            '  espressif/esp-sr: "==1.11.3"\n'
        )
        out, n = bump_text(text, "espressif/mdns", "1.11.3", "1.14.0")
        self.assertEqual(n, 1)
        self.assertIn('espressif/mdns: "==1.14.0"', out)
        # Same version string, different component: must not be swept along.
        self.assertIn('espressif/esp-sr: "==1.11.3"', out)

    def test_a_different_pin_is_left_alone(self):
        # Guards the rerun: --from is matched exactly, so a manifest someone
        # already moved is not moved again.
        out, n = bump_text(BLOCK, "espressif/mdns", "1.9.0", "1.14.0")
        self.assertEqual(n, 0)
        self.assertEqual(out, BLOCK)


class ApplyAcrossTree(unittest.TestCase):
    def test_every_manifest_moves_together(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            for rel in ("components/a", "components/b", "main"):
                p = root / rel
                p.mkdir(parents=True)
                (p / "idf_component.yml").write_text(BLOCK, encoding="utf-8")
            # Two places a manifest must never be read from.
            for rel in ("managed_components/espressif__mdns", "build/x"):
                p = root / rel
                p.mkdir(parents=True)
                (p / "idf_component.yml").write_text(BLOCK, encoding="utf-8")

            changed = apply(root, "espressif/mdns", "1.11.3", "1.14.0")

            self.assertEqual(len(changed), 3)
            for rel in ("components/a", "components/b", "main"):
                self.assertIn(
                    "==1.14.0", (root / rel / "idf_component.yml").read_text()
                )
            for rel in ("managed_components/espressif__mdns", "build/x"):
                self.assertIn(
                    "==1.11.3", (root / rel / "idf_component.yml").read_text()
                )


class ExitCodes(unittest.TestCase):
    def _root(self, text=BLOCK):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        root = pathlib.Path(d)
        (root / "main").mkdir()
        (root / "main" / "idf_component.yml").write_text(text, encoding="utf-8")
        return root

    def _main(self, *argv):
        """main() with its reporting captured: a passing suite should be
        silent, and these cases exercise the paths that print."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = main(list(argv))
        return rc, out.getvalue() + err.getvalue()

    def test_matching_nothing_is_an_error_not_a_quiet_success(self):
        root = self._root()
        rc, said = self._main("--root", str(root), "--name", "espressif/mdns",
                              "--from", "9.9.9", "--to", "9.9.10")
        self.assertEqual(rc, 1)
        self.assertIn("no manifest pins", said)

    def test_a_downgrade_is_refused(self):
        root = self._root()
        rc, said = self._main("--root", str(root), "--name", "espressif/mdns",
                              "--from", "1.11.3", "--to", "1.9.0")
        self.assertEqual(rc, 2)
        self.assertIn("not newer", said)
        self.assertIn("==1.11.3", (root / "main" / "idf_component.yml").read_text())

    def test_the_happy_path_returns_zero(self):
        root = self._root()
        rc, _ = self._main("--root", str(root), "--name", "espressif/mdns",
                           "--from", "1.11.3", "--to", "1.14.0")
        self.assertEqual(rc, 0)


if __name__ == "__main__":
    unittest.main()
