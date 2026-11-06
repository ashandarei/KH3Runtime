"""Native compatibility adapters must be registered before metadata generation."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "authoring/kh02"))
from compatibility_audit import adapter_contract, validate_adapter_coverage
from runtime_metadata import load_metadata


class CompatibilityAuditTests(unittest.TestCase):
    def setUp(self):
        self.metadata = load_metadata()

    def test_every_runtime_adapter_requires_registration(self):
        self.assertEqual(len(adapter_contract()), 40)
        validate_adapter_coverage(self.metadata)
        changed = copy.deepcopy(self.metadata)
        changed['aliases'] = [row for row in changed['aliases'] if row['handler'] != 'compat_thunk<7>']
        with self.assertRaisesRegex(ValueError, 'unregistered_compatibility_adapters:7'):
            validate_adapter_coverage(changed)
        changed['aliases'][0]['handler'] = 'compat_thunk<999>'
        with self.assertRaisesRegex(ValueError, 'unknown_compatibility_adapter'):
            validate_adapter_coverage(changed)


if __name__ == "__main__":
    unittest.main()
