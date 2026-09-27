import copy
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'validation/network'))
import run_large_application_timing as large


class LargeApplicationTimingTests(unittest.TestCase):
    def states(self, count=150):
        shape = (2, 16)
        fractions = np.zeros((count, *shape))
        fractions[:2] = 0.5
        before = dict(rho=np.full(shape, 1e7), eng=np.full(shape, 3e24),
            X=fractions, rhoX=fractions * 1e7, enuc_rate=np.zeros(shape),
            A=np.arange(1, count + 1, dtype=float), Z=np.arange(1, count + 1, dtype=float) // 2,
            names=np.array([f'isotope{i}' for i in range(count)]),
            **{k: np.zeros(shape) for k in ('mom_u', 'mom_v', 'mom_w')})
        after = copy.deepcopy(before)
        after['X'][0] -= 1e-4
        after['X'][1] += 1e-4
        after['rhoX'] = after['X'] * after['rho']
        return before, after

    def cases(self):
        return json.loads((ROOT / 'validation/network/large_runtime_cases.json').read_text())['cases']

    def test_cpu_thread_sweep_has_one_fixed_gpu_host(self):
        self.assertEqual(large.configurations([1, 8, 16], 8),
            [('cpu', 1), ('cpu', 8), ('cpu', 16), ('cuda', 8)])
        for threads, gpu in (([], 8), ([0], 8), ([8, 8], 8), ([8], 0)):
            with self.assertRaises(RuntimeError):
                large.configurations(threads, gpu)

    def test_only_original_two_networks_and_real_sparse_routes_are_accepted(self):
        self.assertEqual([large.expected_species(c) for c in self.cases()], [150] * 3 + [200] * 3)
        for key, value in (('network', 'aprox13'), ('eos', 'ideal'), ('linear', 'denselu')):
            case = copy.deepcopy(self.cases()[0])
            case['plan_policy'][key] = value
            with self.assertRaises(RuntimeError):
                large.expected_species(case)

    def test_weak_charge_change_is_not_rejected_as_aprox13_conservation(self):
        for count in (150, 200):
            with patch.object(large.timing, 'burn_balance', side_effect=AssertionError('wrong network oracle')):
                result = large.trajectory_quality(*self.states(count), count, 1e-8)
            self.assertGreater(result['max_ye_change'], 0)
            self.assertFalse(result['require_constant_ye'])
            self.assertFalse(result['independent_weak_source_energy_oracle'])

    def test_no_burn_cannot_be_a_fast_pass(self):
        before, _ = self.states()
        with self.assertRaisesRegex(RuntimeError, 'measurably burn'):
            large.trajectory_quality(before, before, 150, 1e-8)

    def test_nan_momentum_density_and_bounds_are_rejected(self):
        for mutate in (lambda s: s['eng'].fill(np.nan), lambda s: s['eng'].fill(0),
                       lambda s: s['mom_u'].fill(1), lambda s: s['rho'].fill(2e7),
                       lambda s: s['X'][0].fill(2), lambda s: s['enuc_rate'].fill(np.inf),
                       lambda s: s['rhoX'].fill(1)):
            before, after = self.states()
            mutate(after)
            with self.assertRaises(RuntimeError):
                large.trajectory_quality(before, after, 150, 1e-8)

    def test_layout_species_metadata_and_original_budget_are_bound(self):
        before, after = self.states()
        for count, budget in ((149, 1e-8), (150, 1e-6)):
            with self.assertRaises(RuntimeError):
                large.trajectory_quality(before, after, count, budget)
        for key in ('A', 'Z', 'names'):
            changed = copy.deepcopy(after)
            changed[key] = changed[key][::-1]
            with self.assertRaises(RuntimeError):
                large.trajectory_quality(before, changed, 150, 1e-8)

    def test_original_matrix_requires_all_observation_steps_and_scientific_gate(self):
        cases = self.cases()
        report = dict(identity_verified_after_run=True, manifest_sha256='manifest', provenance='before',
            cases=[dict(id=c['id'], checkpoints=[dict(cpu=dict(steps=s), cuda=dict(steps=s), parity=dict(passed=True))
                for s in c['accepted_steps']], scientific=dict(parity=dict(passed=True))) for c in cases])
        with patch.object(large.provenance, 'require_unchanged') as compare:
            large.require_original_gate(report, cases, 'manifest', 'current')
            compare.assert_called_once_with('before', 'current')
            mutations = (
                lambda r: r.update(identity_verified_after_run=False),
                lambda r: r.update(manifest_sha256='different'),
                lambda r: r['cases'][0]['checkpoints'][0]['cpu'].update(steps=2),
                lambda r: r['cases'][0]['scientific']['parity'].update(passed=False),
                lambda r: r['cases'].append(r['cases'][0]))
            for mutate in mutations:
                bad = copy.deepcopy(report)
                mutate(bad)
                with self.assertRaises(RuntimeError):
                    large.require_original_gate(bad, cases, 'manifest', 'current')

    def test_changed_source_binary_is_not_qualified_by_matching_fields(self):
        with patch.object(large.provenance, 'require_unchanged', side_effect=RuntimeError('changed identity')):
            with self.assertRaisesRegex(RuntimeError, 'changed identity'):
                large.require_original_gate(dict(identity_verified_after_run=True, manifest_sha256='m',
                    provenance='before'), [], 'm', 'after')


if __name__ == '__main__':
    unittest.main()
