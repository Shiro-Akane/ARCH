"""Source/coverage checks only; no C++ compilation or GPU execution."""
import hashlib
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'validation/network/native-wave-candidate/launch-shape/advance_shape_probe.cpp'
FROZEN = ROOT / ('validation/network/results/native-wave-20260917/factory-focused-v1/records/'
                'ARCH-native-wave-v4-20260916/source/src/cuda/microphysics/SparseOdeBatch.cuh')


class AdvanceShapeRecipeTests(unittest.TestCase):
    def test_each_original_cell_covered_exactly_once(self):
        for count in range(1, 33):
            for threads in (1, 2, 4, 8, 16, 32):
                blocks = (count + threads - 1) // threads
                actual = [b * threads + t for b in range(blocks) for t in range(threads)
                          if b * threads + t < count]
                self.assertEqual(actual, list(range(count)))

    def test_only_six_exact_frozen_symbols(self):
        source = SOURCE.read_text()
        names = re.findall(r'"(_ZN4arch4cuda18sparse_burn_detail11advance_ode[^"]+)"', source)
        expected = {'_ZN4arch4cuda18sparse_burn_detail11advance_odeI18NetCustom_audit'
                    + n + solver + '12IdealGasViewEEvNS0_18SparseOdeBatchViewIT_T0_EEiT1_14BurnConfigViewb'
                    for n in ('150', '200') for solver in ('12Solver_BE_NR', '9Solver_BD', '11Solver_ROS4')}
        self.assertEqual(len(names), 6)
        self.assertEqual(set(names), expected)
        self.assertIn('std::strcmp(name, allowed[i]) == 0', source)

    def test_frozen_device_mapping_not_modified(self):
        self.assertEqual(hashlib.sha256(FROZEN.read_bytes()).hexdigest(),
                         'ae3cc59770f4b952a31ded69734fa80304e06665648d40ef173551fa28edbe58')
        source = FROZEN.read_text()
        body = source.split('__global__ void advance_ode(', 1)[1].split('} // namespace sparse_burn_detail', 1)[0]
        self.assertIn('const int lane = blockIdx.x * blockDim.x + threadIdx.x;', body)
        self.assertIn('if (lane >= count) return;', body)
        self.assertNotRegex(body, r'__syncthreads|__syncwarp|__shfl')

    def test_shape_only_and_fail_closed(self):
        source = SOURCE.read_text()
        for call in ('cudaMalloc(', 'cudaMemcpy(', 'cudaMemcpyAsync(', 'cudaStreamSynchronize(',
                     'cudaDeviceSynchronize(', 'cudaEventRecord(', 'cudssExecute('):
            self.assertNotIn(call, source)
        for condition in ('count > 32', 'grid.x != 1', 'block.x != 32', 'shared != 0'):
            self.assertIn(condition, source)
        self.assertIn('if (index < 0) return original(function, grid, block, arguments, shared, stream);', source)
        self.assertIn('return cudaErrorInvalidConfiguration;', source)
        self.assertIn('matched_launches=%llu', source)


if __name__ == '__main__':
    unittest.main()
