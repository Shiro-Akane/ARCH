"""Old capacity transcripts plus SYNTHETIC owners; no new CUDA evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / 'validation/network/native-wave-candidate/windowed/capacity_protocol.py'
spec = importlib.util.spec_from_file_location('window_capacity_protocol_test', PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
PRIOR = ROOT / ('validation/network/results/native-wave-20260917/capacity-v2/'
                'records/ARCH-native-wave-v4-20260916/capacity-v2')


def fixture(output):
    focused = module.focused_helpers()
    validator = focused.load_validator()
    factory = Path('/synthetic/window-factory-v1')
    record = dict(status='passed', profile='capacity', selected_window=32,
                  steps=4, duration=1e-10, runtime_timeout_seconds=7200,
                  commands=[], runs=[], owners={},
                  original_harness_sha256=focused.HARNESS_SHA,
                  original_validator_sha256=focused.VALIDATOR_SHA,
                  **{key: False for key in module.UNQUALIFIED})
    for n, method, pool in module.MATRIX:
        name = f'audit{n}-{method}-pool{pool}'
        text = (PRIOR / (name + '.stdout')).read_text()
        metrics = [line for line in text.splitlines() if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]
        lane = int(metrics[-1].split(',')[8])
        text += (f'\nWINDOW_OWNER selected_window=32 actual_capacity={pool} native_capacity={pool} '
                 f'workspace_bytes={pool * lane} workspace_budget=33554432 device_warp=32\n')
        (output / (name + '.stdout')).write_text(text)
        record['commands'].append(dict(name=name,
            command=[str(factory / f'arch_cuda_generated_sparse_burn_audit{n}'),
                     *validator.trajectory_args('capacity', method, pool)],
            cwd=str(factory), environment={'ARCH_NATIVE_WINDOW_CELLS': '32'},
            timeout_seconds=7200, status='passed', returncode=0, timed_out=False, elapsed_seconds=1.0))
        record['runs'].append(dict(name=name, passed=True, metrics=metrics))
        record['owners'][name] = module.parse_owner(text, pool)
    return record, factory


class CapacityProtocolTests(unittest.TestCase):
    def test_payload_preparation_is_local_pinned_and_non_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'prepared'
            command = [sys.executable, str(PATH.with_name('prepare_capacity.py')), '--output', str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads((output / 'prepared-capacity-input-v1.json').read_text())
            self.assertEqual(report['status'], 'prepared_locally_not_uploaded_or_executed')
            self.assertFalse(report['gpu_executed'])
            self.assertEqual(report['files']['run_focused.py']['sha256'], module.FOCUSED_SHA)
            with tarfile.open(output / 'window-capacity-input-v1.tar') as tar:
                self.assertEqual(len(tar.getmembers()), 7)
                for member in tar:
                    self.assertTrue(member.isfile())
                    self.assertTrue(member.name.startswith('window-capacity-input-v1/'))
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)

    def test_worker_keeps_original_guard_and_dispatch_requires_focused_receipt(self):
        worker = PATH.with_name('capacity-worker.sh').read_text()
        dispatch = PATH.with_name('capacity-dispatch.sh').read_text()
        for part in ('--min-available-mib 32768', '--max-swap-growth-mib 64', '--pressure-guard',
                     '--gpu-memory-device 0', 'OMP_NUM_THREADS=8', 'OMP_DYNAMIC=FALSE',
                     'recipes-check-after.log', 'runtime-exit-code', '26h'):
            self.assertIn(part, worker)
        self.assertIn('window-focused-local-receipt-v1.json', dispatch)
        self.assertIn('test ! -e "$control"', dispatch)
        for path in ('run_capacity.py', 'collect_capacity.py'):
            compile(PATH.with_name(path).read_bytes(), path, 'exec')

    def test_original_profile_and_twelve_case_scope(self):
        original = module.focused_helpers().load_validator()
        self.assertEqual(len(module.MATRIX), 12)
        self.assertEqual(original.PROFILES['capacity']['storage'], (32, 33))
        self.assertEqual(original.PROFILES['capacity']['pools'], (8, 32))
        self.assertEqual(original.PROFILES['capacity']['steps'], 4)
        self.assertEqual(original.PROFILES['capacity']['duration'], 1e-10)
        self.assertEqual(original.PROFILES['capacity']['wall'], 7200)

    def test_synthetic_owner_success_does_not_qualify_paging_or_performance(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            result = module.validate_transcript(record, output, factory, 0)
            self.assertTrue(result['trajectory_matrix_pass'])
            self.assertEqual(len(result['completed_harnesses']), 12)
            for key in module.UNQUALIFIED:
                self.assertFalse(result[key])

    def test_original_field_budget_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            row = record['runs'][0]
            old = row['metrics'][-1]
            fields = old.split(',')
            fields[4] = '3e-10'
            row['metrics'][-1] = ','.join(fields)
            path = output / (row['name'] + '.stdout')
            path.write_text(path.read_text().replace(old, row['metrics'][-1]))
            with self.assertRaisesRegex(ValueError, 'budget exceeded'):
                module.validate_transcript(record, output, factory, 0)

    def test_no_changed_route_override_timeout_or_result(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            for key, value in (('command', ['wrong']), ('cwd', '/wrong'),
                               ('environment', {'ARCH_NATIVE_WINDOW_CELLS': '64'}),
                               ('timeout_seconds', 1800), ('returncode', 1),
                               ('timed_out', True), ('elapsed_seconds', float('inf'))):
                bad = copy.deepcopy(record)
                bad['commands'][0][key] = value
                with self.subTest(key=key), self.assertRaises(ValueError):
                    module.validate_transcript(bad, output, factory, 0)
            for key in module.UNQUALIFIED:
                bad = copy.deepcopy(record)
                bad[key] = True
                with self.subTest(key=key), self.assertRaises(ValueError):
                    module.validate_transcript(bad, output, factory, 0)

    def test_owner_budget_capacity_and_order_are_observed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            text = (output / (record['commands'][0]['name'] + '.stdout')).read_text()
            for before, after in (('actual_capacity=8', 'actual_capacity=32'),
                                  ('native_capacity=8', 'native_capacity=32'),
                                  ('selected_window=32', 'selected_window=64'),
                                  ('workspace_budget=33554432', 'workspace_budget=67108864'),
                                  ('device_warp=32', 'device_warp=64')):
                with self.subTest(before=before), self.assertRaises(ValueError):
                    module.parse_owner(text.replace(before, after), 8)
            record['commands'].reverse()
            with self.assertRaisesRegex(ValueError, 'reordered'):
                module.validate_transcript(record, output, factory, 0)

    def test_partial_failure_never_passes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            record, factory = fixture(output)
            record['status'] = 'failed'
            record['commands'] = record['commands'][:1]
            record['runs'] = record['runs'][:1]
            record['owners'] = {record['runs'][0]['name']: record['owners'][record['runs'][0]['name']]}
            result = module.validate_transcript(record, output, factory, 1)
            self.assertFalse(result['trajectory_matrix_pass'])
            with self.assertRaises(ValueError):
                module.validate_transcript(record, output, factory, 0)

    def test_prior_archive_completion_and_bytes_are_required(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            helpers = module.focused_helpers()
            expected = sorted(f'audit{n}-{m}-pool2' for n in (150, 200) for m in module.METHODS)
            collection = dict(profile='focused', worker_exit_code=0, trajectory_matrix_pass=True,
                              owners_verified=True, completed_harnesses=expected, planned_harnesses=expected,
                              **{key: False for key in module.UNQUALIFIED})
            for kind in ('raw', 'compact'):
                path = root / (kind + '.synthetic')
                path.write_bytes(b'synthetic prior archive, NOT GPU evidence')
                collection[kind] = dict(path=str(path), bytes=path.stat().st_size, sha256=helpers.sha(path))
            receipt = dict(status='both_archives_and_all_members_byte_verified',
                           raw=collection['raw'], compact=collection['compact'])
            parent = root / 'window-focused-collection-v1.json'
            parent.write_text(json.dumps(collection))
            (root / 'window-focused-local-receipt-v1.json').write_text(json.dumps(receipt))
            with patch.object(module, 'focused_helpers', return_value=helpers), patch.object(helpers, 'factory_gate', return_value=()):
                module.parent_gate(root)
                bad = dict(collection, trajectory_matrix_pass=False)
                parent.write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError, 'complete focused result'):
                    module.parent_gate(root)
                parent.write_text(json.dumps(collection))
                Path(collection['raw']['path']).write_bytes(b'changed')
                with self.assertRaisesRegex(ValueError, 'archive identity changed'):
                    module.parent_gate(root)


if __name__ == '__main__':
    unittest.main()
