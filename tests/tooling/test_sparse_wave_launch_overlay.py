"""Preparation/static-boundary checks, not CUDA compilation or runtime tests."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / 'validation/network/native-wave-candidate/batched-kernels'
SPEC = importlib.util.spec_from_file_location('wave_launch_prepare', DIRECTORY / 'prepare.py')
prepare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(prepare)
BUILD_SPEC = importlib.util.spec_from_file_location('wave_launch_build', DIRECTORY / 'build_and_test.py')
builder = importlib.util.module_from_spec(BUILD_SPEC)
BUILD_SPEC.loader.exec_module(builder)


class LaunchOverlayTests(unittest.TestCase):
    def setUp(self):
        self.original = (DIRECTORY.parent / 'CuDssSparseWaveSolver.cpp').read_bytes()

    def test_three_regions_only_and_same_contract_tail(self):
        self.assertEqual(len(builder.KERNEL_COMBINATIONS), 128)
        changed, regions = prepare.transform(self.original)
        self.assertEqual(len(regions), 3)
        anchor = b'    if (!p.analyzed)'
        self.assertEqual(self.original[self.original.index(anchor):], changed[changed.index(anchor):])
        self.assertIn(b'accumulate_sparse_correction(extent, correction.get()', changed)
        self.assertNotIn(b'cudaDeviceSynchronize', changed)

    def test_unreviewed_base_rejected(self):
        with self.assertRaises(ValueError):
            prepare.transform(self.original + b'\n')

    def test_shared_numeric_leaves_and_bounded_launch(self):
        source = (DIRECTORY / 'SparseWaveKernels.cu').read_text()
        for symbol in ('linalg::equilibration_divisor', 'linalg::equilibrated_value',
                       'linalg::sparse_residual_row'):
            self.assertIn(symbol, source)
        self.assertIn('kernel<<<batch.capacity, threads, 0, stream>>>', source)
        self.assertIn('batch.capacity > 32', source)
        self.assertEqual(source.count('__syncthreads()'), 3)
        for forbidden in ('cudssExecute', 'cudaMalloc', 'cudaStreamSynchronize',
                          'cudaDeviceSynchronize', 'atomicAdd'):
            self.assertNotIn(forbidden, source)

    def test_link_preserves_strict_flags_and_native_libraries(self):
        tokens = [':', '&&', '/usr/bin/g++-11', '-O3', '-flto=auto', '-fno-fast-math', '-ffp-contract=off',
                  'CMakeFiles/test.dir/tests/cuda/test_cudss_sparse_solver.cpp.o', '-o', 'old',
                  'libarch_cuda_sparse_provider.a', '/frozen/libcudss.so.0', '-lcudart', '&&', ':']
        result = builder.rewrite_link(tokens, '/new/test.o', '/new/provider.a', '/new/test')
        self.assertEqual(result, ['/usr/bin/g++-11', '-O3', '-flto=auto', '-fno-fast-math', '-ffp-contract=off',
                                  '/new/test.o', '-o', '/new/test', '/new/provider.a', '/frozen/libcudss.so.0', '-lcudart'])
        for invalid in (tokens + ['&&', 'touch', 'file'],
                        [token for token in tokens if token != '-ffp-contract=off'],
                        tokens[:3] + ['-ffast-math'] + tokens[3:]):
            with self.assertRaises(ValueError):
                builder.rewrite_link(invalid, '/new/test.o', '/new/provider.a', '/new/test')


if __name__ == '__main__':
    unittest.main()
