"""Standalone generation and release gates; no game or licensed headers needed."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class CatalogGeneration(unittest.TestCase):
    def test_chest_sources_cover_catalog_and_reject_missing_or_invalid_actors(self):
        sys.path.insert(0, str(ROOT / "scripts"))
        try:
            from runtime_ui_catalogs import chest_journal_header
            locations = json.loads((ROOT / "data/runtime-inputs.json").read_text(encoding="utf-8"))["locations"]["locations"]
            sources = json.loads((ROOT / "data/chest-journal-sources.json").read_text(encoding="utf-8"))
            header = chest_journal_header(locations, sources)
            self.assertIn('L"RA_LBOX_006"', header)  # Dive-rock chest lives in the gimmick layer.
            self.assertIn(sources['RA_LBOX_006'][0], header)
            self.assertIn('L"DW_BOX031"', header)  # Imported post-story chest.
            self.assertIn(sources['DW_BOX031'][0], header)
            self.assertEqual(294, len(sources))
            incomplete = dict(sources)
            incomplete.pop("HE_LBOX_001")
            with self.assertRaises(ValueError):
                chest_journal_header(locations, incomplete)
            malformed = dict(sources, HE_LBOX_001=["/Game/Unverified"])
            with self.assertRaises(ValueError):
                chest_journal_header(locations, malformed)
        finally:
            sys.path.pop(0)

    def test_generation_and_stale_check_work_without_another_checkout(self):
        with tempfile.TemporaryDirectory(prefix="kh3-catalog-") as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "data").mkdir()
            shutil.copy2(ROOT / "scripts/generate_catalogs.py", root / "scripts")
            shutil.copy2(ROOT / "scripts/runtime_ui_catalogs.py", root / "scripts")
            payload = {"text": 'quote " slash \\ null \0 Unicode 世界 ' + "x" * 9000}
            data = {"schema": "kh3ap.runtime_inputs.v1", "limits": {"document_depth": 128},
                    "ui": json.loads((ROOT / "data/runtime-inputs.json").read_text(encoding="utf-8"))["ui"],
                    **{name: payload for name in ("catalog", "chests", "locations", "randomization")}}
            data["locations"] = {"locations": {}}
            (root / "data/chest-journal-sources.json").write_text("{}")
            source = root / "data/runtime-inputs.json"
            source.write_text(json.dumps(data), encoding="utf-8")
            for folder in ("bridge", "features/enemies"):
                (root / "KH3APProbeCpp/kh3ap" / folder).mkdir(parents=True)
            command = [sys.executable, "-E", "-s", str(root / "scripts/generate_catalogs.py")]
            subprocess.run(command, cwd=directory, check=True)
            header = root / "KH3APProbeCpp/kh3ap/bridge/catalog/catalog_data.h"
            original = header.read_bytes()
            chunks = [line.strip().removesuffix(";") for line in original.decode().splitlines()
                      if line.lstrip().startswith('"')]
            self.assertEqual(payload, json.loads("".join(json.loads(line) for line in chunks)))
            subprocess.run([*command, "--check"], cwd=directory, check=True)
            data["catalog"] = {"changed": True}
            source.write_text(json.dumps(data), encoding="utf-8")
            result = subprocess.run([*command, "--check"], cwd=directory, capture_output=True)
            self.assertNotEqual(0, result.returncode)
            self.assertEqual(original, header.read_bytes())
            ui_header = root / "KH3APProbeCpp/kh3ap/features/journal/ingredient_journal_catalog.h"
            ui_original = ui_header.read_bytes()
            data["catalog"] = payload
            data["ui"]["ingredients"][0]["name"] += " changed"
            source.write_text(json.dumps(data), encoding="utf-8")
            result = subprocess.run([*command, "--check"], cwd=directory, capture_output=True)
            self.assertNotEqual(0, result.returncode)
            self.assertIn(b"ingredient_journal_catalog.h", result.stderr)
            self.assertEqual(ui_original, ui_header.read_bytes())
            data["limits"] = {"depth; injected": 128}
            source.write_text(json.dumps(data), encoding="utf-8")
            self.assertNotEqual(0, subprocess.run(command, cwd=directory, capture_output=True).returncode)


@unittest.skipUnless(shutil.which("powershell.exe") or shutil.which("pwsh"), "PowerShell required")
class ReleasePackaging(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="kh3 package with spaces ")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for folder in ("scripts", "data", "licenses", "build/KH3APProbeCpp"):
            (self.root / folder).mkdir(parents=True)
        shutil.copy2(ROOT / "scripts/package.ps1", self.root / "scripts")
        for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "VERSION.txt", "CMakeLists.txt"):
            shutil.copy2(ROOT / name, self.root / name)
        (self.root / "licenses/fixture.txt").write_text("dependency notice")
        inputs = self.root / "data/runtime-inputs.json"
        inputs.write_text("{}")
        self.dll = self.root / "build/KH3APProbeCpp/main.dll"
        self.dll.write_bytes(b"synthetic DLL fixture: release gate test only")
        self.info = {"schema": 3, "ue4ss_revision": "06dd053d3e537ec2b477ee76337661b15642d012",
                     "ue4ss_dirty": False, "allow_unpinned_ue4ss": False, "release_eligible": True, "first_party_license": "MIT",
                     "performance_diagnostics": False, "automation_handlers": False,
                     "sha256": hashlib.sha256(self.dll.read_bytes()).hexdigest(),
                     "runtime_inputs_sha256": hashlib.sha256(inputs.read_bytes()).hexdigest()}

    def package(self):
        Path(str(self.dll) + ".build.json").write_text(json.dumps(self.info))
        environment = {key: value for key, value in os.environ.items() if key.casefold() != "psmodulepath"}
        # Windows PowerShell must discover its own modules when the test runner
        # was launched by PowerShell 7 or another host with a different module path.
        return subprocess.run(
            [shutil.which("powershell.exe") or shutil.which("pwsh"), "-NoProfile",
             "-ExecutionPolicy", "Bypass", "-File", str(self.root / "scripts/package.ps1")],
            cwd=self.root.parent, env=environment, capture_output=True, text=True, timeout=30)

    def test_release_contains_only_dll_metadata_and_notices(self):
        result = self.package()
        self.assertEqual(0, result.returncode, result.stderr)
        archive, = (self.root / "dist").glob("*.zip")
        with zipfile.ZipFile(archive) as package:
            self.assertEqual({"KH3APProbeCpp/dlls/main.dll", "KH3APProbeCpp/dlls/main.dll.build.json",
                              "LICENSE", "THIRD_PARTY_NOTICES.md", "licenses/fixture.txt", "manifest.json"},
                             set(package.namelist()))
            self.assertEqual(self.dll.read_bytes(), package.read("KH3APProbeCpp/dlls/main.dll"))
            self.assertEqual("MIT", json.loads(package.read("manifest.json"))["first_party_license"])

    def test_diagnostic_build_is_rejected(self):
        self.info.update(performance_diagnostics=True, automation_handlers=True)
        result = self.package()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("hash-verified MIT player build", result.stderr)
        self.assertFalse((self.root / "dist").exists())

    def test_modified_dll_is_rejected(self):
        self.dll.write_bytes(b"changed after build")
        result = self.package()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("hash-verified MIT player build", result.stderr)
        self.assertFalse((self.root / "dist").exists())

    def test_changed_catalog_is_rejected(self):
        (self.root / "data/runtime-inputs.json").write_text('{"changed":true}')
        result = self.package()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("different runtime catalogs", result.stderr)
        self.assertFalse((self.root / "dist").exists())

    def test_unreviewed_dependency_is_rejected(self):
        for change in ({"ue4ss_revision": "0" * 40}, {"ue4ss_dirty": True},
                       {"allow_unpinned_ue4ss": True}, {"release_eligible": False},
                       {"ue4ss_dirty": "false"}, {"release_eligible": "true"}):
            with self.subTest(change=change):
                original = dict(self.info)
                self.info.update(change)
                result = self.package()
                self.assertNotEqual(0, result.returncode)
                self.assertIn("reviewed UE4SS", result.stderr)
                self.assertFalse((self.root / "dist").exists())
                self.info = original

    def test_missing_provenance_is_rejected(self):
        del self.info["ue4ss_revision"]
        result = self.package()
        self.assertNotEqual(0, result.returncode)
        self.assertIn("reviewed UE4SS provenance", result.stderr)


@unittest.skipUnless(shutil.which("git") and shutil.which("cmake"), "Git and CMake required")
class DependencyProvenance(unittest.TestCase):
    def test_clean_dirty_and_research_builds(self):
        with tempfile.TemporaryDirectory(prefix="kh3 provenance ") as directory:
            root = Path(directory)
            checkout = root / "dependency"
            checkout.mkdir()
            def git(*args):
                return subprocess.check_output(["git", "-C", str(checkout), *args], text=True).strip()
            git("init", "--quiet")
            source = checkout / "source.cpp"
            source.write_text("reviewed\n")
            git("add", "source.cpp")
            git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "test fixture")
            revision = git("rev-parse", "HEAD")
            output = root / "provenance.json"
            def verify(pin=revision, research=False):
                return subprocess.run(["cmake", f"-DUE4SS_SRC={checkout}",
                    f"-DKH3AP_PINNED_UE4SS_COMMIT={pin}",
                    "-DKH3AP_PINNED_UE4SS_REPOSITORY=fixture",
                    f"-DKH3AP_ALLOW_UNPINNED_UE4SS={'ON' if research else 'OFF'}",
                    f"-DPROVENANCE_FILE={output}", "-P", str(ROOT / "cmake/verify-ue4ss.cmake")],
                    capture_output=True, text=True)
            self.assertEqual(0, verify().returncode)
            clean = json.loads(output.read_text())
            self.assertEqual(revision, clean["ue4ss_revision"])
            self.assertTrue(clean["release_eligible"])
            stamp = output.stat().st_mtime_ns
            self.assertEqual(0, verify().returncode)
            self.assertEqual(stamp, output.stat().st_mtime_ns)
            source.write_text("local edit\n")
            self.assertNotEqual(0, verify().returncode)
            self.assertEqual(0, verify(research=True).returncode)
            dirty = json.loads(output.read_text())
            self.assertTrue(dirty["ue4ss_dirty"])
            self.assertTrue(dirty["allow_unpinned_ue4ss"])
            self.assertFalse(dirty["release_eligible"])
            source.write_text("reviewed\n")
            self.assertNotEqual(0, verify(pin="0" * 40).returncode)
            self.assertEqual(0, verify(pin="0" * 40, research=True).returncode)
            self.assertFalse(json.loads(output.read_text())["release_eligible"])
            self.assertEqual(0, verify().returncode)
            self.assertEqual(clean, json.loads(output.read_text()))


if __name__ == "__main__":
    unittest.main()
