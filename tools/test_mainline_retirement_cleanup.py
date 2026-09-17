#!/usr/bin/env python3
"""Linux-only, temporary-directory tests; no production deletion is invoked."""
import os
from pathlib import Path
import tempfile
import unittest

import mainline_retirement_cleanup as cleanup


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='retirement-cleanup-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        old = cleanup.OLD
        cleanup.OLD = self.root
        self.addCleanup(setattr, cleanup, 'OLD', old)
        self.path = self.root / 'one.log'
        self.path.write_bytes(b'log evidence')
        s = self.path.stat()
        self.entry = {'path': 'one.log', 'bytes': s.st_size, 'inode': s.st_ino, 'device': s.st_dev,
                      'mtime_ns': s.st_mtime_ns, 'nlink': s.st_nlink}

    def test_unchanged_file(self):
        cleanup.validate(self.entry)

    def test_changed_file(self):
        self.path.write_bytes(b'new data')
        with self.assertRaises(AssertionError):
            cleanup.validate(self.entry)

    def test_path_escape(self):
        self.entry['path'] = '../outside'
        with self.assertRaises(AssertionError):
            cleanup.validate(self.entry)

    def test_symlink_rejected(self):
        self.path.unlink()
        self.path.symlink_to('/etc/hosts')
        with self.assertRaises(AssertionError):
            cleanup.validate(self.entry)

    def test_expected_hardlink_decrement_only(self):
        second = self.root / 'two.log'
        os.link(self.path, second)
        self.entry['nlink'] = 2
        cleanup.validate(self.entry)
        second.unlink()
        with self.assertRaises(AssertionError):
            cleanup.validate(self.entry)
        cleanup.validate(self.entry, {(self.entry['device'], self.entry['inode']): 1})


if __name__ == '__main__':
    unittest.main(verbosity=2)
