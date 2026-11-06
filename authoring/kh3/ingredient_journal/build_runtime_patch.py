"""Compile our original Gummiphone extension; preserve all stock movie code/art."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
# Reuse the audited constructor-splice authoring and preservation checks.
sys.path.insert(0, str(HERE.parent / 'shop_ui'))
import build_runtime_patch as extension


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('--java', type=Path, required=True)
    parser.add_argument('--ffdec', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, default=ROOT / 'build/ingredient-journal')
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    helper = extension.compile_helper(args.java.resolve(), args.ffdec.resolve(), args.work_dir.resolve(),
                                      'APIngredientJournal', HERE / 'APIngredientJournal.as')
    original = args.original.read_bytes()
    result, delta, audit = extension.extend_movie(original, helper, {'MobilePortal': 'attach'}, 'APIngredientJournal')
    (args.work_dir / 'extended.gfx').write_bytes(result)
    (args.work_dir / 'audit.json').write_text(json.dumps(dict(
        original_sha256=hashlib.sha256(original).hexdigest(), patched_sha256=hashlib.sha256(result).hexdigest(),
        helper_source_sha256=hashlib.sha256((HERE / 'APIngredientJournal.as').read_bytes()).hexdigest(),
        edits=audit), indent=2) + '\n')
    header = ROOT / 'KH3APProbeCpp/kh3ap/features/journal/ingredient_journal_movie_data.h'
    extension.write_patch_resource(header=header,
        resource=ROOT / 'KH3APProbeCpp/resources/ingredient_journal_patch.bin',
        resource_name='KH3AP_INGREDIENT_JOURNAL_PATCH', namespace='kh3ap::ingredient_journal_movie',
        source=HERE / 'APIngredientJournal.as', generator='authoring/kh3/ingredient_journal/build_runtime_patch.py',
        original=original, result=result, delta=delta)
    print(f'{len(original)} -> {len(result)} movie bytes; {len(delta)} extension bytes')


if __name__ == '__main__':
    main()
