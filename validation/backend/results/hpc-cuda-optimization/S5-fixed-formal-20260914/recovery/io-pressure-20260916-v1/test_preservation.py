"""Mechanical fixtures only: no ARCH/CUDA runs or physics qualification."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('preserver', Path(__file__).with_name('preserve_failed_formal.py'))
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)
GUARD = ('MEMORY_GUARD_STOP reason=io_pressure: terminating owned descendants\n'
         'guard_stopped=True stop_reason=io_pressure\n')


def report():
    completed = [dict(status='passed', returncode=0, timed_out=False) for _ in range(80)]
    completed.append(dict(case=p.MODULE + '_b128', phase='measured', repeat=0,
                          version='candidate', backend='cpu', threads=1, status='running'))
    return dict(status='running', pilot=False, lanes=completed,
                comparisons=[dict(fields=dict(passed=True, status='pass'), workload_aligned=True)
                             for _ in range(77)])


class FailureGate(unittest.TestCase):
    def test_inspected_failure_accepted(self):
        original = report()
        before = copy.deepcopy(original)
        p.validate_failure(original, GUARD, 125)
        self.assertEqual(original, before)

    def test_success_exit_rejected(self):
        with self.assertRaises(RuntimeError): p.validate_failure(report(), GUARD, 0)

    def test_other_guard_cause_rejected(self):
        with self.assertRaises(RuntimeError): p.validate_failure(report(), GUARD.replace('io_pressure', 'memory'), 125)

    def test_completed_report_rejected(self):
        r = report(); r['status'] = 'passed'
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)

    def test_after_identity_not_fabricated(self):
        r = report(); r['identities_after'] = {}
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)

    def test_changed_lane_inventory_rejected(self):
        r = report(); r['lanes'].pop()
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)

    def test_wrong_interrupted_lane_rejected(self):
        r = report(); r['lanes'][-1]['backend'] = 'cuda'
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)

    def test_earlier_failure_rejected(self):
        r = report(); r['lanes'][0]['status'] = 'failed'
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)

    def test_failed_comparison_rejected(self):
        r = report(); r['comparisons'][0]['workload_aligned'] = False
        with self.assertRaises(RuntimeError): p.validate_failure(r, GUARD, 125)


class Storage(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.a = self.base / 'a'
        self.a.write_bytes(b'original\r\nbytes\n')
        self.b = self.base / 'b'
        self.b.mkdir()
        (self.b / 'leaf').write_bytes(b'leaf')
        self.tops = [self.a, self.b]
        self.before = p.inventory(self.base, self.tops)
        self.out = self.base / 'preserved'

    def tearDown(self):
        self.temp.cleanup()

    def test_raw_byte_identity(self):
        self.assertEqual(self.before['a']['sha256'], hashlib.sha256(b'original\r\nbytes\n').hexdigest())

    def test_outside_root_rejected(self):
        with self.assertRaises(RuntimeError): p.inventory(self.b, [self.a])

    def test_overlapping_inventory_rejected(self):
        with self.assertRaises(RuntimeError): p.inventory(self.base, [self.a, self.a])

    def test_relocation_preserves_every_byte(self):
        r = p.move_verified(self.base, self.out, self.tops, self.before, {'synthetic': True})
        self.assertEqual(r['status'], 'failed_attempt_relocated_after_double_backup')
        self.assertFalse(r['scientific_validation_complete'])
        self.assertFalse(r['samples_may_be_mixed_with_retry'])
        self.assertIsNone(r['pending_move'])
        self.assertEqual(p.inventory(self.out, [self.out / 'a', self.out / 'b']), self.before)
        self.assertFalse(self.a.exists())

    def test_modified_original_rejected(self):
        self.a.write_bytes(b'changed')
        with self.assertRaises(RuntimeError):
            p.move_verified(self.base, self.out, self.tops, self.before, {})
        self.assertFalse(self.out.exists())
        self.assertTrue(self.a.exists())

    def test_existing_destination_rejected(self):
        self.out.mkdir()
        with self.assertRaises(RuntimeError):
            p.move_verified(self.base, self.out, self.tops, self.before, {})
        self.assertTrue(self.a.exists())

    def test_partial_move_has_recovery_journal(self):
        original = Path.rename
        def rename(source, target):
            if source == self.b: raise OSError('synthetic second-rename failure')
            return original(source, target)
        with patch.object(Path, 'rename', rename), self.assertRaises(OSError):
            p.move_verified(self.base, self.out, self.tops, self.before, {})
        r = json.loads((self.out / 'relocation.json').read_text())
        self.assertEqual(r['status'], 'failed_partial_relocation_preserved')
        self.assertEqual(r['moved'], ['a'])
        self.assertEqual(r['pending_move'], 'b')
        self.assertEqual(p.identity(self.out / 'a'), self.before['a'])
        self.assertTrue((self.b / 'leaf').is_file())

    def test_prefix_inventory_keeps_control_last(self):
        (self.base / p.LABEL).mkdir()
        (self.base / p.CONTROL).mkdir()
        for suffix in p.SUFFIXES: (self.base / (p.LABEL + suffix)).write_bytes(b'fixture')
        self.assertEqual(p.select_tops(self.base)[-1].name, p.CONTROL)

    def test_unexpected_prefix_rejected(self):
        self.test_prefix_inventory_keeps_control_last()
        (self.base / (p.LABEL + '-unexpected')).write_bytes(b'fixture')
        with self.assertRaises(RuntimeError): p.select_tops(self.base)


class BackupGate(unittest.TestCase):
    def test_matching_attestations(self):
        p.verify_backup(dict(status='interrupted_attempt_preserved_not_qualified',
            raw=dict(sha256='raw', bytes=42), compact=dict(sha256='compact')), 'raw', 42, 'compact')

    def test_mismatches_rejected(self):
        r = dict(status='interrupted_attempt_preserved_not_qualified',
                 raw=dict(sha256='raw', bytes=42), compact=dict(sha256='compact'))
        for values in [('wrong', 42, 'compact'), ('raw', 43, 'compact'), ('raw', 42, 'wrong')]:
            with self.subTest(values=values), self.assertRaises(RuntimeError): p.verify_backup(r, *values)


if __name__ == '__main__':
    unittest.main(verbosity=2)
