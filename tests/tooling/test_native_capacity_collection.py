"""Synthetic collection tests only; never a numerical/GPU qualification."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / 'validation/network/native-wave-candidate'
sys.path.insert(0, str(DIRECTORY))
SPEC = importlib.util.spec_from_file_location('native_capacity_collection', DIRECTORY / 'collect_capacity.py')
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class CapacityCollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.record = dict(steps=4, duration=1e-10, status='failed', commands=[], runs=[])

    def add_run(self, n=150, method='be_nr', pool=8):
        name = f'audit{n}-{method}-pool{pool}'
        metrics = [f'{kind},1,{storage},{step},0,0,0' for storage in (32, 33)
                   for step in range(4) for kind in ('cpu_step', 'gpu_step')]
        (self.output / (name + '.stdout')).write_text('\n'.join(metrics + ['GENERATED_SPARSE_BURN_PARITY_PASS']))
        self.record['commands'].append(dict(name=name, returncode=0, timed_out=False,
            command=['exe', '1e7', '3e9', '1e-10', '1e8', '1e-7', '4', '--ode', method,
                     '--storage-cells', '32', '33', '--pool-cells', str(pool), 'c12=0.5', 'o16=0.5']))
        self.record['runs'].append(dict(name=name, passed=True, metrics=metrics))
        return name

    def test_timeout_stays_failed(self):
        status = collector.matrix_status(self.record, self.output, 1)
        self.assertFalse(status['complete_matrix'])
        self.assertEqual(status['completed_harnesses'], [])

    def test_partial_success_not_complete(self):
        self.add_run()
        self.assertFalse(collector.matrix_status(self.record, self.output, 1)['complete_matrix'])
        with self.assertRaises(ValueError):
            collector.matrix_status(self.record, self.output, 0)

    def test_complete_matrix_requires_twelve(self):
        for n in (150, 200):
            for method in ('be_nr', 'bd', 'ros4'):
                for pool in (8, 32):
                    self.add_run(n, method, pool)
        self.record['status'] = 'passed'
        self.assertTrue(collector.matrix_status(self.record, self.output, 0)['complete_matrix'])

    def test_changed_or_shortened_trajectory_rejected(self):
        self.add_run()
        self.record['commands'][0]['command'][3] = '1e-11'
        with self.assertRaises(ValueError):
            collector.matrix_status(self.record, self.output, 1)

    def test_changed_transcript_rejected(self):
        name = self.add_run()
        with (self.output / (name + '.stdout')).open('a') as stream:
            stream.write('\ngpu_step,1,33,4,0,0,0\n')
        with self.assertRaises(ValueError):
            collector.matrix_status(self.record, self.output, 1)

    def test_timed_out_command_cannot_be_passed(self):
        self.add_run()
        self.record['commands'][0]['timed_out'] = True
        with self.assertRaises(ValueError):
            collector.matrix_status(self.record, self.output, 1)


if __name__ == '__main__':
    unittest.main()
