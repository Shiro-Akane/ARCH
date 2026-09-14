"""Local temporary-fixture tests; no server, CUDA, or real experiment touched."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location('preserve',
    Path(__file__).with_name('preserve-coupled-preflight-20260915.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class PreservationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.base = root / 'timing'
        self.base.mkdir()
        self.control = self.base / m.CONTROL
        self.control.mkdir()
        for name, text in {'pid': '123456\n', 'exit-code': '1\n', 'lock': '',
                           'worker.log': 'preflight failed, no sample started\n'}.items():
            (self.control / name).write_text(text)
        for suffix in m.SUFFIXES:
            (self.base / (m.LABEL + suffix)).write_text('fixture ' + suffix + '\n')
        self.destination = self.base / m.DESTINATION
        self.expected = m.SUFFIXES
        for name, value in [('ROOT', root), ('BASE', self.base)]:
            patcher = patch.object(m, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        patcher = patch.object(m.subprocess, 'run', side_effect=self.process)
        self.run_mock = patcher.start()
        self.addCleanup(patcher.stop)

    def process(self, command, **kwargs):
        return subprocess.CompletedProcess(command, 1, stdout='', stderr='')

    def run_main(self):
        with contextlib.redirect_stdout(io.StringIO()):
            m.main()

    def reject_untouched(self, expression):
        before = {p.relative_to(self.base).as_posix(): p.read_bytes()
                  for p in self.base.rglob('*') if p.is_file()}
        with self.assertRaisesRegex(RuntimeError, expression):
            self.run_main()
        after = {p.relative_to(self.base).as_posix(): p.read_bytes()
                 for p in self.base.rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        self.assertTrue(self.control.is_dir())

    def test_exact_failed_preflight_preserved_not_deleted(self):
        original = {p.relative_to(self.base).as_posix(): p.read_bytes()
                    for p in self.base.rglob('*') if p.is_file()}
        self.run_main()
        record = json.loads((self.destination / 'preservation.json').read_text())
        self.assertEqual(record['status'], 'preserved_never_started_preflight')
        self.assertFalse(record['scientific_validation'])
        self.assertIsNone(record['pending_move'])
        self.assertEqual(len(record['before']), len(original))
        self.assertFalse(self.control.exists())
        for name, data in original.items():
            self.assertEqual((self.destination / name).read_bytes(), data)
        self.assertFalse(any(p.name.startswith(m.LABEL) for p in self.base.iterdir()))

    def test_successful_controller_rejected(self):
        (self.control / 'exit-code').write_text('0\n')
        self.reject_untouched('Successful controller')

    def test_live_controller_rejected(self):
        def process(command, **kwargs):
            return subprocess.CompletedProcess(command, 0 if command[0] == 'ps' else 1,
                                                stdout='S\n' if command[0] == 'ps' else '', stderr='')
        self.run_mock.side_effect = process
        self.reject_untouched('Controller still active')

    def test_active_arch_rejected(self):
        self.run_mock.return_value = subprocess.CompletedProcess(['pgrep'], 0)
        self.run_mock.side_effect = None
        self.reject_untouched('Application/build active')

    def test_any_sample_root_rejected_even_empty(self):
        (self.base / m.LABEL).mkdir()
        self.reject_untouched('Sample output or unknown')

    def test_stdout_rejected_even_empty(self):
        (self.base / (m.LABEL + '.stdout')).touch()
        self.reject_untouched('Sample output or unknown')

    def test_unknown_phase_file_rejected(self):
        (self.base / (m.LABEL + '-unexpected.json')).write_text('{}')
        self.reject_untouched('Sample output or unknown')

    def test_extra_controller_member_rejected(self):
        (self.control / 'unexpected').touch()
        self.reject_untouched('Unexpected controller inventory')

    def test_missing_recipe_rejected(self):
        (self.base / (m.LABEL + '-controller-recipe-v3.sh')).unlink()
        self.reject_untouched('Incomplete preflight recipe')

    def test_collection_rejected(self):
        (self.base / f'collection-{m.MODULE}-v1.log').write_text('started')
        self.reject_untouched('Collection already started')

    def test_existing_recovery_directory_rejected(self):
        self.destination.mkdir()
        self.reject_untouched('Recovery destination already exists')

    def test_mutation_before_move_keeps_failure_journal(self):
        original_inventory = m.inventory
        calls = 0
        def changed(paths):
            nonlocal calls
            calls += 1
            if calls == 2:
                (self.control / 'worker.log').write_text('changed after first inventory')
            return original_inventory(paths)
        with patch.object(m, 'inventory', side_effect=changed):
            with self.assertRaisesRegex(RuntimeError, 'changed before move'):
                self.run_main()
        record = json.loads((self.destination / 'preservation.json').read_text())
        self.assertEqual(record['status'], 'failed_partial_preservation')
        self.assertEqual(record['moved'], [])
        self.assertTrue(self.control.is_dir())

    def test_partial_move_failure_retains_all_original_bytes(self):
        original_rename = Path.rename
        def rename(path, target):
            if path.name == m.LABEL + '-child.log':
                raise OSError('fixture move failure')
            return original_rename(path, target)
        with patch.object(Path, 'rename', new=rename):
            with self.assertRaisesRegex(OSError, 'fixture move failure'):
                self.run_main()
        record = json.loads((self.destination / 'preservation.json').read_text())
        self.assertEqual(record['status'], 'failed_partial_preservation')
        self.assertEqual(record['moved'], [m.CONTROL])
        self.assertEqual(record['pending_move'], m.LABEL + '-child.log')
        for relative, row in record['before'].items():
            choices = [p for p in (self.base / relative, self.destination / relative) if p.is_file()]
            self.assertEqual(len(choices), 1)
            self.assertEqual(m.sha(choices[0]), row['sha256'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
