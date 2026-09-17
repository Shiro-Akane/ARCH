"""Synthetic archive qualification checks; not compilation or GPU evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import statistics
import sys
import tempfile
import unittest
from unittest.mock import patch

from test_leaf_inline_diagnostic import ARCHIVE, DIRECTORY, sample_output, sample_snapshot

saved_path = sys.path[:]
sys.path[:0] = [str(DIRECTORY), str(DIRECTORY.parent)]
try:
    spec = importlib.util.spec_from_file_location('leaf_collection', DIRECTORY / 'collect_leaf.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path[:] = saved_path


class LeafCollectionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.out = self.root / 'leaf-inline-v1'
        self.out.mkdir()
        (self.root / 'factory-release').mkdir()
        original = ARCHIVE / 'ARCH-native-wave-v4-20260916/factory-release/compile_commands.json'
        rows = json.loads(original.read_text())
        (self.root / 'factory-release/compile_commands.json').write_bytes(original.read_bytes())
        self.patcher = patch.object(module, 'ROOT', self.root)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)
        self.record = dict(status='passed', diagnostic_numerical_pass=True, identities_verified_after=True,
            budget=2e-10, order=list(module.VARIANTS), compositions=['co', 'uniform'], rho=1e7, temperature=3e9,
            performance_qualified=False, application_qualified=False, release_qualified=False,
            commands=[], runs=[], comparisons=[])
        for network in (150, 200):
            for variant in ('baseline', 'inline'):
                command = module.compile_recipe(rows, network, self.out / 'timed_network_math.cu',
                    self.out / f'audit{network}-{variant}/NetCustom_audit{network}.h',
                    self.out / f'leaf-audit{network}-{variant}')
                self.record['commands'].append(dict(name=f'compile-audit{network}-{variant}', command=command,
                    environment={}, timeout_seconds=12000, returncode=0, timed_out=False,
                    cwd=str(self.root / 'factory-release')))
            for composition in ('co', 'uniform'):
                for ordinal, variant in enumerate(module.VARIANTS):
                    name = f'audit{network}-{composition}-{ordinal}-{variant}'
                    binary = self.out / (name + '.bin')
                    binary.write_bytes(sample_snapshot(network))
                    text = sample_output(network, composition)
                    (self.out / (name + '.stdout')).write_text(text)
                    self.record['commands'].append(dict(name=name,
                        command=[str(self.out / f'leaf-audit{network}-{variant}'), '1e7', '3e9'],
                        environment={'ARCH_LEAF_COMPOSITION': composition, 'ARCH_LEAF_SNAPSHOT': str(binary)},
                        timeout_seconds=300, returncode=0, timed_out=False, cwd=str(self.root / 'factory-release')))
                    parsed = module.parse_output(text, network, composition)
                    vectors = module.snapshot(binary.read_bytes(), network)
                    parsed.update(name=name, variant=variant, composition=composition, network=network,
                        comparison_to_first_baseline=module.compare_snapshots(vectors, vectors),
                        host_device_errors={key: value['host_device_error'] for key, value in vectors['fields'].items()})
                    self.record['runs'].append(parsed)
                self.record['comparisons'].append(dict(network=network, composition=composition,
                    median_gpu_ms={'baseline': 2.0, 'inline': 2.0}, baseline_over_inline=1.0))

    def test_full_fixture_is_diagnostic_only(self):
        result = module.validate(self.record, self.out)
        self.assertEqual(result['leaf_runs'], 16)
        self.assertTrue(result['diagnostic_numerical_pass'])
        self.assertFalse(result['trajectory_matrix_pass'])
        self.assertFalse(result['performance_qualified'])

    def test_failed_missing_or_modified_commands_cannot_pass(self):
        for mutation in ('failure', 'missing', 'fast_math', 'wrong_input', 'wrong_environment'):
            record = copy.deepcopy(self.record)
            if mutation == 'failure':
                record['commands'][0]['returncode'] = 1
            elif mutation == 'missing':
                record['commands'].pop()
            elif mutation == 'fast_math':
                record['commands'][0]['command'].append('--use_fast_math')
            elif mutation == 'wrong_input':
                record['commands'][2]['command'][-1] = '4e9'
            else:
                record['commands'][2]['environment']['LD_PRELOAD'] = '/unapproved.so'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                module.validate(record, self.out)

    def test_cannot_expand_budget_or_scope(self):
        for key, value in (('budget', 1e-8), ('rho', 1e8), ('status', 'failed'),
                           ('performance_qualified', True), ('order', ['inline']*4)):
            record = copy.deepcopy(self.record)
            record[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                module.validate(record, self.out)

    def test_summary_must_match_actual_samples_and_vectors(self):
        for section in ('runs', 'comparisons'):
            record = copy.deepcopy(self.record)
            if section == 'runs':
                record['runs'][0]['median_gpu_ms'] = 0.1
            else:
                record['comparisons'][0]['baseline_over_inline'] = 10.0
            with self.subTest(section=section), self.assertRaises(ValueError):
                module.validate(record, self.out)

    def test_complete_output_snapshot_required(self):
        path = self.out / 'audit200-uniform-3-baseline.bin'
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            module.validate(self.record, self.out)


if __name__ == '__main__':
    unittest.main()
