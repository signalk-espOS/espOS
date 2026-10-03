# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""A pin behind the registry is reported, a current one is not, and a registry
that will not answer reports nothing rather than something alarming."""

import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from espos_registry_updates import (  # noqa: E402
    classify,
    manifest_deps,
    newest_published,
    parse_version,
    report,
)


class VersionOrder(unittest.TestCase):
    def test_patch_order(self):
        self.assertLess(parse_version("2.12.3"), parse_version("2.12.7"))

    def test_major_beats_bigger_minor(self):
        # The trap that made 0.9.1 look newer than 0.12.1 when sorted as text.
        self.assertLess(parse_version("2.12.13"), parse_version("3.0.9"))
        self.assertLess(parse_version("0.9.1"), parse_version("0.12.1"))

    def test_revision_suffix(self):
        # cjson ships 1.7.19~2; the revision sorts above the bare patch.
        self.assertLess(parse_version("1.7.19"), parse_version("1.7.19~2"))
        self.assertLess(parse_version("1.7.19~2"), parse_version("1.7.19~10"))

    def test_prerelease_sorts_below_its_release(self):
        # So that `newest` never reports a release candidate as the thing to
        # move to.
        self.assertLess(parse_version("1.0.0-rc1"), parse_version("1.0.0"))
        self.assertLess(parse_version("1.0.0-rc10"), parse_version("1.0.0"))

    def test_prereleases_compare_numerically(self):
        # rc10 follows rc2; a plain string compare puts it before.
        self.assertLess(parse_version("1.0.0-rc2"), parse_version("1.0.0-rc10"))

    def test_non_numeric_revision_does_not_raise(self):
        # The string comes from the registry, and a crash here fails the whole
        # check rather than reporting one odd version.
        self.assertEqual(parse_version("1.0.0~beta"), parse_version("1.0.0~beta"))
        self.assertLess(parse_version("1.0.0~beta"), parse_version("1.0.0~2"))

    def test_short_versions_pad(self):
        self.assertEqual(parse_version("1.2")[:3], (1, 2, 0))


class Classify(unittest.TestCase):
    def test_exact_pin_behind(self):
        state, detail = classify({"==2.12.12"}, "3.0.9")
        self.assertEqual(state, "behind")
        self.assertIn("2.12.12", detail)
        self.assertIn("3.0.9", detail)

    def test_exact_pin_current(self):
        self.assertEqual(classify({"==3.0.9"}, "3.0.9")[0], "current")

    def test_pin_ahead_of_registry_is_current_not_behind(self):
        # A locally overridden or yanked-newer case must not nag.
        self.assertEqual(classify({"==3.1.0"}, "3.0.9")[0], "current")

    def test_range_is_unpinned(self):
        self.assertEqual(classify({"*"}, "1.6.5")[0], "unpinned")
        self.assertEqual(classify({"^1.5.0"}, "1.8.0")[0], "unpinned")

    def test_inconsistent_pins_are_reported(self):
        # Ten manifests must not disagree about one component.
        state, detail = classify({"==1.0.0", "==2.0.0"}, "2.0.0")
        self.assertEqual(state, "behind")
        self.assertIn("inconsistently", detail)

    def test_registry_silence_is_unknown(self):
        # Never "behind": an outage must not open an issue claiming a move.
        self.assertEqual(classify({"==1.0.0"}, None)[0], "unknown")


class ManifestParsing(unittest.TestCase):
    def _tree(self, body: str) -> pathlib.Path:
        d = pathlib.Path(tempfile.mkdtemp())
        (d / "components" / "x").mkdir(parents=True)
        (d / "components" / "x" / "idf_component.yml").write_text(body)
        return d

    def test_both_spellings(self):
        d = self._tree(
            'dependencies:\n'
            '  idf: ">=6.0.0,<6.1.0"\n'
            '  espressif/cjson: "==1.7.19~2"\n'
            '  # a comment between the key and its version\n'
            '  espressif/esp_hosted:\n'
            '    # why it is here\n'
            '    version: "==3.0.9"\n'
            '    rules:\n'
            '      - if: "target == esp32p4"\n'
            '  signalk-espos/espos_core:\n'
            '    version: "^0.14.0"\n'
        )
        deps = manifest_deps(d)
        self.assertEqual(deps["espressif/cjson"], {"==1.7.19~2"})
        self.assertEqual(deps["espressif/esp_hosted"], {"==3.0.9"})
        # Ours and idf are not this report's business.
        self.assertNotIn("signalk-espos/espos_core", deps)
        self.assertNotIn("idf", deps)

    def test_report_uses_the_injected_fetcher(self):
        d = self._tree('dependencies:\n  espressif/cjson: "==1.0.0"\n')
        rows = report(d, fetch=lambda name: "2.0.0")
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["state"], "behind")


class NewestPublished(unittest.TestCase):
    class _Resp:
        def __init__(self, payload):
            self._p = payload

        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False

        def read(self):
            import json

            return json.dumps(self._p).encode()

    def _opener(self, payload):
        def _open(url, timeout=0):
            return self._Resp(payload)

        return _open

    def test_picks_newest_stable(self):
        got = newest_published(
            "espressif/x",
            opener=self._opener(
                {"versions": [{"version": "1.0.0"}, {"version": "1.2.0"},
                              {"version": "1.10.0"}]}
            ),
        )
        self.assertEqual(got, "1.10.0")

    def test_skips_prereleases(self):
        got = newest_published(
            "espressif/x",
            opener=self._opener(
                {"versions": [{"version": "1.0.0"}, {"version": "2.0.0-rc1"}]}
            ),
        )
        self.assertEqual(got, "1.0.0")

    def test_network_failure_is_none(self):
        def _boom(url, timeout=0):
            raise TimeoutError("no registry")

        self.assertIsNone(newest_published("espressif/x", opener=_boom))


class ScalarForms(unittest.TestCase):
    """YAML allows three ways to write the same version. The tree uses double
    quotes throughout, and a checker that silently skips the other two is a
    checker that checks nothing the day someone writes one."""

    def _deps(self, body: str, unparsed=None):
        d = pathlib.Path(tempfile.mkdtemp())
        (d / "components" / "x").mkdir(parents=True)
        (d / "components" / "x" / "idf_component.yml").write_text(body)
        return manifest_deps(d, unparsed)

    def test_double_single_and_bare(self):
        deps = self._deps(
            "dependencies:\n"
            '  espressif/a: "==1.0.0"\n'
            "  espressif/b: '==2.0.0'\n"
            "  espressif/c: ==3.0.0\n"
        )
        self.assertEqual(deps["espressif/a"], {"==1.0.0"})
        self.assertEqual(deps["espressif/b"], {"==2.0.0"})
        self.assertEqual(deps["espressif/c"], {"==3.0.0"})

    def test_hyphenated_namespace_is_recognised(self):
        # signalk-espos has a hyphen; a pattern that missed it meant our own
        # components were never matched and so never filtered out.
        unparsed = []
        deps = self._deps(
            "dependencies:\n"
            '  signalk-espos/espos_core:\n    version: "^0.14.0"\n',
            unparsed,
        )
        self.assertEqual(deps, {})          # ours, filtered
        self.assertEqual(unparsed, [])      # recognised, so not "unreadable"

    def test_idf_only_manifest_is_not_flagged(self):
        # Five components declare nothing but idf. That is "no third-party
        # dependencies", not a parse failure.
        unparsed = []
        self._deps('dependencies:\n  idf: ">=6.0.0,<6.1.0"\n', unparsed)
        self.assertEqual(unparsed, [])

    def test_unreadable_manifest_is_flagged(self):
        # version placed after `rules:` is a shape the block pattern cannot
        # reach. It must be reported, never silently dropped.
        unparsed = []
        deps = self._deps(
            "dependencies:\n"
            "  espressif/weird:\n"
            '    rules:\n      - if: "target == esp32"\n'
            '    version: "==1.0.0"\n',
            unparsed,
        )
        self.assertNotIn("espressif/weird", deps)
        self.assertEqual(len(unparsed), 1)


class PrereleaseOnlyRegistry(unittest.TestCase):
    def test_prerelease_only_is_none_not_a_target(self):
        # Reporting "behind, newest 2.0.0-rc1" would be advice to ship a
        # release candidate.
        def _open(url, timeout=0):
            return NewestPublished._Resp({"versions": [{"version": "2.0.0-rc1"}]})

        self.assertIsNone(newest_published("espressif/x", opener=_open))


class RegistryOutage(unittest.TestCase):
    """What the registry says cannot change whether our own manifest is
    pinned, so an outage must not downgrade `unpinned` to `unknown`."""

    def test_range_is_unpinned_even_with_no_registry(self):
        state, detail = classify({"*"}, None)
        self.assertEqual(state, "unpinned")
        self.assertIn("did not answer", detail)

    def test_exact_pin_with_no_registry_is_unknown(self):
        self.assertEqual(classify({"==1.0.0"}, None)[0], "unknown")

    def test_inconsistent_pins_need_no_registry(self):
        self.assertEqual(classify({"==1.0.0", "==2.0.0"}, None)[0], "behind")


class PartialParse(unittest.TestCase):
    def test_readable_dep_does_not_mask_an_unreadable_one(self):
        # The per-manifest version of this check missed exactly this shape.
        d = pathlib.Path(tempfile.mkdtemp())
        (d / "components" / "x").mkdir(parents=True)
        (d / "components" / "x" / "idf_component.yml").write_text(
            "dependencies:\n"
            '  idf: ">=6.0.0"\n'
            '  espressif/good: "==1.0.0"\n'
            "  espressif/weird:\n"
            '    rules:\n      - if: "target == esp32"\n'
            '    version: "==2.0.0"\n'
        )
        unparsed = []
        deps = manifest_deps(d, unparsed)
        self.assertIn("espressif/good", deps)
        self.assertEqual(len(unparsed), 1)
        self.assertIn("espressif/weird", unparsed[0])


class HostileRegistryShapes(unittest.TestCase):
    """The registry is external input. A surprising shape must give None, not
    an exception -- the workflow treats a crash as a failed job, so a
    wrong-shaped response would take the whole check down."""

    def _open(self, payload):
        return lambda url, timeout=0: NewestPublished._Resp(payload)

    def test_top_level_list(self):
        self.assertIsNone(newest_published("e/x", opener=self._open([1, 2])))

    def test_top_level_string(self):
        self.assertIsNone(newest_published("e/x", opener=self._open("nope")))

    def test_versions_not_a_list(self):
        self.assertIsNone(
            newest_published("e/x", opener=self._open({"versions": "x"})))

    def test_version_entries_not_dicts(self):
        self.assertIsNone(
            newest_published("e/x", opener=self._open({"versions": ["1.0.0"]})))

    def test_still_reads_a_good_response(self):
        got = newest_published(
            "e/x", opener=self._open({"versions": [{"version": "1.2.3"}]}))
        self.assertEqual(got, "1.2.3")


class RootPathWithBuildInIt(unittest.TestCase):
    def test_checkout_under_a_build_directory_is_not_excluded(self):
        # Exclusions are relative to root. Absolute matching would exclude the
        # whole tree for a checkout at .../work/build/espOS, leaving a tool
        # that reports nothing and looks content.
        base = pathlib.Path(tempfile.mkdtemp()) / "build" / "espOS"
        (base / "components" / "x").mkdir(parents=True)
        (base / "components" / "x" / "idf_component.yml").write_text(
            'dependencies:\n  espressif/a: "==1.0.0"\n'
        )
        self.assertIn("espressif/a", manifest_deps(base))

    def test_managed_components_is_still_excluded(self):
        base = pathlib.Path(tempfile.mkdtemp())
        d = base / "managed_components" / "espressif__x"
        d.mkdir(parents=True)
        (d / "idf_component.yml").write_text(
            'dependencies:\n  espressif/b: "==1.0.0"\n')
        self.assertEqual(manifest_deps(base), {})


if __name__ == "__main__":
    unittest.main()
