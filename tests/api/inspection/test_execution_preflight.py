"""Run preparation shares production resolution and never creates driver output."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT = (Path(p).resolve() for p in sys.argv[1:3])
del sys.argv[1:3]
BASE = (ROOT / 'simulation/Sod/Sod.par').read_text()


def edit(text, **values):
    lines = [line for line in text.splitlines()
             if line.split('#', 1)[0].split('=', 1)[0].strip() not in values]
    return '\n'.join(lines) + '\n' + ''.join(f'{key}={value}\n' for key, value in values.items())


class ExecutionPreflight(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-run-preflight-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def call(self, text, code=0, case='Sod', command='--check-run'):
        payload = text.encode('utf-8')
        before = sorted(self.cwd.rglob('*'))
        process = subprocess.run([str(ARCH), command, case, '--config-stdin',
                                  '--request-id', 'run-preparation'], input=payload,
                                 capture_output=True, timeout=360, cwd=self.cwd,
                                 env=dict(os.environ, OMP_NUM_THREADS='1'))
        self.assertEqual(process.returncode, code, (process.stdout[-6000:], process.stderr))
        self.assertEqual(before, sorted(self.cwd.rglob('*')), 'query created an output file')
        out = json.loads(process.stdout)
        self.assertEqual(out['identity'], dict(caseId=case, requestId='run-preparation',
                                              configRevision=hashlib.sha256(payload).hexdigest()))
        if command == '--check-run':
            self.assertEqual((out['schemaVersion'], out['version'], out['kind']),
                             ('1.0', '1', 'execution-preflight'))
            self.assertEqual(out['execution'], dict(meshConstruction='not_executed', timeStepping='not_executed'))
            self.assertEqual(out['status'], 'ok' if code == 0 else 'error')
            self.assertIsInstance(out['diagnostics'], list)
            if code:
                self.assertIsNone(out['data'])
                self.assertTrue(any(d['severity'] == 'error' for d in out['diagnostics']))
        return out

    def test_cpu_and_exact_crlf_identity(self):
        for text in [BASE, BASE.replace('\n', '\r\n')]:
            out = self.call(text)
            self.assertEqual(out['data'], dict(requestedBackend='cpu', resolvedBackend='cpu',
                                              linearSolver='none', fallbackReason=''))

    def test_setup_validation_and_unknown_case(self):
        out = self.call(edit(BASE, rho_left=-1), 3)
        self.assertTrue(any('positive densities/pressures' in d['message'] for d in out['diagnostics']))
        self.call(BASE, 3, case='NoSuchRegisteredCase')

    def test_explicit_cuda_rejects_unavailable_device(self):
        self.call(edit(BASE, compute_backend='cuda', cuda_device=2147483647), 3)

    def test_auto_reports_cpu_reason(self):
        out = self.call(edit(BASE, compute_backend='auto', cuda_device=2147483647))
        self.assertEqual(out['data']['requestedBackend'], 'auto')
        self.assertEqual(out['data']['resolvedBackend'], 'cpu')
        self.assertTrue(out['data']['fallbackReason'])

    def test_inspection_does_not_check_run_backend(self):
        self.call(edit(BASE, compute_backend='cuda', cuda_device=2147483647),
                  command='--inspect-config')

    def test_initial_preview_keeps_cpu_contract(self):
        out = self.call(edit(BASE, compute_backend='cuda', cuda_device=2147483647),
                        command='--preview')
        self.assertEqual(out['status'], 'ok')

    def test_missing_cli_input_has_error_envelope(self):
        process = subprocess.run([str(ARCH), '--check-run', 'Sod'], capture_output=True,
                                 timeout=10, cwd=self.cwd)
        self.assertEqual(process.returncode, 3)
        out = json.loads(process.stdout)
        self.assertEqual(out['kind'], 'execution-preflight')
        self.assertEqual(out['identity']['caseId'], 'Sod')
        self.assertIsNone(out['data'])


if __name__ == '__main__':
    unittest.main()
