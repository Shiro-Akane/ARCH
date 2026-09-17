#!/usr/bin/env python3
"""Linux-only, temporary-directory safety tests. Never uses production paths."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import server_storage_audit as audit
import server_storage_cleanup as cleanup
from server_storage_prepare import record


class SafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / 'ARCH-old'
        self.build = self.root / 'build'
        self.source = self.root / 'source'
        self.source.mkdir(parents=True)
        (self.source / 'CMakeLists.txt').write_text('project(ARCH)\n')
        self.build.mkdir()
        (self.build / 'CMakeCache.txt').write_text(f'CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n')
        self.path = self.build / 'CMakeFiles/arch_solver.dir/solver.cpp.o'
        self.path.parent.mkdir(parents=True)
        self.path.write_bytes(b'object fixture')
        os.utime(self.path, (1, 1))
        for module, name, value in [(cleanup, 'BASE', self.base),
                                    (cleanup, 'OLD_ROOTS', [self.root])]:
            p = patch.object(module, name, value)
            p.start()
            self.addCleanup(p.stop)

    def entry(self, p=None):
        p = p or self.path
        return dict(record(p), category='rebuildable_cmake_object_or_pch',
                    build_root=str(self.build), source_root=str(self.source))

    def test_accepts_only_old_generated_object(self):
        cleanup.validate_entry(self.entry())

    def test_rejects_changed_size(self):
        entry = self.entry()
        self.path.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            cleanup.validate_entry(entry)

    def test_rejects_recent_file(self):
        os.utime(self.path, None)
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry())

    def test_rejects_source(self):
        p = self.source / 'solver.cpp'
        p.write_text('int main() {}')
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry(p))

    def test_rejects_dependencies(self):
        p = self.build / '_deps/shared/CMakeFiles/example.dir/file.cpp.o'
        p.parent.mkdir(parents=True)
        p.write_bytes(b'keep dependency')
        os.utime(p, (1, 1))
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry(p))

    def test_rejects_hardlinks(self):
        os.link(self.path, self.path.with_name('alias.o'))
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry())

    def test_rejects_parent_symlink(self):
        entry = self.entry()
        alias = self.root / 'alias'
        alias.symlink_to(self.build, target_is_directory=True)
        entry['path'] = str(alias / self.path.relative_to(self.build))
        with self.assertRaises(ValueError):
            cleanup.validate_entry(entry)

    def test_rejects_missing_source(self):
        (self.source / 'CMakeLists.txt').unlink()
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry())

    def test_rejects_file_outside_roots(self):
        p = self.base / 'elsewhere.o'
        p.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            cleanup.validate_entry(self.entry(p))

    def test_rejects_false_category(self):
        entry = self.entry()
        entry['category'] = 'delete_everything'
        with self.assertRaises(ValueError):
            cleanup.validate_entry(entry)


if __name__ == '__main__':
    unittest.main(verbosity=2)
