"""Preparation/protocol checks only; no CUDA or physics qualification."""
import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / 'validation/network/native-wave-candidate/leaf-inline'
sys.path.insert(0, str(DIRECTORY))
try:
    spec = importlib.util.spec_from_file_location('leaf_inline_run', DIRECTORY / 'run_leaf.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path.pop(0)

ARCHIVE = ROOT / 'validation/network/results/native-wave-20260917/factory-focused-v1/records'


def sample_snapshot(network=150, value=2.0):
    data = struct.pack('<QQQ', 0x415243484C454146, network+1, network+1)
    for count in (network+1, network, network, network, 3):
        values = [value] * count
        if count == 3:
            values[-1] = 1.0
        data += struct.pack('<Q', count) + struct.pack(f'<{count*2}d', *(values + values))
    return data


def sample_output(network=150, composition='co'):
    lines = [f'LEAF_INPUT composition={composition} neq={network+1} rho=1e+07 temperature=3e+09',
             'LEAF_STATIC_RESOURCES registers=48 local_bytes=128 shared_bytes=0']
    lines += [f'LEAF_EVENT_TIMING sample={i} repeats=20 cpu_ms=1.0 gpu_ms=2.0' for i in range(5)]
    lines += ['LEAF_SNAPSHOT_WRITTEN endian=little',
              f'GENERATED_NETWORK_MATH_PASS neq={network+1} nnz={network+1} rho=10000000 temperature=3000000000']
    return '\n'.join(lines)


class LeafInlineTests(unittest.TestCase):
    def test_annotation_only_in_actual_archived_adapters(self):
        for network in (150, 200):
            path = ARCHIVE / f'ARCH-large-networks-20260909/audit{network}/NetCustom_audit{network}.math.h'
            original = path.read_bytes()
            changed = module.sink_variant(original, network)
            self.assertNotEqual(changed, original)
            self.assertEqual(changed.replace(b'ARCH_INLINE void set(int row, int column, double value)',
                                            b'ARCH_HEAVY_INLINE void set(int row, int column, double value)'), original)
            with self.assertRaises(ValueError):
                module.sink_variant(changed, network)

    def test_original_driver_instrumentation(self):
        original = (ARCHIVE / 'ARCH-native-wave-v4-20260916/source/tests/cuda/test_generated_network_math.cu').read_bytes()
        changed = module.timed_driver(original)
        self.assertIn(b'2.e-10 * std::max(1.0, std::abs(expected[index]))', changed)
        for field in ('jacobian', 'rhs', 'energy_gradient', 'temperature_gradient', 'energy/temperature_energy/pattern'):
            self.assertEqual(changed.count(('compare("' + field + '"').encode()), 1)
        self.assertEqual(changed.count(b'LEAF_EVENT_TIMING'), 1)
        self.assertEqual(changed.count(b'LEAF_SNAPSHOT_WRITTEN'), 1)
        with self.assertRaises(ValueError):
            module.timed_driver(original + b'\n')

    def test_original_strict_cmake_compile_recipe(self):
        rows = json.loads((ARCHIVE / 'ARCH-native-wave-v4-20260916/factory-release/compile_commands.json').read_text())
        for network in (150, 200):
            cmd = module.compile_recipe(rows, network, '/private/test.cu', '/private/network.h', '/private/test')
            self.assertEqual(cmd[:2], ['/usr/bin/time', '-v'])
            self.assertNotIn('-c', cmd)
            self.assertIn('--fmad=false', cmd)
            self.assertIn('-O3', cmd)
            self.assertEqual(cmd[cmd.index('-include')+1], '/private/network.h')
            self.assertEqual(cmd[cmd.index('-o')+1], '/private/test')
        modified = json.loads(json.dumps(rows))
        for row in modified:
            row['command'] = row['command'].replace('--fmad=false', '--use_fast_math')
        with self.assertRaises(ValueError):
            module.compile_recipe(modified, 150, '/test.cu', '/network.h', '/test')

    def test_snapshot_complete_vectors_and_comparison(self):
        for n in (150, 200):
            result = module.snapshot(sample_snapshot(n), n)
            self.assertEqual(set(result['fields']), set(module.GROUPS))
            self.assertTrue(module.compare_snapshots(result, result)['jacobian']['gpu']['exactly_equal'])

    def test_reject_snapshot_shape_endian_and_nonfinite(self):
        good = sample_snapshot()
        for bad in (good[:20], good[:-1], good+b'extra', good.replace(b'FAELHCRA', b'ARCHELAF'),
                    sample_snapshot(value=float('nan'))):
            with self.assertRaises(ValueError):
                module.snapshot(bad, 150)
        with self.assertRaises(ValueError):
            module.snapshot(good, 200)

    def test_reject_invalid_pattern_or_empty_mathematics(self):
        good = sample_snapshot()
        with self.assertRaises(ValueError):
            module.snapshot(good[:-8] + struct.pack('<d', 0.0), 150)
        with self.assertRaises(ValueError):
            module.snapshot(sample_snapshot(value=0.0), 150)

    def test_reject_host_device_and_baseline_candidate_budget_changes(self):
        good = sample_snapshot()
        corrupted = bytearray(good)
        struct.pack_into('<d', corrupted, 32+151*8, 4.0)
        with self.assertRaises(ValueError):
            module.snapshot(corrupted, 150)
        with self.assertRaises(ValueError):
            module.compare_snapshots(module.snapshot(good, 150), module.snapshot(sample_snapshot(value=4.0), 150))

    def test_actual_protocol_inputs_and_samples(self):
        for n in (150, 200):
            for composition in ('co', 'uniform'):
                result = module.parse_output(sample_output(n, composition), n, composition)
                self.assertEqual(len(result['samples']), 5)
                self.assertEqual(result['median_gpu_ms'], 2.0)

    def test_reject_wrong_controls_missing_duplicate_or_invalid_timings(self):
        good = sample_output()
        for bad in (good.replace('composition=co', 'composition=uniform'), good.replace('3e+09', '4e+09'),
                    good.replace('repeats=20', 'repeats=1'), good.replace('sample=4', 'sample=3'),
                    good.replace('gpu_ms=2.0', 'gpu_ms=nan'), good.replace('cpu_ms=1.0', 'cpu_ms=0'),
                    good.replace('LEAF_SNAPSHOT_WRITTEN endian=little', ''),
                    good.replace('GENERATED_NETWORK_MATH_PASS', 'FAKE_PASS')):
            with self.assertRaises(ValueError):
                module.parse_output(bad, 150, 'co')

    def test_abba_and_unchanged_budget(self):
        self.assertEqual(module.VARIANTS, ('baseline', 'inline', 'inline', 'baseline'))
        self.assertEqual(module.BUDGET, 2e-10)


if __name__ == '__main__':
    unittest.main()
