"""Synthetic archive-contract checks; no compiler or GPU qualification."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('native_factory_collection',
    ROOT / 'validation/network/native-wave-candidate/collect_factory.py')
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class CollectionContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_manifest_preserves_original_crlf_bytes(self):
        data = self.root / 'source.cpp'
        data.write_bytes(b'line1\r\nline2\n')
        expected = hashlib.sha256(data.read_bytes()).hexdigest()
        manifest = self.root / 'files.sha256'
        manifest.write_text(expected + '  source.cpp\n')
        self.assertEqual(collector.inventory(manifest, self.root), {data: expected})
        data.write_bytes(b'line1\nline2\n')
        with self.assertRaises(ValueError):
            collector.inventory(manifest, self.root)

    def test_duplicate_or_empty_manifest_is_rejected(self):
        data = self.root / 'source.cpp'
        data.write_bytes(b'x')
        manifest = self.root / 'files.sha256'
        line = hashlib.sha256(b'x').hexdigest() + '  source.cpp\n'
        for text in ('', line + line, 'not-a-hash  source.cpp\n'):
            manifest.write_text(text)
            with self.assertRaises(ValueError):
                collector.inventory(manifest, self.root)

    def test_exit_code_is_required(self):
        with self.assertRaises(FileNotFoundError):
            collector.completion(self.root)
        marker = self.root / 'exit-code'
        for invalid in ('', '-1', '256', '0\n1'):
            marker.write_text(invalid)
            with self.assertRaises(ValueError):
                collector.completion(self.root)
        marker.write_text('124\n')
        self.assertEqual(collector.completion(self.root), 124)

    def test_all_guard_observations_must_be_complete(self):
        log = self.root / 'guard.log'
        valid = ('MEMORY_GUARD_RESULT guard_stopped=False stop_reason=none\n'
                 'GPU_MEMORY_OBSERVATION scope=whole_device complete=True\n'
                 'SYSTEM_PRESSURE_OBSERVATION scope=linux_system complete=True\n')
        log.write_text(valid)
        collector.successful_guard(log)
        for invalid in (valid + valid, valid.replace('complete=True', 'complete=False'),
                        valid.replace('guard_stopped=False', 'guard_stopped=True'),
                        valid.replace('stop_reason=none', 'stop_reason=available_memory')):
            log.write_text(invalid)
            with self.assertRaises(ValueError):
                collector.successful_guard(log)

    def report(self, network):
        directory = self.root / 'focused-v1' / network
        transcript = directory / 'trajectory/arch.stdout'
        transcript.parent.mkdir(parents=True)
        transcript.write_bytes(b'synthetic-fixture-not-a-GPU-run\r\n')
        record = dict(focused_gate_pass=True, release_qualified=False,
            identity_verified_after_run=True, summary=dict(methods={'1': {}, '2': {}, '3': {}},
            steps=4, storage_sizes=[2, 3], requested_pool_cells=2),
            transcript=dict(sha256=collector.sha(transcript)))
        path = directory / 'evidence.json'
        path.write_text(json.dumps(record))
        return path, transcript, record

    def test_failure_is_preserved_not_promoted(self):
        self.assertEqual(collector.focused_status(self.root, 124), {})
        self.report('audit150')
        self.assertEqual(set(collector.focused_status(self.root, 1)), {'audit150'})
        with self.assertRaises(ValueError):
            collector.focused_status(self.root, 0)

    def test_both_complete_reports_required(self):
        self.report('audit150')
        _, transcript, _ = self.report('audit200')
        self.assertEqual(set(collector.focused_status(self.root, 0)), {'audit150', 'audit200'})
        transcript.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            collector.focused_status(self.root, 0)

    def test_missing_ode_cannot_pass(self):
        self.report('audit150')
        path, _, record = self.report('audit200')
        del record['summary']['methods']['2']
        path.write_text(json.dumps(record))
        with self.assertRaises(ValueError):
            collector.focused_status(self.root, 0)


if __name__ == '__main__':
    unittest.main()
