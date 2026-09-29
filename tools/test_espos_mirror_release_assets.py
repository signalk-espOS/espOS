# SPDX-FileCopyrightText: 2026 Dirk Wahrheit
# SPDX-License-Identifier: Apache-2.0
"""espos_mirror_release_assets.sh against a local bare repository.

Each case is a failure that happened, or would have, with a simpler script:
another release's images deleted, a rollback that could not be pushed, a
retry that blamed concurrency for an auth error.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parent / "espos_mirror_release_assets.sh"
BRANCH = "release-assets"


def git(*args: str, cwd: pathlib.Path | None = None) -> str:
    return subprocess.run(
        ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
    ).stdout.strip()


class MirrorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp())
        self.bare = self.tmp / "remote.git"
        git("init", "-q", "--bare", "-b", "main", str(self.bare))
        self.remote = f"file://{self.bare}"
        self.env = {
            **os.environ,
            "GIT_CONFIG_GLOBAL": "/dev/null",
            "GIT_AUTHOR_NAME": "t",
            "GIT_AUTHOR_EMAIL": "t@t",
            "GIT_COMMITTER_NAME": "t",
            "GIT_COMMITTER_EMAIL": "t@t",
        }

    def tearDown(self):
        shutil.rmtree(self.tmp)

    # -- helpers -----------------------------------------------------------

    def stage(self, run: str, *tags: str) -> pathlib.Path:
        staged = self.tmp / run / "staged"
        for t in tags:
            (staged / t).mkdir(parents=True)
            (staged / t / f"fw-esp32-{t}-merged.bin").write_bytes(t.encode())
            (staged / t / f"fw-esp32-{t}-ota.bin").write_bytes(t.encode())
        return staged

    def keep(self, run: str, *tags: str) -> pathlib.Path:
        path = self.tmp / run / "keep"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(f"{t}\n" for t in tags))
        return path

    def run_script(self, cmd: str, run: str, *extra: str, ok: bool = True):
        state = self.tmp / run / "state"
        proc = subprocess.run(
            ["bash", str(SCRIPT), cmd, "--remote", self.remote,
             "--state", str(state), *extra],
            capture_output=True, text=True, env=self.env,
        )
        if ok and proc.returncode != 0:
            self.fail(f"{cmd} failed:\n{proc.stdout}\n{proc.stderr}")
        if not ok and proc.returncode == 0:
            self.fail(f"{cmd} should have failed:\n{proc.stdout}\n{proc.stderr}")
        return proc

    def publish(self, run: str, tags, keep, ok: bool = True):
        return self.run_script(
            "publish", run,
            "--staged", str(self.stage(run, *tags)),
            "--keep", str(self.keep(run, *keep)), ok=ok,
        )

    def tip(self) -> str:
        return git("--git-dir", str(self.bare), "rev-parse", "--verify", "-q",
                   f"refs/heads/{BRANCH}")

    def dirs(self) -> list[str]:
        out = git("--git-dir", str(self.bare), "ls-tree", "--name-only",
                  f"refs/heads/{BRANCH}")
        return sorted(line for line in out.splitlines() if line.startswith("v"))

    def exists(self) -> bool:
        return subprocess.run(
            ["git", "--git-dir", str(self.bare), "rev-parse", "--verify", "-q",
             f"refs/heads/{BRANCH}"], capture_output=True,
        ).returncode == 0

    def hook(self, body: str):
        path = self.bare / "hooks" / "pre-receive"
        path.write_text("#!/usr/bin/env bash\n" + textwrap.dedent(body))
        path.chmod(0o755)

    # -- publish -----------------------------------------------------------

    def test_first_publish_creates_a_single_commit(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        self.assertEqual(self.dirs(), ["v1.0.0"])
        # No history: one commit, no parent.
        self.assertEqual(
            git("--git-dir", str(self.bare), "rev-list", "--count", BRANCH), "1"
        )

    def test_second_publish_keeps_the_first(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        self.publish("b", ["v1.1.0"], ["v1.1.0", "v1.0.0"])
        self.assertEqual(self.dirs(), ["v1.0.0", "v1.1.0"])
        self.assertEqual(
            git("--git-dir", str(self.bare), "rev-list", "--count", BRANCH), "1"
        )

    def test_prunes_what_the_window_no_longer_offers(self):
        self.publish("a", ["v1.0.0", "v1.1.0", "v1.2.0"],
                     ["v1.2.0", "v1.1.0", "v1.0.0"])
        self.publish("b", ["v1.3.0"], ["v1.3.0", "v1.2.0", "v1.1.0"])
        self.assertEqual(self.dirs(), ["v1.1.0", "v1.2.0", "v1.3.0"])

    def test_a_published_tag_is_kept_even_outside_the_window(self):
        # Re-publishing an old release must not prune the directory it wrote.
        self.publish("a", ["v1.3.0"], ["v1.3.0"])
        self.publish("b", ["v1.0.0"], ["v1.3.0"])
        self.assertEqual(self.dirs(), ["v1.0.0", "v1.3.0"])

    def test_refuses_a_tag_that_is_not_plain(self):
        proc = self.publish("a", ["v1.0.0-rc1"], ["v1.0.0-rc1"], ok=False)
        self.assertIn("vX.Y.Z", proc.stderr)
        self.assertFalse(self.exists())

    def test_retries_when_another_release_moved_the_branch(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        tip_a = self.tip()
        # Another release's publish, then the branch wound back to before it:
        # the hook replays it between this run's read and its push, once, and
        # rejects the push -- what a lease rejection looks like to the client.
        self.publish("other", ["v1.1.0"], ["v1.1.0", "v1.0.0"])
        tip_other = self.tip()
        git("--git-dir", str(self.bare), "update-ref", f"refs/heads/{BRANCH}", tip_a)
        flag = self.tmp / "moved"
        self.hook(f"""
            if [ ! -f {flag} ]; then
              touch {flag}
              env -u GIT_QUARANTINE_PATH git update-ref refs/heads/{BRANCH} {tip_other}
              exit 1
            fi
        """)
        proc = self.publish("c", ["v1.2.0"], ["v1.2.0", "v1.1.0", "v1.0.0"])
        self.assertIn("moved since it was read", proc.stdout + proc.stderr)
        # The other release's directory survived the retry.
        self.assertEqual(self.dirs(), ["v1.0.0", "v1.1.0", "v1.2.0"])

    def test_rollback_after_a_retry_restores_the_other_release(self):
        # The snapshot must be re-taken on the re-read. One kept from the first
        # read would roll back to a tip from before the other release existed,
        # deleting its directory while its release still points at it.
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        tip_a = self.tip()
        self.publish("other", ["v1.1.0"], ["v1.1.0", "v1.0.0"])
        tip_other = self.tip()
        git("--git-dir", str(self.bare), "update-ref", f"refs/heads/{BRANCH}", tip_a)
        flag = self.tmp / "moved"
        self.hook(f"""
            if [ ! -f {flag} ]; then
              touch {flag}
              env -u GIT_QUARANTINE_PATH git update-ref refs/heads/{BRANCH} {tip_other}
              exit 1
            fi
        """)
        self.publish("c", ["v1.2.0"], ["v1.2.0", "v1.1.0", "v1.0.0"])
        self.run_script("rollback", "c")
        self.assertEqual(self.tip(), tip_other)
        self.assertEqual(self.dirs(), ["v1.0.0", "v1.1.0"])

    def test_does_not_retry_when_nothing_moved(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        self.hook("exit 1\n")  # e.g. branch protection: refused, branch unchanged
        proc = self.publish("b", ["v1.1.0"], ["v1.1.0", "v1.0.0"], ok=False)
        self.assertIn("nothing", proc.stderr)
        self.assertNotIn("re-reading", proc.stdout + proc.stderr)
        self.assertEqual(self.dirs(), ["v1.0.0"])

    def test_refuses_to_publish_over_an_unreadable_remote(self):
        self.remote = f"file://{self.tmp / 'missing.git'}"
        proc = self.publish("a", ["v1.0.0"], ["v1.0.0"], ok=False)
        self.assertIn("could not read", proc.stderr)

    # -- rollback ----------------------------------------------------------

    def test_rollback_restores_what_the_run_replaced(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        before = self.tip()
        self.publish("b", ["v1.1.0"], ["v1.1.0", "v1.0.0"])
        self.run_script("rollback", "b")
        self.assertEqual(self.tip(), before)
        self.assertEqual(self.dirs(), ["v1.0.0"])

    def test_rollback_deletes_a_branch_the_run_created(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        self.run_script("rollback", "a")
        self.assertFalse(self.exists())

    def test_rollback_leaves_a_later_release_alone(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        self.publish("b", ["v1.1.0"], ["v1.1.0", "v1.0.0"])
        self.publish("c", ["v1.2.0"], ["v1.2.0", "v1.1.0", "v1.0.0"])
        after_c = self.tip()
        proc = self.run_script("rollback", "b")  # b failed after c published
        self.assertEqual(self.tip(), after_c)
        self.assertIn("leaving it", proc.stdout + proc.stderr)

    def test_rollback_without_a_push_does_nothing(self):
        self.publish("a", ["v1.0.0"], ["v1.0.0"])
        before = self.tip()
        self.run_script("rollback", "never-published")
        self.assertEqual(self.tip(), before)


if __name__ == "__main__":
    unittest.main()
