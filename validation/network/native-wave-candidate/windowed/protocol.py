"""Window-only experiment recipes. Tooling checks do not qualify GPU behavior."""
import hashlib
from pathlib import Path
import re

SUPPORT_SHA = 'd9c769a9893cd1706ab6de8fcd62552d546e6c2586b315d447f745b77a414875'
PROVIDER_SHA = '0b902a1e7d87174bc395d4be328713390d36a28320353d55113678e57f697204'
COMMANDS_SHA = '6a17407cf53fba1ad868e783245ea21c56274db29829e4eea0dd32bc0ba62049'
WINDOWS = ((1, 1), (2, 1), (3, 2), (8, 8), (9, 8), (32, 32), (33, 32), (64, 32), (128, 32))
MATRIX = tuple((n, w, c) for n in (151, 201) for w, c in WINDOWS)
FIELDS = ('pages', 'restored', 'factor_calls', 'factor_systems', 'solve_calls', 'solve_systems', 'estimated_peak_bytes')


def extract_support(original):
    if hashlib.sha256(original).hexdigest() != SUPPORT_SHA:
        raise ValueError('unreviewed original manufactured test support')
    marker = b'template<int N> void run(int capacity) {'
    if original.count(marker) != 1:
        raise ValueError('ambiguous original support boundary')
    prefix = original.split(marker, 1)[0]
    if prefix.count(b'namespace {') != 1 or prefix.count(b'} // namespace') != 0:
        raise ValueError('unreviewed support namespace')
    # All independent solution, residual, failure classifier and transfer
    # lifetime functions remain byte-identical. Only close the existing scope.
    return b'#pragma once\n' + prefix + b'\n} // namespace\n'


def link_recipe(tokens, wrapper_obj, test_obj, provider, executable):
    if tokens[:2] != [':', '&&'] or tokens[-2:] != ['&&', ':']:
        raise ValueError('unsupported native link wrapper')
    argv = list(tokens[2:-2])
    objects = [i for i, value in enumerate(argv) if value.endswith('.o')]
    if (len(objects) != 1 or not argv[objects[0]].endswith('/test_cudss_sparse_solver.cpp.o')
            or argv.count('libarch_cuda_sparse_provider.a') != 1 or argv.count('-o') != 1):
        raise ValueError('ambiguous native object/provider binding')
    if ('-fno-fast-math' not in argv or '-ffp-contract=off' not in argv
            or any(flag in argv for flag in ('-ffast-math', '--use_fast_math'))):
        raise ValueError('strict FP link changed')
    argv[argv.index('-o')+1] = str(executable)
    argv[argv.index('libarch_cuda_sparse_provider.a')] = str(provider)
    i = objects[0]
    argv[i:i+1] = [str(test_obj), str(wrapper_obj)]
    return argv


def parse_result(text, n, window, capacity):
    if (n, window, capacity) not in MATRIX:
        raise ValueError('unregistered window contract')
    lines = [line for line in text.splitlines() if line.startswith('SPARSE_WINDOW_CONTRACT_')]
    prefix = f'SPARSE_WINDOW_CONTRACT_PASS extent={n} window={window} capacity={capacity} '
    if len(lines) != 1 or not lines[0].startswith(prefix):
        raise ValueError('missing/ambiguous/wrong window result')
    pairs = [part.split('=') for part in lines[0][len(prefix):].split()]
    if (len(pairs) != len(FIELDS) or any(len(pair) != 2 for pair in pairs)
            or tuple(pair[0] for pair in pairs) != FIELDS
            or any(not re.fullmatch(r'\d+', pair[1]) for pair in pairs)):
        raise ValueError('invalid native/logical counter schema')
    result = {key: int(value) for key, value in pairs}
    if (result['pages'] < 1 or result['factor_calls'] < 1 or result['solve_calls'] < 1
            or result['factor_systems'] != result['factor_calls']*capacity
            or result['solve_systems'] != result['solve_calls']*capacity
            or not 0 < result['estimated_peak_bytes'] <= 256*1024*1024
            or result['restored'] < 1):
        raise ValueError('missing work/cost accounting or enlarged memory budget')
    # Even resident single-page cases restore after explicit invalidation.
    return result


def verify_record(record, output):
    expected = {f'window-n{n}-w{w}-c{c}' for n, w, c in MATRIX}
    if (record.get('status') != 'window-contracts-passed' or record.get('identities_verified_after') is not True
            or set(record.get('tests', {})) != expected
            or record.get('shared_math_modified') is not False or record.get('native_provider_modified') is not False
            or record.get('manufactured_solution_budget') != 1e-12
            or record.get('provider_budget_bytes') != 256*1024*1024
            or record.get('matrix') != [list(row) for row in MATRIX]
            or any(record.get(field) is not False for field in
                   ('nuclear_qualified', 'performance_qualified', 'release_qualified'))):
        raise ValueError('incomplete or overstated window qualification')
    commands = record.get('commands', [])
    named = {row['name']: row for row in commands}
    if (len(named) != len(commands) or set(named) != expected | {'compile-window', 'compile-test', 'link-test', 'ldd-test'}
            or any(row.get('status') != 'passed' or row.get('returncode') != 0 for row in commands)):
        raise ValueError('missing/duplicate/failed actual window command')
    for name in ('compile-window', 'compile-test', 'link-test'):
        argv = named[name]['command']
        if (not argv or not argv[0].endswith('/g++-11') or '-fno-fast-math' not in argv
                or '-ffp-contract=off' not in argv or any(flag in argv for flag in ('-ffast-math', '--use_fast_math'))):
            raise ValueError('recorded window build changed strict Host compilation')
    if len(record.get('artifacts', {})) != 3:
        raise ValueError('missing wrapper/test object and executable identities')
    for n, w, c in MATRIX:
        name = f'window-n{n}-w{w}-c{c}'
        argv = named[name]['command']
        if argv != [str(Path(output)/'window-test'), str(n), str(w), str(c)]:
            raise ValueError('actual window command differs from declared matrix')
        actual = parse_result((Path(output)/(name+'.stdout')).read_text(), n, w, c)
        if record['tests'][name] != actual:
            raise ValueError('declared window counters differ from raw result')
