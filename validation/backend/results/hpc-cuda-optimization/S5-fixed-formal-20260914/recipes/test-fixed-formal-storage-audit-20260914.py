"""Storage-audit negative controls; synthetic records are not physics evidence."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit',
    Path(__file__).with_name('audit-fixed-formal-storage-20260914.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'keep.txt').write_bytes(b'original\r\n')
        self.manifest = {'raw/keep.txt': dict(bytes=10, sha256=hashlib.sha256(b'original\r\n').hexdigest())}
        self.mapping = {'keep.txt': 'raw/keep.txt'}

    def test_preserves_original_line_endings(self):
        self.assertEqual(audit.verify_projection(self.root, self.manifest, self.mapping, {}), 1)

    def test_rejects_reformatted_bytes(self):
        (self.root / 'keep.txt').write_bytes(b'original\n')
        with self.assertRaises(ValueError):
            audit.verify_projection(self.root, self.manifest, self.mapping, {})

    def test_rejects_unexplained_missing_file(self):
        self.mapping['missing.txt'] = 'raw/keep.txt'
        with self.assertRaises(ValueError):
            audit.verify_projection(self.root, self.manifest, self.mapping, {})

    def test_large_tsv_omission_must_match_manifest(self):
        identity = dict(bytes=2*1024*1024+1, sha256='a'*64)
        self.manifest['raw/trace.tsv'] = identity
        self.mapping['trace.tsv'] = 'raw/trace.tsv'
        omitted = {'trace.tsv': dict(original_path='raw/trace.tsv', **identity)}
        self.assertEqual(audit.verify_projection(self.root, self.manifest, self.mapping, omitted), 1)
        omitted['trace.tsv']['sha256'] = 'b'*64
        with self.assertRaises(ValueError):
            audit.verify_projection(self.root, self.manifest, self.mapping, omitted)

    def test_small_or_non_tsv_omission_rejected(self):
        for name, size in [('trace.tsv', 2*1024*1024), ('evidence.json', 3*1024*1024)]:
            with self.subTest(name=name):
                identity = dict(bytes=size, sha256='a'*64)
                manifest = {'raw/data': identity}
                with self.assertRaises(ValueError):
                    audit.verify_projection(self.root, manifest, {},
                        {name: dict(original_path='raw/data', **identity)})

    def test_unsafe_paths_rejected(self):
        for name in ('../keep.txt', '/keep.txt', 'x\\keep.txt'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                audit.verify_projection(self.root, self.manifest, {name: 'raw/keep.txt'}, {})


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.raw = dict(bytes=100, sha256='a'*64)
        self.manifest = {'raw/trace.tsv': dict(bytes=999, sha256='b'*64)}
        self.receipt = dict(applied=True, archive_sha256='a'*64, windows_copy_sha256='a'*64,
            windows_copy_bytes=100, verified=copy.deepcopy(self.manifest), bytes=999,
            removed=['raw/trace.tsv'], compressed=[dict(original='raw/trace.tsv', decompressed_sha256='b'*64)])

    def test_complete_receipt(self):
        audit.verify_receipt(self.receipt, self.raw, self.manifest, True)

    def test_rejects_incomplete_or_changed_receipt(self):
        variants = [dict(applied=False), dict(removed=[]), dict(removed=['raw/trace.tsv']*2),
            dict(bytes=998), dict(windows_copy_bytes=101), dict(archive_sha256='c'*64),
            dict(compressed=[]), dict(compressed=[dict(original='raw/trace.tsv', decompressed_sha256='c'*64)])]
        for patch in variants:
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                audit.verify_receipt(dict(self.receipt, **patch), self.raw, self.manifest, True)


if __name__ == '__main__':
    unittest.main()
