"""Pure preparation checks, not compiled/runtime profiler validation."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('wait_probe_recipe',
    ROOT / 'validation/network/native-wave-candidate/launch-shape/prepare_wait_probe.py')
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)
BASE = ROOT / 'validation/network/results/large-scheduling-20260914/evidence/records/probe-cuda-progress-20260914.cpp'


class WaitProbeRecipeTests(unittest.TestCase):
    def test_only_labels_change(self):
        before = BASE.read_bytes()
        after = module.transform(before)
        self.assertNotEqual(before, after)
        for call in (b'native(destination,source,bytes,kind,stream)', b'native(function,grid,block,arguments,shared,stream)',
                     b'const auto start=Clock::now();', b'const auto end=Clock::now();'):
            self.assertEqual(before.count(call), after.count(call))
        self.assertIn(b'+":after:"+preceding', after)

    def test_unreviewed_base_rejected(self):
        with self.assertRaises(ValueError):
            module.transform(BASE.read_bytes() + b'\n')


if __name__ == '__main__':
    unittest.main()
