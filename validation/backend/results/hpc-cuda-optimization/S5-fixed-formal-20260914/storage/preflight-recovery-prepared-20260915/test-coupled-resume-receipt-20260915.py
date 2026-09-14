"""Execute only the receipt-reading Python gate with local temporary fixtures."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


text = Path(__file__).with_name('run-remaining-coupled-after-capacity-20260915.ps1').read_text()
source = re.search(r"/python\" -c '([^']+)'", text).group(1)
code = compile(source, '<receipt-gate-extracted-from-powershell>', 'exec')


class ReceiptGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.saved = self.root / 'saved'
        self.saved.mkdir()
        self.data = b'preflight failed before any sample\n'
        self.name = 'controller-v3-formal-coupled_bd_rkl1_all_transport-v1/worker.log'
        path = self.saved / self.name
        path.parent.mkdir()
        path.write_bytes(self.data)
        self.row = {'bytes': len(self.data), 'sha256': hashlib.sha256(self.data).hexdigest()}
        self.report = {'status': 'preserved_never_started_preflight', 'sample_output_absent': True,
                       'scientific_validation': False, 'module': 'coupled_bd_rkl1_all_transport',
                       'pending_move': None, 'before': {self.name: self.row}}

    def run_gate(self):
        (self.saved / 'preservation.json').write_text(json.dumps(self.report))
        with patch.object(sys, 'argv', ['receipt-check', str(self.saved)]), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            exec(code, {})
        return output.getvalue()

    def reject(self):
        with self.assertRaises(AssertionError):
            self.run_gate()

    def test_valid_fixture_passes(self):
        self.assertIn('IDENTITY_PASS_NOT_SCIENCE_PASS', self.run_gate())

    def test_partial_preservation_rejected(self):
        self.report['status'] = 'failed_partial_preservation'
        self.reject()

    def test_sample_started_rejected(self):
        self.report['sample_output_absent'] = False
        self.reject()

    def test_wrong_module_rejected(self):
        self.report['module'] = 'coupled_bd_rkl2_all_transport'
        self.reject()

    def test_parent_traversal_rejected_even_with_matching_hash(self):
        (self.root / 'outside.log').write_bytes(self.data)
        self.report['before'] = {'../outside.log': self.row}
        self.reject()

    def test_absolute_path_rejected_even_with_matching_hash(self):
        path = self.root / 'outside.log'
        path.write_bytes(self.data)
        self.report['before'] = {path.as_posix(): self.row}
        self.reject()

    def test_backslash_path_rejected(self):
        self.report['before'] = {self.name.replace('/', '\\'): self.row}
        self.reject()

    def test_missing_member_rejected(self):
        (self.saved / self.name).unlink()
        self.reject()

    def test_changed_member_rejected(self):
        (self.saved / self.name).write_bytes(b'changed')
        self.reject()

    def test_wrong_hash_rejected(self):
        self.row['sha256'] = '0' * 64
        self.reject()

    def test_empty_inventory_rejected(self):
        self.report['before'] = {}
        self.reject()

    def test_pending_move_rejected(self):
        self.report['pending_move'] = self.name
        self.reject()

    def test_optimized_python_cannot_bypass_checks(self):
        result = subprocess.run([sys.executable, '-O', '-c', source, str(self.saved)],
                                capture_output=True, text=True, timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('refusing optimized Python', result.stderr)
        self.assertNotIn('IDENTITY_PASS', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
