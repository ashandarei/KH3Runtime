"""Test pinned UEPseudo setup using local Git repositories."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell.exe")


@unittest.skipUnless(POWERSHELL and shutil.which("git"), "PowerShell and Git required")
class LicensedDependencySetup(unittest.TestCase):
    def git(self, root, *args):
        return subprocess.check_output(["git", "-C", str(root), *args], text=True).strip()

    def commit(self, root):
        self.git(root, "add", ".")
        self.git(root, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "fixture")
        return self.git(root, "rev-parse", "HEAD")

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="licensed dependency fixture ")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name) / "runtime"
        (self.root / "scripts").mkdir(parents=True)
        for name in ("setup.ps1", "toolchain.ps1", "kh3-local-environment.ps1"):
            shutil.copy2(ROOT / "scripts" / name, self.root / "scripts" / name)
        self.source = Path(temp.name) / "licensed source"
        self.source.mkdir()
        self.git(self.source, "init", "-q")
        (self.source / "header.hpp").write_text("reviewed fixture")
        self.pin = self.commit(self.source)
        (self.source / "header.hpp").write_text("newer unreviewed fixture")
        self.commit(self.source)
        (self.source / "header.hpp").write_text("uncommitted fixture")
        self.vendor = self.root / "vendor/ue4ss-goa"
        self.vendor.mkdir(parents=True)
        self.git(self.vendor, "init", "-q")
        (self.vendor / ".gitmodules").write_text(
            '[submodule "deps/first/Unreal"]\n'
            '\tpath = deps/first/Unreal\n'
            '\turl = https://invalid.example/must-never-be-fetched.git\n')
        self.git(self.vendor, "add", ".gitmodules")
        self.git(self.vendor, "update-index", "--add", "--cacheinfo",
                 f"160000,{self.pin},deps/first/Unreal")
        self.git(self.vendor, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "-qm", "fixture")
        revision = self.git(self.vendor, "rev-parse", "HEAD")
        (self.root / "CMakeLists.txt").write_text(
            'set(KH3AP_PINNED_UE4SS_REPOSITORY "https://github.com/fixture/unused.git")\n'
            f'set(KH3AP_PINNED_UE4SS_COMMIT "{revision}")\n')

    def setup(self, *args):
        env = os.environ.copy()
        for name in ("KH3_ENV_FILE", "KH3AP_UEPSEUDO_SOURCE"):
            env.pop(name, None)
        # Restrict Git transport to local files for offline tests.
        env["GIT_ALLOW_PROTOCOL"] = "file"
        return subprocess.run([POWERSHELL, "-NoProfile", "-File", str(self.root / "scripts/setup.ps1"),
                               *map(str, args)], env=env, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=45)

    def test_local_source_preserves_pin_without_public_fetch_or_tracked_changes(self):
        result = self.setup("-UePseudoSource", self.source)
        self.assertEqual(result.returncode, 0, result.stdout)
        dest = self.vendor / "deps/first/Unreal"
        self.assertEqual(self.git(dest, "rev-parse", "HEAD"), self.pin)
        self.assertEqual((dest / "header.hpp").read_text(), "reviewed fixture")
        self.assertEqual(self.git(self.vendor, "status", "--porcelain"), "")
        self.assertNotIn("invalid.example", self.git(dest, "remote", "get-url", "origin"))
        second = self.setup("-UePseudoSource", self.source)
        self.assertEqual(second.returncode, 0, second.stdout)
        (dest / "header.hpp").write_text("keep my work")
        dirty = self.setup("-UePseudoSource", self.source)
        self.assertNotEqual(dirty.returncode, 0)
        self.assertIn("Preserve local changes", dirty.stdout)
        self.assertEqual((dest / "header.hpp").read_text(), "keep my work")

    def test_missing_source_or_commit_fails_without_public_fallback(self):
        missing = self.setup()
        self.assertNotEqual(missing.returncode, 0)
        self.assertIn("KH3AP_UEPSEUDO_SOURCE", missing.stdout)
        unrelated = self.setup("-UePseudoSource", self.vendor)
        self.assertNotEqual(unrelated.returncode, 0)
        self.assertIn("lacks pinned commit", unrelated.stdout)
        self.assertFalse((self.vendor / "deps/first/Unreal/.git").exists())


if __name__ == "__main__":
    unittest.main()
