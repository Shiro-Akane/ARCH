import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('microphysics_validation',
    ROOT/'validation/backend/run_microphysics_validation.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MicrophysicsValidationTests(unittest.TestCase):
    def run_phase(self, phase, *, missing=False, skipped=False, rc=0):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root/'build'/'evidence'
            calls = []
            def run(command, **kwargs):
                calls.append(command)
                if '--show-only=json-v1' in command:
                    tests = ['cuda_multiblock_burn','cuda_multiblock_diffusion',
                             'diffusion_rkl_parity','cuda_multiblock_diffusion_global_species']
                    tests += [f'cuda_multiblock_{m}_b{b}'
                              for m in ('burn','diffusion') for b in (3,1024,1025)]
                    if missing: tests.pop()
                    return SimpleNamespace(stdout=json.dumps(dict(tests=[dict(name=t) for t in tests])),returncode=0)
                if skipped: kwargs['stdout'].write('***Skipped')
                return SimpleNamespace(returncode=rc)
            argv = ['verify','--build-dir',str(root/'build'/'release'),
                    '--output-dir',str(out),'--phase',phase]
            with patch.object(module,'ROOT',root), patch.object(sys,'argv',argv), \
                 patch.object(module.subprocess,'run',side_effect=run), \
                 patch.object(module.provenance,'capture',return_value={}), \
                 patch.object(module.provenance,'file_identity',return_value={}), \
                 contextlib.redirect_stdout(io.StringIO()):
                error = None
                try: module.main()
                except RuntimeError as exc: error = exc
            return calls, json.loads((out/'record.json').read_text()), error

    def test_all_transport_restart_selection_does_not_omit_either_gate(self):
        for phase, flags in [('coupled',set()),('coupled-restart',{'--restart'}),
                ('coupled-all-transport',{'--all-transport'}),
                ('coupled-all-transport-restart',{'--restart','--all-transport'})]:
            calls, report, error = self.run_phase(phase)
            self.assertIsNone(error)
            self.assertEqual(set(calls[0]) & {'--restart','--all-transport'},flags)
            self.assertEqual(report['status'],'passed')
            self.assertFalse(report['release_qualified'])

    def test_batch_inventory_includes_waves_wide_workspace_and_leaf_failures(self):
        calls, report, error = self.run_phase('batch-contracts')
        self.assertIsNone(error)
        self.assertEqual(len(report['required_tests']),10)
        self.assertIn('--show-only=json-v1',calls[0])
        self.assertIn('--no-tests=error',calls[1])
        self.assertEqual(calls[1][calls[1].index('--parallel')+1],'1')

    def test_missing_inventory_never_starts_a_partial_test_set(self):
        calls, report, error = self.run_phase('batch-contracts',missing=True)
        self.assertIsNotNone(error)
        self.assertEqual(len(calls),1)
        self.assertEqual(report['status'],'failed')

    def test_skip_or_nonzero_exit_cannot_be_a_pass(self):
        for options in (dict(skipped=True),dict(rc=1)):
            _, report, error = self.run_phase('batch-contracts',**options)
            self.assertIsNotNone(error)
            self.assertEqual(report['status'],'failed')


if __name__ == '__main__': unittest.main()
