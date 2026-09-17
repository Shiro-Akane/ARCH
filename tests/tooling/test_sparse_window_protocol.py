"""Local source/recipe checks only; never counts as actual CUDA contracts."""
import ast
import copy
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT/'validation/network/native-wave-candidate/windowed'
spec = importlib.util.spec_from_file_location('sparse_window_protocol', DIRECTORY/'protocol.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ARCHIVE = ROOT/'validation/network/results/native-wave-20260917/factory-focused-v1/records/ARCH-native-wave-v4-20260916'


def result(n, w, c):
    values = dict(pages=17, restored=1, factor_calls=3, factor_systems=3*c,
                  solve_calls=5, solve_systems=5*c, estimated_peak_bytes=1000)
    line = f'SPARSE_WINDOW_CONTRACT_PASS extent={n} window={w} capacity={c} '
    return line+' '.join(f'{key}={value}' for key, value in values.items()), values


def fixture(output):
    record = dict(status='window-contracts-passed', identities_verified_after=True, tests={}, commands=[],
                  nuclear_qualified=False, performance_qualified=False, release_qualified=False,
                  shared_math_modified=False, native_provider_modified=False,
                  manufactured_solution_budget=1e-12, provider_budget_bytes=256*1024*1024,
                  matrix=[list(row) for row in module.MATRIX], artifacts={'wrapper': 'a', 'test': 'b', 'exe': 'c'})
    for name in ('compile-window', 'compile-test', 'link-test', 'ldd-test'):
        command = ['/usr/bin/g++-11', '-fno-fast-math', '-ffp-contract=off']
        record['commands'].append(dict(name=name, status='passed', returncode=0, command=command))
    for n, w, c in module.MATRIX:
        name = f'window-n{n}-w{w}-c{c}'
        text, counters = result(n, w, c)
        (output/(name+'.stdout')).write_text(text+'\n')
        record['tests'][name] = counters
        record['commands'].append(dict(name=name, status='passed', returncode=0,
            command=[str(output/'window-test'), str(n), str(w), str(c)]))
    return record


class WindowProtocolTests(unittest.TestCase):
    def test_support_and_recipe_pins_match_archived_bytes(self):
        original = (ARCHIVE/'source/tests/cuda/test_sparse_wave.cpp').read_bytes()
        extracted = module.extract_support(original)
        prefix = original.split(b'template<int N> void run(int capacity) {')[0]
        self.assertEqual(extracted, b'#pragma once\n'+prefix+b'\n} // namespace\n')
        for token in (b'maximum < 1e-12', b'solution_accurate', b'verify_negative_gate', b'TransferCompletion'):
            self.assertIn(token, extracted)
        with self.assertRaises(ValueError): module.extract_support(original.replace(b'1e-12', b'1e-6'))
        data = (ARCHIVE/'factory-release/compile_commands.json').read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), module.COMMANDS_SHA)

    def test_matrix_covers_native_window_tails_and_resource_bounds(self):
        self.assertEqual(len(module.MATRIX), 18)
        self.assertEqual(len(set(module.MATRIX)), 18)
        for n in (151, 201):
            for pair in ((1, 1), (2, 1), (3, 2), (9, 8), (33, 32), (64, 32), (128, 32)):
                self.assertIn((n, *pair), module.MATRIX)
        self.assertTrue(all(1 <= c <= 32 and c <= w <= 128 for _, w, c in module.MATRIX))

    def test_link_only_adds_host_wrapper_and_test_objects(self):
        original = [':', '&&', '/usr/bin/g++-11', '-fno-fast-math', '-ffp-contract=off',
                    'CMakeFiles/a/test_cudss_sparse_solver.cpp.o', '-o', 'old-test',
                    'libarch_cuda_sparse_provider.a', '/vendor/libcudss.so.0', '&&', ':']
        changed = module.link_recipe(original, 'wrapper.o', 'test.o', 'frozen.a', 'new-test')
        self.assertEqual(changed, ['/usr/bin/g++-11', '-fno-fast-math', '-ffp-contract=off', 'test.o',
                                  'wrapper.o', '-o', 'new-test', 'frozen.a', '/vendor/libcudss.so.0'])
        for bad in (original+['x'], original[:3]+['-ffast-math']+original[3:],
                    [t for t in original if t != '-ffp-contract=off'],
                    original[:5]+['extra.o']+original[5:]):
            with self.assertRaises(ValueError): module.link_recipe(bad, 'w.o', 't.o', 'p.a', 'exe')

    def test_native_work_budget_and_actual_dimensions_are_mandatory(self):
        line, counters = result(151, 33, 32)
        self.assertEqual(module.parse_result(line, 151, 33, 32), counters)
        for wrong in ('', line+'\n'+line, line.replace('window=33', 'window=32'),
                      line.replace('factor_systems=96', 'factor_systems=3'),
                      line.replace('estimated_peak_bytes=1000', 'estimated_peak_bytes=999999999'),
                      line.replace('restored=1', 'restored=0'), line+' pages=3'):
            with self.assertRaises(ValueError): module.parse_result(wrong, 151, 33, 32)

    def test_full_matrix_success_and_reject_missing_overstated_or_forged(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)
            good = fixture(output)
            module.verify_record(good, output)
            mutations = [lambda r: r.update(performance_qualified=True),
                         lambda r: r.update(nuclear_qualified=True),
                         lambda r: r.update(provider_budget_bytes=512*1024*1024),
                         lambda r: r.update(manufactured_solution_budget=1e-6),
                         lambda r: r['commands'].pop(),
                         lambda r: r['commands'].append(copy.deepcopy(r['commands'][-1])),
                         lambda r: r['commands'][0]['command'].append('-ffast-math'),
                         lambda r: r['commands'][4]['command'].__setitem__(-2, '32'),
                         lambda r: r['tests']['window-n151-w1-c1'].update(factor_calls=999)]
            for change in mutations:
                bad = copy.deepcopy(good); change(bad)
                with self.assertRaises(ValueError): module.verify_record(bad, output)
            (output/'window-n201-w128-c32.stdout').write_text('SPARSE_WINDOW_CONTRACT_FAIL: expected synthetic failure\n')
            with self.assertRaises(ValueError): module.verify_record(good, output)

    def test_preparation_syntax_and_scope_no_kernel_or_physics_body(self):
        for name in ('protocol.py', 'build_and_test.py', 'collect_contracts.py'):
            ast.parse((DIRECTORY/name).read_text(encoding='utf-8'))
        source = (DIRECTORY/'CuDssSparseWindowSolver.cpp').read_text()
        self.assertNotIn('__global__', source)
        self.assertNotIn('cudssExecute', source)
        self.assertIn('auto proposed = p.preflight(tasks, proposed_next)', source)
        self.assertIn('p.logical = std::move(proposed)', source)
        self.assertLess(source.index('auto proposed = p.preflight'), source.index('p.native->execute(p.page)'))
        self.assertGreater(source.index('p.logical = std::move(proposed)'), source.index('p.native->execute(p.page)'))


if __name__ == '__main__':
    unittest.main()
