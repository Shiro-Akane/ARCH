"""Small real-Ninja observation controls; no compiler or build is invoked."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('cold_core_replay', Path(__file__).with_name('replay.py'))
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)


class ColdContractTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='arch-cold-contract-')
        self.root = Path(self.temporary.name)
        self.cold, self.reference = self.root / 'cold', self.root / 'reference'
        self.configure(self.cold)
        self.configure(self.reference)

    def tearDown(self):
        self.temporary.cleanup()

    def configure(self, build, *, compile_flag='-O3', link_flag='-flto', depth=2,
                  second_pool=True, dependency='/shared/include', reverse=False):
        (build / 'CMakeFiles').mkdir(parents=True, exist_ok=True)
        (build / 'CMakeCache.txt').write_text('CMAKE_BUILD_TYPE:STRING=Release\n')
        database = [dict(directory=str(build), file=str(build / (name + '.cpp')),
                        arguments=['g++', '-c', str(build / (name + '.cpp')), '-o', name + '.o'])
                    for name in ('first', 'second')]
        (build / 'compile_commands.json').write_text(json.dumps(database))
        (build / 'CMakeFiles/rules.ninja').write_text(
            f'pool arch_cuda_heavy\n  depth = {depth}\n\n'
            f'rule compile\n  command = g++ {compile_flag} -I{dependency} -c $in -o $out\n\n'
            f'rule link\n  command = g++ {link_flag} $in -o $out\n')
        edges = 'include CMakeFiles/rules.ninja\n'
        for name in ('first', 'second'):
            source = build / (name + '.cpp')
            source.write_text('// Test fixture: never compiled.\n')
            edges += f'build {name}.o: compile {source}\n'
            if name == 'first' or second_pool:
                edges += '  pool = arch_cuda_heavy\n'
        order = 'second.o first.o' if reverse else 'first.o second.o'
        (build / 'build.ninja').write_text(edges + f'build ARCH: link {order}\n')
        return database

    def observe(self, name='observation'):
        return replay.require_build_equivalence(self.cold, self.reference, self.root / name)

    def test_equivalent_own_roots_and_zero_objects(self):
        contract = self.observe()
        self.assertEqual(contract['measurement']['command_count'], 3)
        self.assertEqual(contract['measurement']['heavy_pool']['depth'], 2)
        database = json.loads((self.cold / 'compile_commands.json').read_text())
        self.assertEqual(replay.require_cold_objects(database),
                         dict(configured_compile_outputs=2, existing_compile_outputs=0))

    def test_compile_flags_cannot_be_normalized_away(self):
        self.configure(self.cold, compile_flag='-O1')
        with self.assertRaisesRegex(RuntimeError, 'ARCH commands differ'):
            self.observe()

    def test_link_flags_cannot_be_normalized_away(self):
        self.configure(self.cold, link_flag='-fno-lto')
        with self.assertRaisesRegex(RuntimeError, 'ARCH commands differ'):
            self.observe()

    def test_external_roots_are_not_normalized(self):
        self.configure(self.cold, dependency='/different/provider/include')
        with self.assertRaisesRegex(RuntimeError, 'ARCH commands differ'):
            self.observe()

    def test_command_order_is_preserved(self):
        self.configure(self.cold, reverse=True)
        with self.assertRaisesRegex(RuntimeError, 'ARCH commands differ'):
            self.observe()

    def test_heavy_depth_mismatch_is_rejected(self):
        self.configure(self.cold, depth=1)
        with self.assertRaisesRegex(RuntimeError, 'heavy scheduling differ'):
            self.observe()

    def test_heavy_assignment_mismatch_is_rejected(self):
        self.configure(self.cold, second_pool=False)
        with self.assertRaisesRegex(RuntimeError, 'heavy scheduling differ'):
            self.observe()

    def test_existing_object_is_rejected(self):
        (self.cold / 'first.o').write_bytes(b'existing object')
        database = json.loads((self.cold / 'compile_commands.json').read_text())
        with self.assertRaisesRegex(RuntimeError, 'already contains a compiled object'):
            replay.require_cold_objects(database)

    def test_both_trees_changing_identically_still_invalidates_measurement(self):
        before = self.observe('before')
        self.configure(self.cold, compile_flag='-O1')
        self.configure(self.reference, compile_flag='-O1')
        after = self.observe('after')
        with self.assertRaisesRegex(RuntimeError, 'changed during measurement'):
            replay.require_unchanged_contract(before, after)


if __name__ == '__main__':
    unittest.main(verbosity=2)
