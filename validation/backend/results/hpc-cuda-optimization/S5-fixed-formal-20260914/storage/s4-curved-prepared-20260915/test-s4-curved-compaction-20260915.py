"""Small local fixtures for the cleanup contract; no server/GPU/real zstd."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('reclaim', Path(__file__).with_name(
    'compact-verified-s4-curved-20260915.py'))
reclaim = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reclaim)


class FakeDecoder:
    def __init__(self, command, **kwargs):
        self.stdout = Path(command[-1]).open('rb')
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.stdout.close()
    def wait(self):
        return 0


class ReclaimContract(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.addCleanup(patch.stopall)
        self.root = Path(temp.name) / 'project'
        self.base = self.root / 'build'
        self.scope = self.base / reclaim.PREFIX
        self.scope.mkdir(parents=True)
        self.paths = [self.scope / 'one.h5', self.scope / 'two.h5']
        for path, data in zip(self.paths, (b'checkpoint-one', b'checkpoint-two')):
            path.write_bytes(data)
        self.log = self.scope / 'arch.stdout'
        self.log.write_text('immutable log')
        self.archive = self.base / reclaim.ARCHIVE_NAME
        with tarfile.open(self.archive, 'w') as archive:
            for path in self.paths:
                archive.add(path, arcname=path.relative_to(self.base).as_posix())
        self.archive_sha = reclaim.file_hash(self.archive)
        self.proof = dict(status='local_archive_member_proof_only', prefix=reclaim.PREFIX,
            archive=dict(path='local-fixture.tar', sha256=self.archive_sha,
                         bytes=self.archive.stat().st_size),
            members={p.relative_to(self.base).as_posix(): dict(bytes=p.stat().st_size,
                      sha256=reclaim.file_hash(p)) for p in self.paths})
        self.proof_path = self.base / 'proof.json'
        patch.object(reclaim, 'ROOT', self.root).start()
        patch.object(reclaim, 'SHA', self.archive_sha).start()
        patch.object(reclaim, 'ARCHIVE_BYTES', self.archive.stat().st_size).start()
        patch.object(reclaim, 'EXPECTED_FILES', 2).start()
        patch.object(reclaim, 'EXPECTED_BYTES', sum(p.stat().st_size for p in self.paths)).start()
        patch.object(reclaim, 'allocated_size', lambda p: p.stat().st_size).start()
        patch.object(reclaim.subprocess, 'Popen', FakeDecoder).start()
        self.process_check = patch.object(reclaim.subprocess, 'run',
            side_effect=lambda cmd, **kw: subprocess.CompletedProcess(cmd, 1)).start()
        self.output = self.base / 's4-curved-compaction-20260915-v1.json'

    def run_cleanup(self, apply=False):
        self.proof_path.write_text(json.dumps(self.proof))
        args = ['cleanup', '--local-proof', str(self.proof_path)] + (['--apply'] if apply else [])
        with patch.object(reclaim, 'PROOF_SHA', reclaim.file_hash(self.proof_path)), \
                patch.object(sys, 'argv', args), contextlib.redirect_stdout(io.StringIO()):
            reclaim.main()
        return json.loads(self.output.read_text())

    def assert_all_originals_exist(self):
        self.assertTrue(all(p.exists() for p in self.paths))
        self.assertEqual(reclaim.file_hash(self.archive), self.archive_sha)
        self.assertEqual(self.log.read_text(), 'immutable log')

    def test_dry_run_preserves_all(self):
        result = self.run_cleanup()
        self.assertEqual(result['status'], 'verified_only')
        self.assertEqual(result['removed'], [])
        self.assertFalse(result['applied'])
        self.assert_all_originals_exist()

    def test_apply_only_verified_hdf_copies(self):
        result = self.run_cleanup(apply=True)
        self.assertTrue(result['applied'])
        self.assertEqual(result['status'], 'applied')
        self.assertEqual(len(result['removed']), 2)
        self.assertIsNone(result['pending_removal'])
        self.assertTrue(all(not p.exists() for p in self.paths))
        self.assertTrue(self.scope.is_dir())
        self.assertEqual(reclaim.file_hash(self.archive), self.archive_sha)
        self.assertEqual(self.log.read_text(), 'immutable log')
        self.assertFalse(result['scientific_validation'])

    def test_active_application_refuses_cleanup(self):
        self.process_check.side_effect = lambda cmd, **kw: subprocess.CompletedProcess(cmd, 0)
        with self.assertRaisesRegex(RuntimeError, 'active'):
            self.run_cleanup(apply=True)
        self.assert_all_originals_exist()

    def test_changed_live_hdf_refuses_all_removals(self):
        self.paths[1].write_bytes(b'checkpoint-bad')
        with self.assertRaisesRegex(RuntimeError, 'hashes differ'):
            self.run_cleanup(apply=True)
        self.assert_all_originals_exist()

    def test_changed_archive_refuses_all_removals(self):
        self.archive.write_bytes(b'wrong archive')
        with self.assertRaisesRegex(RuntimeError, 'archive mismatch'):
            self.run_cleanup(apply=True)
        self.assertTrue(all(p.exists() for p in self.paths))

    def test_unlisted_hdf_requires_investigation(self):
        (self.scope / 'unexpected.h5').write_bytes(b'not backed up')
        with self.assertRaisesRegex(RuntimeError, 'inventory differs'):
            self.run_cleanup(apply=True)
        self.assert_all_originals_exist()

    def test_wrong_local_member_hash_refuses_removal(self):
        self.proof['members'][self.paths[0].relative_to(self.base).as_posix()]['sha256'] = '0' * 64
        with self.assertRaisesRegex(RuntimeError, 'hashes differ'):
            self.run_cleanup(apply=True)
        self.assert_all_originals_exist()

    def test_traversal_member_refused(self):
        name = self.paths[0].relative_to(self.base).as_posix()
        self.proof['members'][reclaim.PREFIX + '../one.h5'] = self.proof['members'].pop(name)
        with self.assertRaisesRegex(RuntimeError, 'Unsafe member path'):
            self.run_cleanup(apply=True)
        self.assert_all_originals_exist()

    def test_existing_receipt_not_overwritten(self):
        self.output.write_text('previous receipt')
        with self.assertRaisesRegex(RuntimeError, 'Fresh receipt required'):
            self.run_cleanup(apply=True)
        self.assertEqual(self.output.read_text(), 'previous receipt')
        self.assert_all_originals_exist()

    def test_partial_failure_has_pending_and_removed_journal(self):
        original_unlink = Path.unlink
        def fail_second(path, *args, **kwargs):
            if path == self.paths[1]:
                raise OSError('injected unlink failure')
            return original_unlink(path, *args, **kwargs)
        with patch.object(Path, 'unlink', fail_second), self.assertRaisesRegex(OSError, 'injected'):
            self.run_cleanup(apply=True)
        result = json.loads(self.output.read_text())
        self.assertEqual(result['status'], 'failed_after_partial_removal')
        self.assertFalse(result['applied'])
        self.assertEqual(result['removed'], [self.paths[0].relative_to(self.base).as_posix()])
        self.assertEqual(result['pending_removal'], self.paths[1].relative_to(self.base).as_posix())
        self.assertFalse(self.paths[0].exists())
        self.assertTrue(self.paths[1].exists())
        self.assertEqual(reclaim.file_hash(self.archive), self.archive_sha)


if __name__ == '__main__':
    unittest.main(verbosity=2)
