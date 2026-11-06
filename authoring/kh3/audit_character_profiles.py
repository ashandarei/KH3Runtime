"""Verify the paired character-native audit against installed EGS/Steam binaries.

Read-only: checks image identity, recorded code bytes and source address bindings.
An offline match does not validate private layouts, ABI or live gameplay.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from generate_character_profiles import OUTPUT as RUNTIME_TABLE, render


REPO = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).with_name("character_profiles.json")


class Image:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:2] != b"MZ":
            raise ValueError(f"Invalid DOS header: {path}")
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"Invalid PE header: {path}")
        machine, count, self.timestamp = struct.unpack_from("<HHI", self.data, pe + 4)
        optional_size = struct.unpack_from("<H", self.data, pe + 20)[0]
        if machine != 0x8664 or not 0 < count < 64 or struct.unpack_from("<H", self.data, pe + 24)[0] != 0x20B:
            raise ValueError(f"Expected Win64 PE image: {path}")
        self.image_size = struct.unpack_from("<I", self.data, pe + 24 + 56)[0]
        self.sections = []
        for index in range(count):
            start = pe + 24 + optional_size + index * 40
            virtual_size, rva, size, offset = struct.unpack_from("<IIII", self.data, start + 8)
            if offset + size > len(self.data):
                raise ValueError(f"Truncated PE section: {path}")
            self.sections.append((rva, size, offset))

    def read(self, rva: int, size: int) -> bytes:
        for start, length, offset in self.sections:
            if start <= rva and rva + size <= start + length:
                return self.data[offset + rva - start:offset + rva - start + size]
        raise ValueError(f"Unbacked RVA {rva:#x} in {self.path}")


def executable(root: Path) -> Path:
    nested = root / "KINGDOM HEARTS III"
    if (nested / "Binaries/Win64/KINGDOM HEARTS III.exe").is_file():
        root = nested
    return root / "Binaries/Win64/KINGDOM HEARTS III.exe"


def audit(manifest: dict, roots: dict[str, Path]) -> dict:
    result = dict(schema="kh3ap.character_native_audit_result.v1", status="passed", builds={}, sites=[], errors=[])
    for storefront, root in roots.items():
        image = Image(executable(root))
        expected = manifest["builds"][storefront]
        identity = dict(path=str(image.path.resolve()), timestamp=hex(image.timestamp), image_size=hex(image.image_size),
                        sha256=hashlib.sha256(image.data).hexdigest())
        result["builds"][storefront] = identity
        if any(identity[key] != expected[key] for key in ("timestamp", "image_size", "sha256")):
            result["errors"].append(f"{storefront}: executable identity differs from audited image")
        for site in manifest["sites"]:
            profile = site.get(storefront)
            if storefront == "steam":
                for block in site.get("steam_equivalent_blocks", []):
                    expected_bytes = bytes.fromhex(block["bytes"])
                    matched = image.read(int(block["rva"], 16), len(expected_bytes)) == expected_bytes
                    result["sites"].append(dict(id=site["id"], storefront=storefront, rva=block["rva"],
                                                bytes_match=matched, callable_entry=False))
                    if not matched:
                        result["errors"].append(f"steam:{site['id']}: owning-method block differs")
            if profile is None:
                continue
            rva = int(profile["rva"], 16)
            expected_bytes = bytes.fromhex(profile["bytes"])
            matched = image.read(rva, len(expected_bytes)) == expected_bytes
            result["sites"].append(dict(id=site["id"], storefront=storefront, rva=profile["rva"], bytes_match=matched))
            if not matched:
                result["errors"].append(f"{storefront}:{site['id']}: recorded code bytes differ")
            source = (REPO / site["source"]).read_text(encoding="utf-8")
            if site.get("runtime", True):
                if not re.search(rf"\bSite::{re.escape(site['id'])}\b", source):
                    result["errors"].append(f"{site['id']}: runtime source no longer binds the audited site")
            elif not re.search(rf"\b0x0*{rva:x}\b", source, re.IGNORECASE):
                result["errors"].append(f"{storefront}:{site['id']}: documentary address changed")
    if not RUNTIME_TABLE.exists() or RUNTIME_TABLE.read_text(encoding="utf-8") != render(manifest):
        result["errors"].append("runtime character profile differs from the audited manifest")
    result["matched_offline"] = sum(site["status"] == "matched_offline" for site in manifest["sites"])
    result["remaining"] = [dict(id=site["id"], status=site["status"]) for site in manifest["sites"]
                           if site["status"] not in ("matched_offline", "platform_specific_hook")]
    result["platform_specific_hooks"] = [site["id"] for site in manifest["sites"]
                                         if site["status"] == "platform_specific_hook"]
    result["runtime_sites"] = sum(site.get("runtime", True) for site in manifest["sites"])
    result["steam_live_validated"] = False
    if result["errors"]:
        result["status"] = "failed"
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--egs-game-dir", type=Path)
    parser.add_argument("--steam-game-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    roots = {store: root for store, root in
             (("egs", args.egs_game_dir), ("steam", args.steam_game_dir)) if root is not None}
    if not roots:
        parser.error("Select an installation with --egs-game-dir or --steam-game-dir")
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if manifest.get("schema") != "kh3ap.character_native_audit.v1":
            raise ValueError("Unsupported character-native audit schema")
        result = audit(manifest, roots)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    except (OSError, ValueError, KeyError, struct.error) as exc:
        parser.exit(1, f"{exc}\n")
    print(json.dumps({key: result[key] for key in ("status", "matched_offline", "remaining", "errors")}))
    return int(result["status"] != "passed")


if __name__ == "__main__":
    raise SystemExit(main())
