"""Synthetic Git publication checks, never GPU/scientific qualification."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'validation/network/audit_git_evidence.py'


class NetworkGitAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.scope = self.root / 'validation/network/results/fixture'
        self.scope.mkdir(parents=True)
        (self.scope / 'record.txt').write_bytes(b'recorded\r\nbytes\r\n')
        (self.root / '.gitattributes').write_text('* -text\n')
        (self.root / '.gitignore').write_text('build/\n')
        self.git('init', '-q')
        self.git('add', '.')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid', 'commit', '-qm', 'fixture')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root)

    def audit(self):
        return subprocess.run([sys.executable, '-B', str(SCRIPT), '--revision', 'HEAD', '--scope',
                               'validation/network/results/fixture', '--output', str(self.root / 'audit.json')],
                              cwd=self.root, capture_output=True, text=True)

    def test_original_bytes_and_inventory_pass(self):
        result = self.audit()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads((self.root / 'audit.json').read_text())['scopes'][0]['files'], 1)

    def test_ignored_original_must_not_silently_disappear(self):
        (self.scope / 'build').mkdir()
        (self.scope / 'build/raw.log').write_bytes(b'original output\n')
        result = self.audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('inventory differs', result.stderr)
        self.assertFalse((self.root / 'audit.json').exists())

    def test_changed_original_bytes_rejected(self):
        (self.scope / 'record.txt').write_bytes(b'recorded\nbytes\n')
        result = self.audit()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Git bytes differ', result.stderr)


if __name__ == '__main__':
    unittest.main()
