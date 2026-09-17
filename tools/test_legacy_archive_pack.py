#!/usr/bin/env python3
import gzip
import json
from pathlib import Path
import tempfile
import unittest

import legacy_archive_pack as archive


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='legacy-pack-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        assert self.root.parent == Path(tempfile.gettempdir()).resolve()
        assert self.root.name.startswith('legacy-pack-test-')
        self.store = archive.Store(self.root)
        key = self.store.put(b'preserved failure log\n')
        self.assertEqual(key, self.store.put(b'preserved failure log\n'))
        self.store.close()
        archive.save(self.root / 'chunks.json', self.store.objects)
        archive.save(self.root / 'volumes.json', self.store.volumes)
        self.entry = {'path': 'ARCH-example/logs/failure.stdout', 'bytes': 22,
                      'sha256': key, 'chunks': [key]}
        self.write_manifest()

    def write_manifest(self):
        with gzip.open(self.root / 'files.jsonl.gz', 'wt') as stream:
            stream.write(json.dumps(self.entry) + '\n')

    def test_verify_restore_and_refuse_overwrite(self):
        archive.verify(self.root)
        out = self.root / 'restored.log'
        archive.restore_one(self.root, self.entry['path'], out)
        self.assertEqual(out.read_bytes(), b'preserved failure log\n')
        with self.assertRaises(AssertionError):
            archive.restore_one(self.root, self.entry['path'], out)

    def test_corrupt_pack_rejected(self):
        p = self.root / self.store.volumes[0]['path']
        with p.open('ab') as stream:
            stream.write(b'corruption')
        with self.assertRaises(AssertionError):
            archive.verify(self.root)

    def test_traversal_origin_rejected(self):
        self.entry['path'] = '../outside'
        self.write_manifest()
        with self.assertRaises(AssertionError):
            archive.verify(self.root)

    def test_unpublished_source_detection(self):
        self.assertTrue(archive.is_source_file(Path('mutation.driver'), b'int main() {}'))
        self.assertTrue(archive.is_source_file(Path('anonymous.tmp'), b'#include "unsafe.h"'))
        self.assertFalse(archive.is_source_file(Path('failure.stderr'), b'#include "shown-in-error.h"'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
