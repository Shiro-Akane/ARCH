"""Synthetic readiness tests; never launch a GPU worker or overwrite evidence."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'validation/network/native-wave-candidate/windowed/resume_preflight.py'
spec = importlib.util.spec_from_file_location('resume_preflight_test', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
SERVER = '/home/ubuntu/projects/ARCH-native-wave-v4-20260916'


class ResumeTests(unittest.TestCase):
    def exercise(self, output, source, *, busy=True, fail_identity=False):
        focused = types.SimpleNamespace(
            factory_gate=Mock(return_value=[Path('/synthetic/prior.json')]),
            verify_factory_identity=Mock(return_value={'artifacts': {'synthetic': 'sha'}}),
            sha=Mock(return_value='synthetic_hash'))
        if fail_identity:
            focused.verify_factory_identity.side_effect = ValueError('changed factory')
        helper = types.SimpleNamespace(inventory=Mock(return_value={'synthetic': 'sha'}))

        def run(command, **kwargs):
            if command[0] == 'nvidia-smi':
                return subprocess.CompletedProcess(command, 0, '123, unrelated, 935 MiB\n' if busy else '', '')
            self.assertEqual(command[0], 'pgrep')
            return subprocess.CompletedProcess(command, 1, '', '')

        argv = [str(SCRIPT), '--root', SERVER, '--focused-script', str(source), '--output', str(output)]
        with patch.object(sys, 'argv', argv), patch.object(module, 'load', side_effect=[focused, helper]), \
             patch.object(module.subprocess, 'run', side_effect=run), \
             patch.object(module.subprocess, 'check_output', return_value='synthetic GPU snapshot'), \
             patch.object(module.hashlib, 'sha256', return_value=Mock(hexdigest=lambda: 'f7f449b2ac96e053f496f777abf77e6400b1a562a0f0a348a58ae30d082c3e37')):
            module.main()

    def test_busy_gpu_is_blocked_not_numerical_failure_or_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            output, source = Path(tmp) / 'record.json', Path(tmp) / 'synthetic.py'
            source.write_bytes(b'synthetic fixture, import is mocked')
            self.exercise(output, source)
            report = json.loads(output.read_text())
            self.assertEqual(report['status'], 'dispatch_blocked')
            self.assertTrue(report['static_preflight_pass'])
            for key in ('gpu_dispatch_ready', 'gpu_executed', 'numerical_qualified', 'performance_qualified'):
                self.assertFalse(report[key])

    def test_idle_means_ready_only_never_qualified(self):
        with tempfile.TemporaryDirectory() as tmp:
            output, source = Path(tmp) / 'record.json', Path(tmp) / 'synthetic.py'
            source.write_bytes(b'synthetic fixture')
            self.exercise(output, source, busy=False)
            report = json.loads(output.read_text())
            self.assertTrue(report['gpu_dispatch_ready'])
            self.assertFalse(report['gpu_executed'])
            self.assertFalse(report['numerical_qualified'])

    def test_changed_factory_is_recorded_and_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            output, source = Path(tmp) / 'record.json', Path(tmp) / 'synthetic.py'
            source.write_bytes(b'synthetic fixture')
            with self.assertRaisesRegex(ValueError, 'changed factory'):
                self.exercise(output, source, fail_identity=True)
            self.assertFalse(json.loads(output.read_text())['static_preflight_pass'])

    def test_existing_receipt_never_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            output, source = Path(tmp) / 'record.json', Path(tmp) / 'synthetic.py'
            output.write_bytes(b'old evidence')
            with self.assertRaises(AssertionError):
                self.exercise(output, source)
            self.assertEqual(output.read_bytes(), b'old evidence')

    def test_wrong_script_pin_rejected_before_import(self):
        with tempfile.TemporaryDirectory() as tmp:
            output, source = Path(tmp) / 'record.json', Path(tmp) / 'changed.py'
            source.write_bytes(b'changed input must not execute')
            argv = [str(SCRIPT), '--root', SERVER, '--focused-script', str(source), '--output', str(output)]
            with patch.object(sys, 'argv', argv), patch.object(module, 'load') as loader:
                with self.assertRaisesRegex(ValueError, 'Frozen focused input'):
                    module.main()
                loader.assert_not_called()
            self.assertFalse(json.loads(output.read_text())['static_preflight_pass'])


if __name__ == '__main__':
    unittest.main()
