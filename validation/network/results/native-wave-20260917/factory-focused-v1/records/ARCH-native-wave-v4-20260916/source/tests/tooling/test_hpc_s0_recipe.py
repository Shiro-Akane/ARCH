"""Negative controls for the bounded S0 recipe, not GPU/scientific evidence."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / 'validation/backend/results/hpc-cuda-optimization/S0/run_s0_checks.py'
spec = importlib.util.spec_from_file_location('hpc_s0_recipe', PATH)
recipe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recipe)


class S0RecipeContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name)
        self.output = self.source / 'build/output'
        self.build = self.source / 'build/release'
        self.identity = dict(source={'commit': 'fixture'}, build={'config': 'fixture'},
                             artifacts={'binary': 'fixture'}, artifact_observation={}, execution_environment={})
        self.fail = None
        self.code = 0
        self.stdout = 'ninja: no work to do.\n'

    def run_fake(self, command, *, source_root, lane_root, timeout):
        self.assertEqual(source_root, self.source)
        (lane_root / 'arch.stdout').write_text(self.stdout)
        if self.fail:
            raise self.fail
        return SimpleNamespace(returncode=self.code)

    def invoke(self, action='inspect-build', capture=None, source=None):
        argv = ['recipe', '--source-root', str(self.source), '--output-root', str(self.output),
                '--action', action, '--build-dir', str(self.build), '--target', 'ARCH', '--artifact', 'arch=bin/ARCH']
        with patch.object(sys, 'argv', argv), \
             patch.object(recipe.provenance, 'source_identity', side_effect=source or (lambda _: {'commit': 'fixture'})), \
             patch.object(recipe.provenance, 'capture_focused', side_effect=capture or (lambda **_: self.identity)), \
             patch.object(recipe.validation, 'run_arch_with_logs', side_effect=self.run_fake):
            return recipe.main()

    def status(self):
        return json.loads((self.output / 'status.json').read_text())

    def test_build_inspection_is_not_science(self):
        self.assertEqual(self.invoke(), 0)
        result = self.status()
        self.assertTrue(result['build_graph_up_to_date'])
        self.assertFalse(result['release_qualified'])
        self.assertIn('NOT retrospective science', result['scope'])
        self.assertEqual(result['commands'][0]['command'][-1], '-n')

    def test_nonzero_command_stops_and_records_failure(self):
        self.code = 7
        with self.assertRaisesRegex(RuntimeError, 'returned 7'):
            self.invoke('tooling')
        result = self.status()
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(len(result['commands']), 1)
        self.assertEqual(result['commands'][0]['returncode'], 7)

    def test_timeout_and_interrupt_are_not_left_running(self):
        for failure in (TimeoutError('timeout fixture'), KeyboardInterrupt()):
            with self.subTest(failure=type(failure).__name__):
                self.output = self.source / 'build' / type(failure).__name__
                self.fail = failure
                with self.assertRaises(type(failure)):
                    self.invoke()
                self.assertEqual(self.status()['status'], 'failed')
                self.assertNotEqual(self.status()['commands'][0]['status'], 'running')

    def test_missing_or_damaged_artifact_never_executes(self):
        for failure in (FileNotFoundError('missing fixture'), RuntimeError('damaged identity fixture')):
            self.output = self.source / 'build' / type(failure).__name__
            def capture(**_):
                raise failure
            with self.assertRaises(type(failure)):
                self.invoke(capture=capture)
            self.assertEqual(self.status()['commands'], [])

    def test_existing_output_is_never_overwritten_even_if_empty(self):
        self.output.mkdir(parents=True)
        with self.assertRaises(FileExistsError):
            self.invoke()
        self.assertEqual(list(self.output.iterdir()), [])

    def test_repeated_completion_is_not_reused(self):
        self.invoke()
        original = (self.output / 'status.json').read_bytes()
        with self.assertRaises(FileExistsError):
            self.invoke()
        self.assertEqual((self.output / 'status.json').read_bytes(), original)

    def test_pending_build_graph_does_not_claim_artifact_ready(self):
        self.stdout = '[1/1] Building CXX object fixture.o\n'
        self.invoke()
        self.assertFalse(self.status()['build_graph_up_to_date'])
        self.assertFalse(self.status()['release_qualified'])

    def test_source_change_is_rejected(self):
        identities = iter([{'commit': 'before'}, {'commit': 'after'}])
        with self.assertRaisesRegex(RuntimeError, 'source changed'):
            self.invoke(source=lambda _: next(identities))
        self.assertEqual(self.status()['status'], 'failed')

    def test_configuration_or_binary_change_is_rejected(self):
        for section in ('build', 'artifacts'):
            self.output = self.source / 'build' / section
            changed = dict(self.identity, **{section: {'changed': True}})
            identities = iter([self.identity, changed])
            with self.assertRaisesRegex(RuntimeError, 'changed during execution'):
                self.invoke(capture=lambda **_: next(identities))
            self.assertEqual(self.status()['status'], 'failed')

    def test_inspection_does_not_depend_on_old_summary_marker(self):
        # No artifact-sha256.txt is created; identity is separately established.
        self.invoke()
        self.assertEqual(self.status()['status'], 'passed')
        self.assertFalse((self.build / 'artifact-sha256.txt').exists())


if __name__ == '__main__':
    unittest.main()
