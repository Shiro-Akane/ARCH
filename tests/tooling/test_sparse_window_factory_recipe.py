"""Reversible execution-header preparation; not compiler/CUDA qualification."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT/'validation/network/native-wave-candidate/windowed'
spec = importlib.util.spec_from_file_location('window_factory_preparation', HERE/'prepare_factory.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
SOURCE = ROOT/'validation/network/results/native-wave-20260917/factory-focused-v1/records/ARCH-native-wave-v4-20260916/source'


class WindowFactoryTests(unittest.TestCase):
    def test_original_pins_and_all_device_math_regions_unchanged(self):
        name = 'src/cuda/microphysics/SparseOdeBatch.cuh'
        original = (SOURCE/name).read_bytes()
        changed = module.transform(original, name)
        begin = b'namespace arch::cuda {'
        end = b'class SparseOdeBatchExecutor'
        self.assertEqual(original.split(begin, 1)[1].split(end, 1)[0],
                         changed.split(begin, 1)[1].split(end, 1)[0])
        self.assertEqual(changed.count(b'CuDssSparseWindowSolver'), 3)
        self.assertNotIn(b'CuDssSparseWaveSolver', changed)
        self.assertIn(b'batch_.capacity, std::min(batch_.capacity, 32), Network::ODE_NEQ', changed)
        for old in (original+b' ', original.replace(b'count == 0', b'count <= 0')):
            with self.assertRaises(ValueError): module.transform(old, name)

    def test_owner_memory_and_selector_do_not_change_execution_or_ownership(self):
        name = 'src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh'
        original = (SOURCE/name).read_bytes()
        changed = module.transform(original, name)
        for marker in (b'    ~TypedSparseBurnOwner()', b'        rows_.allocate(pattern_.row_offsets.size());'):
            self.assertEqual(original.split(marker, 1)[1], changed.split(marker, 1)[1])
        self.assertIn(b'constexpr std::size_t workspace_budget = 32ull*1024*1024;', changed)
        self.assertIn(b'if (warp != 32)', changed)
        for value in (b'"32"', b'"64"', b'"128"'):
            self.assertIn(b'std::strcmp(selected, '+value, changed)
        self.assertIn(b'actual_capacity=', changed)
        self.assertNotIn(b'cudaDeviceSetLimit', changed)
        self.assertNotIn(b'cudaDeviceSetCacheConfig', changed)

    def test_no_other_headers_or_replica_math_allowed(self):
        self.assertEqual(len(module.PINS), 2)
        with self.assertRaises(ValueError): module.transform(b'', 'src/numerics/burnsolver/ode_bd.h')
        with self.assertRaises(ValueError): module.transform(b'', 'CudaBackend.cu')


if __name__ == '__main__':
    unittest.main()
