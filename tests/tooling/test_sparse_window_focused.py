"""Preparation-only gates using labelled synthetic owners, not new GPU evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT/'validation/network/native-wave-candidate/windowed'
spec = importlib.util.spec_from_file_location('window_focused_test', HERE/'run_focused.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
PRIOR = ROOT/'validation/network/results/native-wave-20260917/batch-launch-focused-reaudit-v1/records/ARCH-native-wave-v4-20260916/batch-launch-focused-v1'


def fixture(output):
    """Old actual transcripts plus SYNTHETIC window metadata for parser testing."""
    original = module.load_validator()
    factory = Path('/synthetic/window-factory-v1')
    record = dict(status='passed', profile='focused', steps=4, duration=1e-10,
                  runtime_timeout_seconds=1800, commands=[], runs=[], owners={},
                  original_harness_sha256=module.HARNESS_SHA, original_validator_sha256=module.VALIDATOR_SHA,
                  paged_ode_qualified=False, application_qualified=False,
                  performance_qualified=False, release_qualified=False)
    for network in (150, 200):
        for method in module.METHODS:
            name = f'audit{network}-{method}-pool2'
            text = (PRIOR/(name+'.stdout')).read_text()
            metrics = [line for line in text.splitlines() if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]
            lane_bytes = int(next(line for line in metrics if line.startswith('metrics,')).split(',')[8])
            text += f'\nWINDOW_OWNER selected_window=32 actual_capacity=2 native_capacity=2 workspace_bytes={2*lane_bytes} workspace_budget=33554432 device_warp=32\n'
            (output/(name+'.stdout')).write_text(text)
            record['commands'].append(dict(name=name,
                command=[str(factory/f'arch_cuda_generated_sparse_burn_audit{network}'),
                         *original.trajectory_args('focused', method, 2)],
                cwd=str(factory), environment={'ARCH_NATIVE_WINDOW_CELLS': '32'}, timeout_seconds=1800,
                status='passed', returncode=0, timed_out=False, elapsed_seconds=1.0))
            record['runs'].append(dict(name=name, passed=True, metrics=metrics))
            record['owners'][name] = module.parse_owner(text)
    return record, factory


class FocusedPreparationTests(unittest.TestCase):
    def test_original_validator_and_harness_pins(self):
        module.load_validator()
        self.assertEqual(module.sha(ROOT/'build/native-wave-factory-focused-v1-verified/raw/ARCH-native-wave-v4-20260916/source/tests/cuda/test_generated_sparse_burn.cpp'), module.HARNESS_SHA)

    def test_complete_synthetic_execution_retains_limited_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            value = module.validate_transcript(record, output, factory, 0)
            self.assertTrue(value['trajectory_matrix_pass'])
            self.assertEqual(len(value['completed_harnesses']), 6)
            for name in ('paged_ode_qualified', 'application_qualified', 'performance_qualified', 'release_qualified'):
                self.assertFalse(value[name])

    def test_original_numerical_budget_not_relaxed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            name = record['runs'][0]['name']
            old = record['runs'][0]['metrics'][-1]
            fields = old.split(',')
            fields[4] = '3e-10'
            altered = ','.join(fields)
            record['runs'][0]['metrics'][-1] = altered
            path = output/(name+'.stdout')
            path.write_text(path.read_text().replace(old, altered))
            with self.assertRaisesRegex(ValueError, 'budget exceeded'):
                module.validate_transcript(record, output, factory, 0)

    def test_execution_or_scope_mutations_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            for field, value in (('command', ['old-factory']), ('cwd', '/wrong'),
                                 ('environment', {'ARCH_NATIVE_WINDOW_CELLS': '64'}),
                                 ('timeout_seconds', 9999), ('returncode', 1), ('timed_out', True),
                                 ('elapsed_seconds', float('nan'))):
                changed = copy.deepcopy(record)
                changed['commands'][0][field] = value
                with self.subTest(field=field), self.assertRaises(ValueError):
                    module.validate_transcript(changed, output, factory, 0)
            for key in ('paged_ode_qualified', 'application_qualified', 'performance_qualified', 'release_qualified'):
                changed = copy.deepcopy(record)
                changed[key] = True
                with self.subTest(key=key), self.assertRaises(ValueError):
                    module.validate_transcript(changed, output, factory, 0)
            changed = copy.deepcopy(record)
            changed['commands'].append(copy.deepcopy(record['commands'][0]))
            with self.assertRaises(ValueError): module.validate_transcript(changed, output, factory, 0)

    def test_synthetic_owner_requires_capacity_budget_warp_and_transcript_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            path = output/(record['runs'][0]['name']+'.stdout')
            text = path.read_text()
            for before, after in (('selected_window=32', 'selected_window=64'),
                                  ('actual_capacity=2', 'actual_capacity=3'),
                                  ('native_capacity=2', 'native_capacity=32'),
                                  ('workspace_budget=33554432', 'workspace_budget=67108864'),
                                  ('device_warp=32', 'device_warp=1')):
                with self.subTest(before=before), self.assertRaises(ValueError):
                    module.parse_owner(text.replace(before, after))
            with self.assertRaises(ValueError): module.parse_owner(text+text)
            record['owners'][record['runs'][0]['name']]['workspace_bytes'] += 1
            with self.assertRaisesRegex(ValueError, 'owner record differs'):
                module.validate_transcript(record, output, factory, 0)

    def test_backup_gate_rejects_missing_partial_or_tampered_prior_archives(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            collection = dict(factory_build_pass=True, worker_exit_code=0, fresh_factories=2,
                              nuclear_qualified=False, performance_qualified=False, release_qualified=False)
            for kind in ('raw', 'compact'):
                path = root/(kind+'.synthetic')
                path.write_bytes(b'synthetic archive identity fixture')
                collection[kind] = dict(path=str(path), bytes=path.stat().st_size, sha256=module.sha(path))
            receipt = dict(status='both_archives_and_all_members_byte_verified', raw=collection['raw'], compact=collection['compact'])
            parent = root/'window-factory-collection-v1.json'
            local = root/'window-factory-local-receipt-v1.json'
            parent.write_text(json.dumps(collection))
            local.write_text(json.dumps(receipt))
            module.factory_gate(root)
            for key, value in (('factory_build_pass', False), ('fresh_factories', 1), ('nuclear_qualified', True)):
                bad = dict(collection, **{key: value})
                parent.write_text(json.dumps(bad))
                with self.subTest(key=key), self.assertRaises(ValueError): module.factory_gate(root)
            parent.write_text(json.dumps(collection))
            Path(collection['raw']['path']).write_bytes(b'changed')
            with self.assertRaises(ValueError): module.factory_gate(root)


if __name__ == '__main__':
    unittest.main()
