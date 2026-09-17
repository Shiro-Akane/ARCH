"""Synthetic build-transcript rejection checks; not compiled factory evidence."""
import copy
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT/'validation/network/native-wave-candidate/windowed'
saved = sys.path[:]
try:
    spec = importlib.util.spec_from_file_location('window_factory_collection_test', HERE/'collect_factory.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path[:] = saved


def fixture():
    expected = []
    names = ['compile-wrapper']
    for network in (150, 200):
        names += [f'dependencies-audit{network}', f'compile-audit{network}-factory',
                  f'compile-audit{network}-harness', f'link-audit{network}', f'ldd-audit{network}']
    for name in names:
        # Explicitly synthetic: exact real recipes and paths are derived by
        # validate() at collection, not by this pure transcript fixture.
        expected.append(dict(name=name, command=['synthetic', name], cwd='/frozen/build',
                             timeout_seconds=12000 if name.endswith('-factory') else 300))
    record = dict(status='factory-built-not-runtime-qualified', identities_verified_after=True,
                  fresh_cuda_objects=True, window_contracts_passed=True,
                  commands=[dict(**row, status='passed', returncode=0, timed_out=False, elapsed_seconds=1.0)
                            for row in expected])
    for name in ('original_provider_modified', 'mathematical_bodies_modified', 'kernel_launch_shape_modified',
                 'production_registration_modified', 'nuclear_qualified', 'performance_qualified', 'release_qualified'):
        record[name] = False
    return record, expected


class FactoryCollectionTests(unittest.TestCase):
    def test_compiler_dotdot_aliases_archive_once_without_relaxing_safety(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sub = root/'nested'
            sub.mkdir()
            original = root/'header.h'
            original.write_bytes(b'synthetic header')
            alias = sub/'../header.h'
            self.assertEqual(module.canonical_archive_paths({original, alias}, root), {original.resolve()})
            with self.assertRaises(ValueError): module.canonical_archive_paths({original}, sub)
            with self.assertRaises(ValueError): module.canonical_archive_paths({sub}, root)
            with self.assertRaises(ValueError): module.canonical_archive_paths({root/'missing'}, root)

    def test_collection_uses_frozen_absolute_ninja_not_login_path(self):
        cache = ROOT/'validation/network/results/native-wave-20260917/factory-focused-v1/records/ARCH-native-wave-v4-20260916/factory-release/CMakeCache.txt'
        self.assertEqual(module.configured_ninja(cache.read_text()), '/home/ubuntu/projects/.envs/arch/bin/ninja')

    def test_missing_ambiguous_or_relative_ninja_rejected(self):
        for text in ('', 'CMAKE_MAKE_PROGRAM:FILEPATH=ninja\n',
                     'CMAKE_MAKE_PROGRAM:FILEPATH=/usr/bin/make\n',
                     'CMAKE_MAKE_PROGRAM:FILEPATH=/one/ninja\nCMAKE_MAKE_PROGRAM:FILEPATH=/two/ninja\n'):
            with self.subTest(text=text), self.assertRaises(ValueError): module.configured_ninja(text)

    def test_completed_build_transcript_only(self):
        record, expected = fixture()
        module.validate_transcript(record, expected)

    def test_missing_duplicate_or_reordered_commands_rejected(self):
        for change in ('missing', 'duplicate', 'reordered'):
            record, expected = fixture()
            if change == 'missing': record['commands'].pop()
            elif change == 'duplicate': record['commands'].append(copy.deepcopy(record['commands'][0]))
            else: record['commands'][1], record['commands'][2] = record['commands'][2], record['commands'][1]
            with self.subTest(change=change), self.assertRaises(ValueError): module.validate_transcript(record, expected)

    def test_failed_timed_out_or_altered_recipe_rejected(self):
        for field, value in (('returncode', 1), ('status', 'failed'), ('timed_out', True),
                             ('command', ['old-factory.o']), ('cwd', '/wrong'), ('timeout_seconds', 99999),
                             ('elapsed_seconds', float('nan')), ('elapsed_seconds', -1)):
            record, expected = fixture()
            record['commands'][2][field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                module.validate_transcript(record, expected)

    def test_cannot_promote_build_into_nuclear_or_performance_pass(self):
        for field in ('nuclear_qualified', 'performance_qualified', 'release_qualified',
                      'mathematical_bodies_modified', 'original_provider_modified', 'production_registration_modified'):
            record, expected = fixture()
            record[field] = True
            with self.subTest(field=field), self.assertRaises(ValueError): module.validate_transcript(record, expected)

    def test_missing_predecessor_or_post_identity_gate_rejected(self):
        for field in ('identities_verified_after', 'fresh_cuda_objects', 'window_contracts_passed'):
            record, expected = fixture()
            record[field] = False
            with self.subTest(field=field), self.assertRaises(ValueError): module.validate_transcript(record, expected)


if __name__ == '__main__':
    unittest.main()
