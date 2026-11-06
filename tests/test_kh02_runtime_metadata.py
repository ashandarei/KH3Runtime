"""Native alias coverage and area-policy regressions, without game assets."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "authoring/kh02"))
from runtime_metadata import load_metadata, generated_files, require_generated, validate_metadata


class RuntimeMetadataTests(unittest.TestCase):
    def setUp(self):
        self.metadata = load_metadata()

    def test_generated_runtime_contract_is_current(self):
        require_generated(self.metadata)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in generated_files(self.metadata).items():
                (root / name).write_text(text, encoding='utf-8')
            require_generated(self.metadata, root)
            (root / 'compatibility_aliases.inc').write_text('// stale', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'stale_runtime_metadata'):
                require_generated(self.metadata, root)

    def test_alias_phases_preserve_interleaved_native_setup(self):
        rows = self.metadata['aliases']
        self.assertEqual([row['name'] for row in rows if row['phase'] == 'after_signs'], ['KH02_MJump'])
        self.assertEqual([row['name'] for row in rows if row['phase'] == 'after_enums'],
                         ['KH02_NavBegin', 'KH02_NavTick', 'IsSuspendedBGM', 'SetVolumeBGM'])
        changed = copy.deepcopy(self.metadata)
        changed['aliases'][0], changed['aliases'][-1] = changed['aliases'][-1], changed['aliases'][0]
        with self.assertRaisesRegex(ValueError, 'compatibility_alias_phase_order'):
            validate_metadata(changed)
        changed = copy.deepcopy(self.metadata)
        changed['aliases'].insert(1, changed['aliases'][0].copy())
        with self.assertRaisesRegex(ValueError, 'duplicate_compatibility_alias'):
            validate_metadata(changed)

    def test_area_select_offers_save_point_areas_and_keeps_mirror_minimaps(self):
        rows = self.metadata['areas']
        self.assertEqual([row['area'] for row in rows if row['selectable']], [2, 3, 8, 9])
        by_area = {row['area']: row for row in rows}
        self.assertEqual(by_area[2]['initial_nav'], 'AP02_dw02_02')
        self.assertEqual(by_area[9]['initial_nav'], 'AP02_dw09_01')
        for area in (4, 5, 6):
            self.assertFalse(by_area[area]['selectable'])
            self.assertEqual(by_area[area]['initial_nav'], f'AP02_dw{area:02}_01')
        for area in (7, 11):
            self.assertFalse(by_area[area]['selectable'])
            self.assertEqual(by_area[area]['initial_nav'], 'None')
        changed = copy.deepcopy(self.metadata)
        changed['areas'][0]['package'] = changed['areas'][1]['package']
        with self.assertRaisesRegex(ValueError, 'duplicate_runtime_area'):
            validate_metadata(changed)


if __name__ == '__main__':
    unittest.main()
