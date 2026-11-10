"""Test release orchestration and archive validation with synthetic build outputs."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release_record", ROOT / "scripts/release_record.py")
record_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(record_module)
POWERSHELL = shutil.which("pwsh") or shutil.which("powershell.exe")


@unittest.skipUnless(POWERSHELL and shutil.which("git"), "PowerShell and Git required")
class LocalRelease(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kh3 release fixture ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "repo"
        self.root.mkdir()
        self.headers = Path(self.temp.name) / "private headers"
        for name in ("Templates/Function.h", "HAL/UnrealMemory.h", "Containers/Map.h"):
            path = self.headers / "Public" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture header")
        for folder in ("scripts", "data", "licenses"):
            (self.root / folder).mkdir()
        for name in ("release.ps1", "release_record.py", "package.ps1", "toolchain.ps1", "kh3-local-environment.ps1"):
            shutil.copy2(ROOT / "scripts" / name, self.root / "scripts" / name)
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
            (self.root / name).write_text("fixture notice")
        (self.root / "licenses/dependency.txt").write_text("fixture dependency notice")
        (self.root / "VERSION.txt").write_text("2.0.1\n")
        (self.root / ".gitignore").write_text("/vendor/\n/artifacts/\n/build*/\n__pycache__/\n*.pyc\n")
        (self.root / "data/runtime-inputs.json").write_text("{}")
        (self.root / "scripts/generate_catalogs.py").write_text("print('fixture catalog check passed')\n")
        # Stub dependency setup and compilation while exercising release and packaging scripts.
        (self.root / "scripts/setup.ps1").write_text("""
param($UePseudoSource)
$Dependency = Join-Path (Split-Path $PSScriptRoot -Parent) 'vendor/ue4ss-goa'
New-Item -ItemType Directory -Path $Dependency -Force > $null
git -C $Dependency init -q
git -C $Dependency -c user.name=Fixture -c user.email=fixture@example.invalid commit -q --allow-empty -m fixture
""")
        (self.root / "scripts/test.ps1").write_text("""
param($BuildDir, $VcVarsVer, $VsDevCmd)
if (Test-Path -LiteralPath $BuildDir) { throw 'test directory was reused' }
New-Item -ItemType Directory -Path $BuildDir > $null
""")
        (self.root / "scripts/build.ps1").write_text("""
param($BuildDir, $VcVarsVer, $VsDevCmd, $CoreHeadersRoot, [switch]$EnableDiagnostics)
if (Test-Path -LiteralPath $BuildDir) { throw 'build directory was reused' }
New-Item -ItemType Directory -Path (Join-Path $BuildDir 'KH3APProbeCpp') > $null
$Root = Split-Path $PSScriptRoot -Parent
Copy-Item (Join-Path $Root 'fixture.dll') (Join-Path $BuildDir 'KH3APProbeCpp/main.dll')
Copy-Item (Join-Path $Root 'fixture.build.json') (Join-Path $BuildDir 'KH3APProbeCpp/main.dll.build.json')
Copy-Item (Join-Path $Root 'fixture.toolchain.json') (Join-Path $BuildDir 'toolchain.json')
""")
        dll = b"synthetic DLL for release workflow tests"
        (self.root / "fixture.dll").write_bytes(dll)
        info = {"schema": 3, "sha256": hashlib.sha256(dll).hexdigest(),
                "runtime_inputs_sha256": hashlib.sha256(b"{}").hexdigest(),
                "first_party_license": "MIT", "performance_diagnostics": False,
                "automation_handlers": False, "ue4ss_revision": "1" * 40,
                "ue4ss_dirty": False, "allow_unpinned_ue4ss": False, "release_eligible": True}
        (self.root / "fixture.build.json").write_text(json.dumps(info))
        (self.root / "CMakeLists.txt").write_text('set(KH3AP_PINNED_UE4SS_COMMIT "' + "1" * 40 + '")\n')
        toolchain = {"msvc": "14.38.fixture", "windows_sdk": "10.fixture", "architecture": "x64",
                     "tools": {name: {"version": "fixture", "sha256": "0" * 64} for name in
                               ("cl.exe", "link.exe", "rc.exe", "cmake.exe", "ninja.exe", "git.exe", "cargo.exe", "rustc.exe")}}
        (self.root / "fixture.toolchain.json").write_text(json.dumps(toolchain))
        self.git("init", "-q")
        self.commit()

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.root), *args], text=True).strip()

    def commit(self):
        self.git("add", ".")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "fixture")

    def run_release(self, *args):
        env = os.environ.copy()
        env.pop("KH3_ENV_FILE", None)
        return subprocess.run([POWERSHELL, "-NoProfile", "-File", str(self.root / "scripts/release.ps1"),
                               "-CoreHeadersRoot", str(self.headers), "-UePseudoSource", str(self.root), *args], cwd=self.root, env=env,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)

    def test_clean_isolation_and_provenance_package(self):
        # Ignored build output should be absent from the release checkout.
        (self.root / "build").mkdir()
        (self.root / "build/contaminated.dll").write_bytes(b"stale")
        result = self.run_release("-ValidateDiagnostics")
        self.assertEqual(result.returncode, 0, result.stdout)
        runs = list((self.root / "artifacts/releases").iterdir())
        self.assertEqual(len(runs), 1)
        run = runs[0]
        self.assertFalse((run / "source/build/contaminated.dll").exists())
        archive = next((run / "dist").glob("*.zip"))
        with zipfile.ZipFile(archive) as package:
            report = json.loads(package.read("release-provenance.json"))
            self.assertEqual(report["source"]["commit"], self.git("rev-parse", "HEAD"))
            self.assertEqual(report["tests"]["diagnostic_build"], "passed")
            self.assertEqual(package.read("KH3APProbeCpp/dlls/main.dll"), (self.root / "fixture.dll").read_bytes())
            self.assertNotIn(str(self.headers), json.dumps(report))
            self.assertFalse(any("private headers" in name or name.endswith(".env") for name in package.namelist()))
            self.assertEqual(package.read("release-provenance.json"), Path(str(archive) + ".provenance.json").read_bytes())
        self.assertEqual(self.git("status", "--porcelain"), "")
        dll = run / "build-player/KH3APProbeCpp/main.dll"
        dll.write_bytes(b"modified after build")
        with self.assertRaisesRegex(ValueError, "does not match the DLL"):
            record_module.validate_record(report, run / "source", dll,
                                          dll.with_name("main.dll.build.json"), "2.0.1")
        second = self.run_release()
        self.assertEqual(second.returncode, 0, second.stdout)
        self.assertEqual(len(list((self.root / "artifacts/releases").iterdir())), 2)

    def test_dirty_source_and_mismatched_version_fail_before_build(self):
        (self.root / "uncommitted.txt").write_text("pending")
        result = self.run_release()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("clean working tree", result.stdout)
        (self.root / "uncommitted.txt").unlink()
        result = self.run_release("-ReleaseTag", "9.0.0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must match VERSION", result.stdout)
        self.assertFalse((self.root / "artifacts/releases").exists())

    def test_failed_tests_do_not_build_or_package(self):
        with (self.root / "scripts/test.ps1").open("a") as stream:
            stream.write("\nthrow 'fixture test failed'\n")
        self.commit()
        result = self.run_release()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("fixture test failed", result.stdout)
        self.assertEqual(list((self.root / "artifacts").rglob("*.zip")), [])
        self.assertEqual(list((self.root / "artifacts").rglob("build-player")), [])

    def test_source_change_during_build_cannot_be_packaged(self):
        with (self.root / "scripts/build.ps1").open("a") as stream:
            stream.write("\nAdd-Content (Join-Path $Root 'VERSION.txt') 'changed'\n")
        self.commit()
        result = self.run_release()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(list((self.root / "artifacts").rglob("*.zip")), [])

    def test_existing_tag_cannot_move(self):
        self.git("tag", "2.0.1")
        (self.root / "new-source.txt").write_text("new commit")
        self.commit()
        result = self.run_release()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tag points at a different commit", result.stdout)


if __name__ == "__main__":
    unittest.main()
