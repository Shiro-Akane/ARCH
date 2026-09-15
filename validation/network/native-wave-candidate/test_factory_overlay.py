"""Mechanical gate tests using explicitly synthetic receipts; no CUDA/processes.

Fixture PASS strings below test the parser only. They are never written to a
real qualification directory or presented as C++/GPU/ODE validation results.
"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


factory = load('prepare_factory_overlay')
overlay = load('make_overlay')


class FactoryOverlayContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / 'payload'
        self.evidence = self.root / 'synthetic-parser-fixture-not-GPU-evidence'
        self.evidence.mkdir()
        self.receipt = self.evidence / 'record.json'
        self.manifest = HERE / 'shared-inputs.json'
        self.original = overlay.prepare(ROOT, HERE, self.payload)
        tests = {f'contract-n{n}-c{c}': dict(passed=True, extent=n, capacity=c)
                 for n, c in factory.standalone.MATRIX}
        for name, test in tests.items():
            (self.evidence / (name + '.stdout')).write_text(
                f'SPARSE_WAVE_CONTRACT_PASS extent={test["extent"]} capacity={test["capacity"]} synthetic-parser-fixture\n')
        names = list(tests) + ['hardware', 'host-compiler', 'cuda-compiler', 'kernel',
            'compile-provider', 'compile-shared-math', 'compile-test', 'link-test', 'dynamic-libraries']
        artifacts = {}
        for name in factory.ARTIFACT_NAMES:
            path = self.evidence / name
            path.write_bytes(b'synthetic parser fixture; NOT an executable or object')
            artifacts[str(path)] = factory.standalone.sha(path)
        self.record = dict(status='standalone-provider-contracts-passed',
            scope='standalone-native-wave-provider-not-ODE-or-ARCH', release_qualified=False,
            identities_verified_after_run=True, payload=self.original, tests=tests,
            commands=[dict(name=name, status='passed', returncode=0) for name in names],
            inputs={str(path.resolve()): factory.standalone.sha(path) for path in
                    (self.payload / 'overlay-record.json', self.manifest)}, artifacts=artifacts)
        self.save()

    def save(self):
        self.receipt.write_text(json.dumps(self.record))

    def verify(self):
        return factory.verify_contract_receipt(self.receipt, self.payload, self.manifest)

    def prepare(self, output=None):
        return factory.prepare(ROOT, self.payload, self.receipt, self.manifest,
                               output or self.root / 'isolated-overlay')

    def test_cmake_changes_one_source_only(self):
        original = (ROOT / factory.CMAKE).read_bytes().replace(b'\r\n', b'\n')
        for raw in (original, original.replace(b'\n', b'\r\n')):
            after = factory.cmake_overlay(raw)
            self.assertEqual(after.count(factory.ADDITION), 1)
            self.assertEqual(after.replace(factory.ADDITION, b'', 1), original)

    def test_changed_cmake_cannot_be_fuzzy_patched(self):
        raw = (ROOT / factory.CMAKE).read_bytes()
        with self.assertRaisesRegex(ValueError, 'CMake identity'):
            factory.cmake_overlay(raw + b'\n# changed\n')

    def test_failed_standalone_cannot_prepare(self):
        self.record['status'] = 'failed'
        self.save()
        with self.assertRaisesRegex(ValueError, 'matching real standalone'):
            self.prepare()
        self.assertFalse((self.root / 'isolated-overlay').exists())

    def test_missing_one_of_eight_contracts_rejected(self):
        del self.record['tests']['contract-n201-c32']
        self.save()
        with self.assertRaisesRegex(ValueError, 'all eight'):
            self.verify()

    def test_duplicate_command_ledger_rejected(self):
        self.record['commands'].append(self.record['commands'][0])
        self.save()
        with self.assertRaisesRegex(ValueError, 'command ledger'):
            self.verify()

    def test_missing_completion_marker_rejected(self):
        (self.evidence / 'contract-n201-c32.stdout').write_text('return code zero is not qualification\n')
        with self.assertRaisesRegex(ValueError, 'completion marker'):
            self.verify()

    def test_changed_artifact_rejected(self):
        (self.evidence / 'test_sparse_wave').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'product identity'):
            self.verify()

    def test_missing_fresh_shared_math_object_rejected(self):
        del self.record['artifacts'][str(self.evidence / 'SparseEquilibration.cu.o')]
        self.save()
        with self.assertRaisesRegex(ValueError, 'four fresh'):
            self.verify()

    def test_old_payload_receipt_cannot_authorize_new_scheduler(self):
        self.record['payload'] = {**self.original, 'recipe_sha256': 'old-candidate'}
        self.save()
        with self.assertRaisesRegex(ValueError, 'matching real standalone'):
            self.verify()

    def test_payload_modified_after_qualification_rejected(self):
        path = self.payload / 'src/cuda/microphysics/CuDssSparseWaveSolver.cpp'
        path.write_bytes(path.read_bytes() + b'\n// changed\n')
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            self.verify()

    def test_missing_input_binding_rejected(self):
        del self.record['inputs'][str((self.payload / 'overlay-record.json').resolve())]
        self.save()
        with self.assertRaisesRegex(ValueError, 'not bound'):
            self.verify()

    def test_existing_partial_output_preserved(self):
        out = self.root / 'isolated-overlay'
        out.mkdir()
        sentinel = out / 'partial'
        sentinel.write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'new output'):
            self.prepare()
        self.assertEqual(sentinel.read_bytes(), b'preserve')

    def test_canonical_and_evidence_outputs_rejected(self):
        for out in (ROOT / 'src' / 'uncreated-overlay', self.evidence / 'uncreated-overlay',
                    self.payload / 'uncreated-overlay'):
            with self.subTest(path=out), self.assertRaisesRegex(ValueError, 'overlaps'):
                self.prepare(out)
            self.assertFalse(out.exists())

    def test_isolated_overlay_preserves_payload_and_production(self):
        before = (ROOT / factory.CMAKE).read_bytes()
        record = self.prepare()
        self.assertEqual(record['status'], 'prepared_factory_overlay_not_built_not_ODE_validated')
        self.assertFalse(record['release_qualified'])
        self.assertFalse(record['old_factory_objects_qualify_new_scheduler'])
        self.assertTrue(record['new_build_directory_required'])
        self.assertIsNone(record['pending'])
        self.assertEqual(len(record['files']), 14)
        self.assertEqual((ROOT / factory.CMAKE).read_bytes(), before)
        for relative in self.original['files']:
            self.assertEqual((self.root / 'isolated-overlay' / relative).read_bytes(),
                             (self.payload / relative).read_bytes())


if __name__ == '__main__':
    unittest.main()
