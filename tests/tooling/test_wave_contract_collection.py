"""Synthetic collection integrity checks; no CUDA execution."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('wave_contract_collection',
    ROOT / 'validation/network/native-wave-candidate/batched-kernels/collect_contracts.py')
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class ContractCollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        tests = {'kernel-matrix': dict(passed=True, cases=128)}
        for n in (151, 201):
            for p in (1, 2, 8, 32):
                name = f'provider-n{n}-c{p}'
                tests[name] = dict(passed=True)
                (self.output / (name + '.stdout')).write_text(f'SPARSE_WAVE_CONTRACT_PASS extent={n} capacity={p} fixture=1\n')
        lines = [f'WAVE_KERNEL_BITWISE_PARITY_PASS extent={n} capacity={p} scenario={s} all_active={a}'
                 for n in (1, 151, 201, 513) for p in (1, 2, 8, 32) for s in range(4) for a in (0, 1)]
        lines += ['WAVE_KERNEL_MATRIX_PASS cases=128 scope=execution-equivalence-not-nuclear-or-performance']
        (self.output / 'kernel-matrix.stdout').write_text('\n'.join(lines))
        self.record = dict(status='kernel-and-provider-contracts-passed', identities_verified_after=True,
            tests=tests, artifacts={str(i): 'fixture' for i in range(7)},
            commands=[dict(name=name, status='passed', returncode=0) for name in tests])

    def test_complete_synthetic_matrix(self):
        self.assertEqual(len(collector.KERNEL_COMBINATIONS), 128)
        collector.require_complete(self.record, self.output)

    def test_missing_case_cannot_hide_behind_count(self):
        path = self.output / 'kernel-matrix.stdout'
        lines = path.read_text().splitlines()
        lines[1] = lines[0]
        path.write_text('\n'.join(lines))
        with self.assertRaises(ValueError):
            collector.require_complete(self.record, self.output)

    def test_failed_command_not_promoted(self):
        self.record['commands'][0]['returncode'] = 1
        with self.assertRaises(ValueError):
            collector.require_complete(self.record, self.output)

    def test_missing_artifact_not_promoted(self):
        self.record['artifacts'].pop('0')
        with self.assertRaises(ValueError):
            collector.require_complete(self.record, self.output)


if __name__ == '__main__':
    unittest.main()
