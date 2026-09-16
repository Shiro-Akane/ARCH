"""Synthetic recipe checks, not CUDA or nuclear validation."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('batch_trajectories',
    ROOT / 'validation/network/native-wave-candidate/batched-kernels/run_trajectories.py')
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class TrajectoryRecipeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)

    def matrix(self, profile):
        p = module.PROFILES[profile]
        record = dict(steps=p['steps'], duration=p['duration'], runtime_timeout_seconds=p['wall'],
                      status='passed', commands=[], runs=[])
        for n in (150, 200):
            for method, method_id in module.METHODS.items():
                for pool in p['pools']:
                    name = f'audit{n}-{method}-pool{pool}'
                    metrics = [f'{kind},{method_id},{storage},{step},8,0,0.1'
                               for storage in p['storage'] for step in range(p['steps'])
                               for kind in ('cpu_step', 'gpu_step')]
                    metrics += [f'metrics,{method_id},8,0,1e-15,1e-15,1e-5,{pool},1000']
                    controls = [f'controls,custom:audit{n},{n+1},1e7,3e9,{p["duration"]},1e8,1e-7,{p["steps"]},selected_ode,{method_id}',
                                'storage_controls,' + ','.join(map(str, (*p['storage'], pool)))]
                    (self.output / (name + '.stdout')).write_text('\n'.join(controls + metrics + ['GENERATED_SPARSE_BURN_PARITY_PASS']))
                    record['commands'].append(dict(name=name, returncode=0, timed_out=False,
                        command=['exe', *module.trajectory_args(profile, method, pool)]))
                    record['runs'].append(dict(name=name, passed=True, metrics=metrics))
        return record

    def test_exact_focused_capacity_and_long_profiles(self):
        self.assertEqual(module.PROFILES['focused']['storage'], (2, 3))
        self.assertEqual(module.PROFILES['focused']['pools'], (2,))
        self.assertEqual(module.PROFILES['long']['steps'], 16)
        self.assertEqual(module.PROFILES['long']['duration'], 1e-9)
        for profile, count in (('focused', 6), ('capacity', 12), ('long', 12)):
            record = self.matrix(profile)
            result = module.validate(profile, record, self.output, 0)
            self.assertTrue(result['trajectory_matrix_pass'])
            self.assertEqual(len(result['completed_harnesses']), count)
            self.assertFalse(result['performance_qualified'])

    def test_missing_harness_not_passed(self):
        record = self.matrix('capacity')
        record['runs'].pop()
        with self.assertRaises(ValueError):
            module.validate('capacity', record, self.output, 0)

    def test_failed_run_remains_partial(self):
        record = self.matrix('focused')
        record['status'] = 'failed'
        record['runs'].pop()
        record['commands'][-1].update(returncode=None, timed_out=True)
        self.assertFalse(module.validate('focused', record, self.output, 1)['trajectory_matrix_pass'])

    def test_shortened_long_trajectory_rejected(self):
        record = self.matrix('long')
        record['duration'] = 1e-10
        with self.assertRaises(ValueError):
            module.validate('long', record, self.output, 0)

    def test_observer_rejected(self):
        record = self.matrix('focused')
        record['observer'] = {'path': 'fake.so'}
        with self.assertRaises(ValueError):
            module.validate('focused', record, self.output, 0)

    def test_wrong_runtime_ode_rejected(self):
        record = self.matrix('focused')
        path = self.output / (record['runs'][0]['name'] + '.stdout')
        path.write_text(path.read_text().replace('selected_ode,1', 'selected_ode,2'))
        with self.assertRaises(ValueError):
            module.validate('focused', record, self.output, 0)

    def test_unchanged_composition_not_called_burning(self):
        record = self.matrix('focused')
        path = self.output / (record['runs'][0]['name'] + '.stdout')
        path.write_text(path.read_text().replace(',1e-5,2,', ',0,2,'))
        record['runs'][0]['metrics'][-1] = record['runs'][0]['metrics'][-1].replace(',1e-5,2,', ',0,2,')
        with self.assertRaises(ValueError):
            module.validate('focused', record, self.output, 0)


if __name__ == '__main__':
    unittest.main()
