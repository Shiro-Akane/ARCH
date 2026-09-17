#!/usr/bin/env python3
import io
import tarfile
import unittest

from mainline_retirement_archive import Collector, omitted_member, safe_member


class RetirementArchiveTests(unittest.TestCase):
    def test_member_traversal_rejected(self):
        for name in ('/absolute', '../outside', 'a/../../b', 'a\\..\\b'):
            self.assertFalse(safe_member(name))
        self.assertTrue(safe_member('./source/src/a.cpp'))

    def test_failed_source_but_not_failure_logs(self):
        self.assertEqual(omitted_member('taskg-red-source/src/a.cpp', b'int main(){}'),
                         'previously_confirmed_failed_source')
        self.assertIsNone(omitted_member('taskg-red-source/logs/failure.stderr', b'failed'))
        self.assertIsNone(omitted_member('red-mutation-fixture.cpp', b'int main(){}'))

    def test_hdf_and_unaccepted_source_preserved(self):
        self.assertIsNone(omitted_member('run/output.h5', b'\x89HDF'))
        self.assertIsNone(omitted_member('unknown/src/a.cpp', b'int main(){}'))
        self.assertEqual(omitted_member('build/ARCH', b'\x7fELFxxx'), 'rebuildable_compiled_artifact')

    def test_git_in_archive_is_not_silently_discarded(self):
        self.assertIsNone(omitted_member('source/.git/objects/pack/pack-123.pack', b'PACK'))

    def test_archive_links_are_recorded_not_followed(self):
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode='w') as t:
            m = tarfile.TarInfo('link')
            m.type, m.linkname = tarfile.SYMTYPE, '/outside'
            t.addfile(m)
        buf.seek(0)
        c = Collector.__new__(Collector)
        rows = []
        c.emit = rows.append
        c.tar_members('sample.tar', buf, lambda *args: self.fail('link was read'))
        self.assertEqual(rows[0]['target'], '/outside')
        self.assertEqual(rows[0]['kind'], 'archive_link')

    def test_unsafe_tar_member_fails_before_read(self):
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode='w') as t:
            t.addfile(tarfile.TarInfo('../outside'))
        buf.seek(0)
        c = Collector.__new__(Collector)
        with self.assertRaises(AssertionError):
            c.tar_members('bad.tar', buf, lambda *args: self.fail('unsafe member was read'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
