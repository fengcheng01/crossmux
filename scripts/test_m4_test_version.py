"""Host regression tests: python3 -m unittest discover -s scripts -p test_m4_test_version.py."""

import json
import tempfile
import unittest
from pathlib import Path

from m4_test_version import inject_version, select_revision, source_digest


class VersionTest(unittest.TestCase):
    def test_first_build_and_unchanged_retry(self):
        self.assertEqual(select_revision(None, "a", 126), 127)
        state = {"revision": 127, "fingerprint": "a"}
        self.assertEqual(select_revision(state, "a", 126), 127)
        self.assertEqual(select_revision(state, "b", 126), 128)

    def test_new_seed_never_reuses_older_revision(self):
        state = {"revision": 120, "fingerprint": "a"}
        self.assertEqual(select_revision(state, "a", 126), 127)

    def test_source_and_sdk_changes_but_not_generated_output(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            source = project / "src" / "main.cpp"
            source.parent.mkdir()
            source.write_text("before")
            before = source_digest(project)
            (source.parent / "ui.generated.h").write_text("generated")
            self.assertEqual(before, source_digest(project))
            source.write_text("after")
            self.assertNotEqual(before, source_digest(project))
            before = source_digest(project)
            sdk = project / "freeink-sdk" / "libs" / "driver.cpp"
            sdk.parent.mkdir(parents=True)
            sdk.write_text("driver")
            self.assertNotEqual(before, source_digest(project))

    def test_injection_persists_revision_and_skips_clean_and_other_envs(self):
        class Env(dict):
            def IsCleanTarget(self):
                return self.get("clean", False)

            def Replace(self, **values):
                self.update(values)

        with tempfile.TemporaryDirectory() as directory:
            flags = ['-DCROSSPOINT_VERSION=\\"1.5.7-m4cn.v126\\"', '-DUNCHANGED=1']
            env = Env(PIOENV="murphy_m4_cn", PROJECT_DIR=directory, BUILD_FLAGS=flags)
            inject_version(env)
            self.assertIn('v127', env["BUILD_FLAGS"][0])
            self.assertEqual(env["BUILD_FLAGS"][1], flags[1])
            state = Path(directory) / ".pio" / "m4-test-version.json"
            self.assertEqual(json.loads(state.read_text())["revision"], 127)
            env["BUILD_FLAGS"] = flags
            inject_version(env)
            self.assertIn('v127', env["BUILD_FLAGS"][0])
            env.update(BUILD_FLAGS=flags, clean=True)
            inject_version(env)
            self.assertEqual(env["BUILD_FLAGS"], flags)
            env.update(clean=False, PIOENV="gh_release_cn")
            inject_version(env)
            self.assertEqual(env["BUILD_FLAGS"], flags)


if __name__ == "__main__":
    unittest.main()
