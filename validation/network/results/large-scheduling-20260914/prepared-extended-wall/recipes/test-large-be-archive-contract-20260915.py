"""Local collector contract tests; no GPU, compiler, server or real tar invocation."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    'be_archive', Path(__file__).with_name('archive-large-be-extended-wall-20260914.py'))
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class CollectorContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        projects = Path(self.temp.name) / 'projects'
        root = projects / 'ARCH'
        base = root / 'build/p12'
        recipes = base / 'be-extended-wall-recipes-v1'
        run = base / 'factor-cache/long-be-extended-wall-v1'
        self.paths = dict(PROJECTS=projects, ROOT=root, BASE=base, RECIPES=recipes,
                          RUN=run, OUT=run.with_name(run.name + '-archive'),
                          PROVIDER=base / 'factor-cache/candidate-v2/provider.a',
                          OLD_BUILD=projects / 'old/build')
        self.addCleanup(patch.stopall)
        for name, path in self.paths.items():
            patch.object(collector, name, path).start()
        run.mkdir(parents=True)
        files = {
            recipes / 'validation/network/run_sparse_capacity.py': 'recipe',
            root / 'tests/cuda/test_generated_sparse_burn.cpp': 'frozen host source',
            self.paths['PROVIDER']: '!<arch>provider',
            self.paths['OLD_BUILD'] / 'compile_commands.json': '[]',
        }
        for name in ('replay-large-be-extended-wall-20260914.sh',
                     'large-be-extended-wall-protocol-20260914.json',
                     'archive-large-be-extended-wall-20260914.py',
                     'sparse-capacity-wall-budget-tests-20260914.log',
                     'sparse-capacity-wall-budget-tests-v2-20260914.log',
                     'server-recipe-tests.log', 'run-large-be-followup-worker-20260914.sh',
                     'tests/tooling/test_sparse_capacity_recipe.py'):
            files[recipes / name] = 'preserved recipe or log'
        for path, content in files.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        self.names = [f'{network}-be_nr-pool{pool}'
                      for network in ('audit150', 'audit200') for pool in (8, 32)]
        self.record = dict(
            status='passed', duration=1e-9, steps=16, runtime_timeout_seconds=21600,
            build_command_timeout_seconds=1800,
            recipe_sha256=collector.sha(recipes / 'validation/network/run_sparse_capacity.py'),
            source_sha256=collector.sha(root / 'tests/cuda/test_generated_sparse_burn.cpp'),
            provider_sha256=collector.sha(self.paths['PROVIDER']),
            compile_commands_sha256=collector.sha(self.paths['OLD_BUILD'] / 'compile_commands.json'),
            artifacts=[], runs=[dict(name=name, passed=True) for name in self.names])
        for network in ('audit150', 'audit200'):
            factory = self.paths['OLD_BUILD'] / (network + '.o')
            exe = run / ('arch_cuda_generated_sparse_burn_' + network)
            factory.write_bytes(b'\x7fELF frozen factory')
            exe.write_bytes(b'\x7fELF linked harness')
            self.record['artifacts'].append(dict(factory_path=str(factory),
                factory_sha256=collector.sha(factory), executable_path=str(exe),
                executable_sha256=collector.sha(exe)))
        for name in self.names:
            lines = [f'{prefix},0,{cells},{step},fixture'
                     for cells in (32, 33) for step in range(16)
                     for prefix in ('cpu_step', 'gpu_step')]
            (run / (name + '.stdout')).write_text('\n'.join(
                lines + ['GENERATED_SPARSE_BURN_PARITY_PASS']) + '\n')
        (run / 'failure.stderr').write_text('failure evidence must remain')
        self.write_record()
        self.tar_calls = 0
        self.mutate_during_archive = False

    def write_record(self):
        (self.paths['RUN'] / 'record.json').write_text(json.dumps(self.record))

    def fake_process(self, command, **kwargs):
        if command[0] == 'pgrep':
            return subprocess.CompletedProcess(command, 1)
        self.assertEqual(command[0], 'tar')
        self.tar_calls += 1
        Path(command[command.index('-cf') + 1]).write_bytes(b'mocked tar output')
        if self.mutate_during_archive:
            (self.paths['RUN'] / 'failure.stderr').write_text('changed during archive')
        return subprocess.CompletedProcess(command, 0)

    def collect(self):
        self.write_record()
        with patch.object(collector.subprocess, 'run', side_effect=self.fake_process), \
                contextlib.redirect_stdout(io.StringIO()):
            collector.main()
        return json.loads((self.paths['OUT'] / 'compact/raw-archive.json').read_text())

    def assert_rejected(self, message):
        with self.assertRaisesRegex(RuntimeError, message):
            self.collect()

    def test_complete_matrix_preserves_exact_storage_sequences(self):
        receipt = self.collect()
        self.assertEqual(receipt['scientific_status'], 'passed')
        self.assertTrue(receipt['complete_matrix'])
        self.assertEqual(receipt['completed_storage_trajectories'], 8)
        self.assertFalse(receipt['formal_timing'])
        self.assertFalse(receipt['independent_reaction_oracle'])
        self.assertEqual(self.tar_calls, 2)

    def test_failed_partial_matrix_is_archived_not_promoted(self):
        self.record.update(status='failed', runs=self.record['runs'][:1], error='TimeoutExpired')
        receipt = self.collect()
        self.assertEqual(receipt['scientific_status'], 'failed')
        self.assertFalse(receipt['complete_matrix'])
        self.assertEqual(receipt['completed_storage_trajectories'], 2)
        self.assertEqual((self.paths['RUN'] / 'failure.stderr').read_text(),
                         'failure evidence must remain')

    def test_nonterminal_rejected(self):
        self.record['status'] = 'running'
        self.assert_rejected('No terminal record')

    def test_physical_duration_change_rejected(self):
        self.record['duration'] = 1e-10
        self.assert_rejected('Unexpected follow-up protocol')

    def test_wall_limit_change_rejected(self):
        self.record['runtime_timeout_seconds'] = 1800
        self.assert_rejected('Unexpected follow-up protocol')

    def test_observer_rejected(self):
        self.record['observer'] = {}
        self.assert_rejected('must not use the observer')

    def test_duplicate_harness_rejected(self):
        self.record['runs'].append(self.record['runs'][0])
        self.assert_rejected('duplicate trajectory completion')

    def test_missing_harness_cannot_pass(self):
        self.record['runs'].pop()
        self.assert_rejected('complete four-harness matrix')

    def test_missing_last_gpu_step_rejected(self):
        path = self.paths['RUN'] / (self.names[0] + '.stdout')
        path.write_text(path.read_text().replace('gpu_step,0,33,15,fixture\n', ''))
        self.assert_rejected('incomplete or reordered gpu_step')

    def test_reordered_cpu_steps_rejected(self):
        path = self.paths['RUN'] / (self.names[0] + '.stdout')
        text = path.read_text().replace('cpu_step,0,32,0,fixture', 'cpu_step,0,32,1,fixture', 1)
        path.write_text(text)
        self.assert_rejected('incomplete or reordered cpu_step')

    def test_duplicate_parity_marker_rejected(self):
        path = self.paths['RUN'] / (self.names[0] + '.stdout')
        path.write_text(path.read_text() + 'GENERATED_SPARSE_BURN_PARITY_PASS\n')
        self.assert_rejected('missing unique parity completion')

    def test_changed_provider_rejected(self):
        self.paths['PROVIDER'].write_bytes(b'changed provider')
        self.assert_rejected('Identity changed')

    def test_out_of_scope_factory_rejected(self):
        path = Path(self.temp.name) / 'outside.o'
        path.write_bytes(b'\x7fELF outside')
        self.record['artifacts'][0].update(factory_path=str(path), factory_sha256=collector.sha(path))
        self.assert_rejected('Identity changed')

    def test_original_archive_never_overwritten(self):
        raw = self.paths['ROOT'] / 'build/large-be-extended-wall-v1.tar.zst'
        raw.write_bytes(b'original archive')
        self.assert_rejected('never overwritten')
        self.assertEqual(raw.read_bytes(), b'original archive')
        self.assertEqual(self.tar_calls, 0)

    def test_mutation_during_tar_cannot_get_qualified_receipt(self):
        self.mutate_during_archive = True
        self.assert_rejected('Files changed while archiving')
        self.assertFalse((self.paths['OUT'] / 'compact/raw-archive.json').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
