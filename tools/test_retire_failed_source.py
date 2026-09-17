#!/usr/bin/env python3
"""Read-only selection tests; run on Linux beside retire_failed_source.py."""
from pathlib import Path
import unittest

import retire_failed_source as retirement


class SelectionTests(unittest.TestCase):
    def test_only_reviewed_source_tree(self):
        for relative in ('src/solver.cu', 'tests/regression.cpp', 'simulation/example.h'):
            with self.subTest(relative=relative):
                self.assertTrue(retirement.selected(retirement.ROOT / relative))

    def test_root_build_configuration(self):
        for relative in ('CMakeLists.txt', 'CMakePresets.json'):
            self.assertTrue(retirement.selected(retirement.ROOT / relative))

    def test_logs_data_docs_and_scripts_preserved(self):
        for relative in ('src/input.h5', 'tests/failure.stderr', 'tests/run.py',
                         'logs/failed.cpp', 'docs/design.md', 'data/checkpoint.h5'):
            with self.subTest(relative=relative):
                self.assertFalse(retirement.selected(retirement.ROOT / relative))

    def test_other_snapshot_and_current_source_rejected(self):
        for root in (retirement.ROOT.parent / 'mutation-test',
                     Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')):
            with self.assertRaises(ValueError):
                retirement.selected(root / 'src/solver.cu')

    def test_nested_build_configuration_not_selected(self):
        self.assertFalse(retirement.selected(retirement.ROOT / 'tests/CMakeLists.txt'))


if __name__ == '__main__':
    unittest.main(verbosity=2)
