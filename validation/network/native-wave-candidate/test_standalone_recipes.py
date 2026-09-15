"""Recipe/inventory unit tests. No compiler, GPU, process launcher, or SSH."""
import copy
import importlib.util
import json
from pathlib import Path, PurePosixPath
import shlex
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


runner = load('run_standalone_contracts')
overlay = load('make_overlay')
ENTRIES = json.loads((ROOT / 'validation/network/results/sparse-followup-20260914/provenance/compile_commands.json').read_text())
LINK = json.loads((ROOT / 'validation/network/results/large-scheduling-20260914/evidence/records/factor-cache/pinned-status-candidate-v2/record.json').read_text())


class RecipeContracts(unittest.TestCase):
    def rewrite(self, original='CuDssSparseSolver.cpp', entries=None):
        return runner.rewrite_compile(ENTRIES if entries is None else entries, original,
            PurePosixPath('/new/payload/src/replacement'), PurePosixPath('/new/output/fresh.o'),
            PurePosixPath('/new/payload'))

    def edited_entries(self, old, new):
        entries = copy.deepcopy(ENTRIES)
        for entry in entries:
            if entry['file'].endswith('/CuDssSparseSolver.cpp'):
                self.assertIn(old, entry['command'])
                entry['command'] = entry['command'].replace(old, new)
        return entries

    def test_three_real_compile_recipes_retain_flags(self):
        for name in ('CuDssSparseSolver.cpp', 'SparseEquilibration.cu', 'test_cudss_sparse_solver.cpp'):
            with self.subTest(name=name):
                rewritten = self.rewrite(name)
                original = next(e for e in ENTRIES if e['file'].endswith('/' + name))
                before = shlex.split(original['command'])
                after = rewritten['command']
                self.assertNotIn(original['file'], after)
                self.assertEqual(after[after.index('-o') + 1], '/new/output/fresh.o')
                self.assertEqual(after[1:3], ['-I/new/payload/src', '-I/new/payload/src/cuda/microphysics'])
                expected = list(before)
                expected[expected.index(original['file'])] = '/new/payload/src/replacement'
                expected[expected.index('-o') + 1] = '/new/output/fresh.o'
                self.assertEqual(after[:1] + after[3:], expected)

    def test_duplicate_compile_recipe_rejected(self):
        entry = next(e for e in ENTRIES if e['file'].endswith('/CuDssSparseSolver.cpp'))
        with self.assertRaisesRegex(ValueError, 'ambiguous compile'):
            self.rewrite(entries=[*ENTRIES, entry])

    def test_strict_fp_cannot_be_removed(self):
        with self.assertRaisesRegex(ValueError, 'strict FP'):
            self.rewrite(entries=self.edited_entries('-ffp-contract=off', ''))

    def test_fast_math_cannot_be_added(self):
        with self.assertRaisesRegex(ValueError, 'fast math'):
            self.rewrite(entries=self.edited_entries('-ffp-contract=off', '-ffp-contract=off -ffast-math'))

    def test_ir_count_cannot_change(self):
        with self.assertRaisesRegex(ValueError, 'IR=2'):
            self.rewrite(entries=self.edited_entries('-DARCH_CUDSS_IR_STEPS=2', '-DARCH_CUDSS_IR_STEPS=0'))

    def test_dependency_output_cannot_touch_frozen_tree(self):
        with self.assertRaisesRegex(ValueError, 'dependency output'):
            self.rewrite(entries=self.edited_entries('-c ', '-MF /old/tree/dependency.d -c '))

    def test_shell_operator_rejected(self):
        with self.assertRaisesRegex(ValueError, 'shell syntax'):
            self.rewrite(entries=self.edited_entries('-c ', '&& -c '))

    def test_link_uses_all_fresh_objects_before_helper(self):
        objects = [PurePosixPath('/new/provider.o'), PurePosixPath('/new/shared_math.o'), PurePosixPath('/new/test.o')]
        result = runner.rewrite_link(LINK, objects, PurePosixPath('/new/test'), PurePosixPath('/old/verified-helper.a'))
        tokens = result['command']
        self.assertEqual([t for t in tokens if t.endswith('.o')], [str(p) for p in objects])
        self.assertLess(tokens.index('/new/shared_math.o'), tokens.index('/old/verified-helper.a'))
        self.assertEqual(tokens[tokens.index('-o') + 1], '/new/test')
        original = next(r for r in LINK['commands'] if r['name'] == 'link-' + runner.TARGET)
        self.assertEqual(result['cwd'], original['cwd'])

    def test_unreviewed_link_object_rejected(self):
        changed = copy.deepcopy(LINK)
        row = next(r for r in changed['commands'] if r['name'] == 'link-' + runner.TARGET)
        row['command'].insert(1, '/old/hidden.o')
        with self.assertRaisesRegex(ValueError, 'object inventory'):
            runner.rewrite_link(changed, [], Path('/new/test'), Path('/old/helper.a'))

    def test_failed_link_recipe_rejected(self):
        changed = copy.deepcopy(LINK)
        row = next(r for r in changed['commands'] if r['name'] == 'link-' + runner.TARGET)
        row['returncode'] = 1
        with self.assertRaisesRegex(ValueError, 'successful recorded'):
            runner.rewrite_link(changed, [], Path('/new/test'), Path('/old/helper.a'))

    def test_payload_identity_and_changed_math_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp) / 'payload'
            overlay.prepare(ROOT, HERE, dest)
            record, _ = runner.verify_payload(dest, HERE / 'shared-inputs.json')
            self.assertFalse(record['release_qualified'])
            header = dest / 'src/numerics/linalg/LinearEquilibration.h'
            header.write_bytes(header.read_bytes() + b'\n// changed\n')
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                runner.verify_payload(dest, HERE / 'shared-inputs.json')

    def test_incomplete_payload_never_qualifies(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp) / 'payload'
            overlay.prepare(ROOT, HERE, dest)
            file = dest / 'overlay-record.json'
            record = json.loads(file.read_text())
            record['pending'] = 'some unfinished write'
            file.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'complete thirteen-file'):
                runner.verify_payload(dest, HERE / 'shared-inputs.json')


if __name__ == '__main__':
    unittest.main()
