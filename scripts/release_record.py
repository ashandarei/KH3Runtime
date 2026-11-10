"""Create and validate source, dependency and toolchain records for runtime releases."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def source_identity(repo: Path) -> dict:
    if git(repo, "status", "--porcelain", "--untracked-files=all"):
        raise ValueError("Release source has uncommitted changes; commit the intended release first.")
    return {"commit": git(repo, "rev-parse", "HEAD"), "tree": git(repo, "rev-parse", "HEAD^{tree}"),
            "dirty": False}


def header_identity(root: Path) -> dict:
    """Fingerprint relative header paths and contents as one digest and file count."""
    for name in ("Templates/Function.h", "HAL/UnrealMemory.h", "Containers/Map.h"):
        if not (root / "Public" / name).is_file():
            raise ValueError(f"Missing required Core header: {name}")
    entries = sorted(path for path in root.rglob("*") if path.is_file()
                     and path.suffix.lower() in {".h", ".hpp", ".inl", ".inc"})
    value = hashlib.sha256()
    for path in entries:
        value.update(path.relative_to(root).as_posix().encode() + b"\0")
        value.update(bytes.fromhex(digest(path)))
    return {"kind": "UE4.17 Core headers", "files": len(entries), "sha256": value.hexdigest()}


def snapshot(repo: Path, headers: Path) -> dict:
    return {"source": source_identity(repo), "external_headers": header_identity(headers)}


def dependency_records(repo: Path, build: Path) -> list[dict]:
    roots = [("ue4ss-goa", repo / "vendor/ue4ss-goa")]
    roots.extend((path.name, path) for path in sorted((build / "_deps").glob("*-src")))
    records = []
    for name, path in roots:
        if not (path / ".git").exists():
            continue
        changes = subprocess.check_output(["git", "-C", str(path), "diff", "--binary", "HEAD"])
        records.append({"name": name, "commit": git(path, "rev-parse", "HEAD"),
                        "tracked_diff_sha256": hashlib.sha256(changes).hexdigest(),
                        "submodules": git(path, "submodule", "status", "--recursive").splitlines()})
    if not records or records[0]["name"] != "ue4ss-goa":
        raise ValueError("Missing UE4SS dependency checkout")
    return records


def validate_record(record: dict, repo: Path, dll: Path, metadata: Path, version: str) -> None:
    if record.get("schema") != 1 or record.get("kind") != "kh3-local-release":
        raise ValueError("Invalid release provenance schema")
    if record.get("source") != source_identity(repo) or record.get("version") != version:
        raise ValueError("Release provenance does not match this source commit/version")
    if record.get("dll_sha256") != digest(dll) or record.get("build_metadata_sha256") != digest(metadata):
        raise ValueError("Release provenance does not match the DLL/build metadata")
    if record.get("clean_build") is not True or record.get("tests", {}).get("native") != "passed" or (
        record.get("tests", {}).get("catalogs") != "passed"
    ):
        raise ValueError("Release provenance requires a clean build and passing tests")
    toolchain = record.get("toolchain", {})
    if not toolchain.get("msvc") or not toolchain.get("windows_sdk") or toolchain.get("architecture") != "x64":
        raise ValueError("Release provenance is missing the compiler/SDK identity")
    for name in ("cl.exe", "link.exe", "rc.exe", "cmake.exe", "ninja.exe", "git.exe", "cargo.exe", "rustc.exe"):
        tool = toolchain.get("tools", {}).get(name, {})
        if not tool.get("version") or not re.fullmatch(r"[0-9a-f]{64}", str(tool.get("sha256", ""))):
            raise ValueError(f"Release provenance is missing {name}")
    if not re.fullmatch(r"[0-9a-f]{64}", str(record.get("external_headers", {}).get("sha256", ""))):
        raise ValueError("Release provenance is missing the external header fingerprint")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("snapshot", "record", "validate"))
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--headers", type=Path)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--before", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tag")
    parser.add_argument("--channel", choices=("Beta", "Stable"))
    parser.add_argument("--diagnostics-validated", action="store_true")
    args = parser.parse_args()
    if args.action == "snapshot":
        record = snapshot(args.repo, args.headers)
    else:
        dll = args.build / "KH3APProbeCpp/main.dll"
        metadata = dll.with_name("main.dll.build.json")
        version = (args.repo / "VERSION.txt").read_text().strip()
        if args.action == "validate":
            validate_record(json.loads(args.output.read_text()), args.repo, dll, metadata, version)
            return
        inputs = snapshot(args.repo, args.headers)
        if inputs != json.loads(args.before.read_text()):
            raise ValueError("Source or licensed headers changed during the release build")
        record = {
            "schema": 1, "kind": "kh3-local-release", **inputs,
            "version": version, "release_tag": args.tag, "channel": args.channel,
            "built_at_utc": datetime.now(timezone.utc).isoformat(), "clean_build": True,
            "configuration": "Game__Shipping__Win64", "performance_diagnostics": False,
            "automation_handlers": False, "dll_sha256": digest(dll),
            "build_metadata_sha256": digest(metadata),
            "toolchain": json.loads((args.build / "toolchain.json").read_text()),
            "dependencies": dependency_records(args.repo, args.build),
            "tests": {"catalogs": "passed", "native": "passed",
                      "diagnostic_build": "passed" if args.diagnostics_validated else "not_requested",
                      "live_game": "not_run"},
        }
        validate_record(record, args.repo, dll, metadata, version)
    args.output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
