"""Build and run the test-only native wave provider in a NEW isolated directory.

Run on the idle Linux server, under tools/run_memory_guarded.py, only after the
frozen formal matrix and original BE follow-up have finished. This script never
builds ARCH, registers a provider, or qualifies nuclear/ODE/application behavior.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import subprocess
import sys
import time

BUILD_TIMEOUT = 1800
CONTRACT_TIMEOUT = 300
MATRIX = [(n, capacity) for n in (151, 201) for capacity in (1, 2, 8, 32)]
TARGET = 'arch_cuda_cudss_sparse_solver'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def unique_option(tokens, option):
    if tokens.count(option) != 1 or tokens.index(option) + 1 >= len(tokens):
        raise ValueError('missing/ambiguous original option: ' + option)
    return tokens.index(option) + 1


def rewrite_compile(entries, original, replacement, output, payload):
    selected = [e for e in entries if e['file'].endswith('/' + original)]
    if len(selected) != 1:
        raise ValueError('missing/ambiguous compile recipe: ' + original)
    entry = selected[0]
    tokens = shlex.split(entry['command'])
    if tokens.count(entry['file']) != 1 or tokens.count('-c') != 1:
        raise ValueError('ambiguous source/compile binding')
    if any(t in tokens for t in (';', '&&', '||', '>', '<', '|')):
        raise ValueError('shell syntax is not a compiler argument')
    if any(t.startswith(('-MF', '-MT', '-MQ', '-MD', '-MMD', '--dependency-')) for t in tokens):
        raise ValueError('unreviewed dependency output could mutate the frozen build')
    if any(t in tokens for t in ('-ffast-math', '--use_fast_math')):
        raise ValueError('fast math is outside this experiment')
    if tokens[0].endswith('/nvcc'):
        required = ('--fmad=false', '--ftz=false', '--prec-div=true', '--prec-sqrt=true',
                    '-Xcompiler=-fno-fast-math,-ffp-contract=off')
    else:
        required = ('-fno-fast-math', '-ffp-contract=off')
    if not all(t in tokens for t in required):
        raise ValueError('original strict FP recipe is incomplete')
    if original != 'test_cudss_sparse_solver.cpp' and tokens.count('-DARCH_CUDSS_IR_STEPS=2') != 1:
        raise ValueError('original IR=2 recipe is required')
    tokens[tokens.index(entry['file'])] = str(replacement)
    tokens[unique_option(tokens, '-o')] = str(output)
    tokens[1:1] = ['-I' + str(payload / 'src'), '-I' + str(payload / 'src/cuda/microphysics')]
    return dict(cwd=entry['directory'], command=tokens)


def rewrite_link(record, objects, output, helper):
    rows = [r for r in record['commands'] if r['name'] == 'link-' + TARGET]
    if len(rows) != 1 or rows[0].get('returncode') != 0:
        raise ValueError('one successful recorded native link is required')
    tokens = list(rows[0]['command'])
    old_objects = [i for i, token in enumerate(tokens) if token.endswith('.o')]
    if len(old_objects) != 1 or not tokens[old_objects[0]].endswith('/test_cudss_sparse_solver.cpp.o'):
        raise ValueError('unreviewed native link object inventory')
    libraries = [i for i, token in enumerate(tokens) if token.endswith('/libarch_cuda_sparse_provider.a')]
    if len(libraries) != 1:
        raise ValueError('ambiguous frozen helper archive')
    if '-fno-fast-math' not in tokens or '-ffp-contract=off' not in tokens or '-ffast-math' in tokens:
        raise ValueError('original strict FP link is incomplete')
    tokens[libraries[0]] = str(helper)
    tokens[unique_option(tokens, '-o')] = str(output)
    # Explicit fresh shared-math object BEFORE the helper archive. The old
    # archive supplies require_success, not a silently reused math object.
    index = old_objects[0]
    tokens[index:index + 1] = [str(obj) for obj in objects]
    return dict(cwd=rows[0]['cwd'], command=tokens)


def verify_payload(payload, manifest):
    record = json.loads((payload / 'overlay-record.json').read_text())
    if (record.get('status') != 'prepared_not_compiled_not_runtime_validated'
            or record.get('release_qualified') is not False or record.get('pending') is not None
            or record.get('unchanged_device_math_and_response_regions') is not True
            or len(record.get('files', {})) != 13
            or record.get('shared_manifest_sha256') != sha(manifest)):
        raise ValueError('only the complete thirteen-file prepared overlay is accepted')
    shared = json.loads(manifest.read_text())
    if len(shared.get('sha256', {})) != 9 or 'src/numerics/linalg/LinearEquilibration.h' not in shared['sha256']:
        raise ValueError('nine pinned canonical shared inputs are required')
    expected_files = set(shared['sha256']) | {
        'src/cuda/microphysics/SparseOdeBatch.cuh',
        'src/cuda/microphysics/CuDssSparseWaveSolver.h',
        'src/cuda/microphysics/CuDssSparseWaveSolver.cpp',
        'tests/cuda/test_sparse_wave.cpp'}
    if set(record['files']) != expected_files:
        raise ValueError('prepared payload file inventory differs')
    for relative, identity in record['files'].items():
        path = PurePosixPath(relative)
        if path.is_absolute() or '..' in path.parts or '\\' in relative:
            raise ValueError('unsafe payload path')
        file = payload.joinpath(*path.parts)
        if file.is_symlink() or not file.resolve(strict=True).is_relative_to(payload):
            raise ValueError('payload escaped its isolated root')
        if file.stat().st_size != identity['bytes'] or sha(file) != identity['sha256']:
            raise ValueError('payload identity changed: ' + relative)
    for relative, expected in shared['sha256'].items():
        if record['files'].get(relative, {}).get('sha256') != expected:
            raise ValueError('shared input identity is not canonical: ' + relative)
    return record, shared


def source_root(build):
    lines = (build / 'CMakeCache.txt').read_text().splitlines()
    roots = [line.split('=', 1)[1] for line in lines if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')]
    if len(roots) != 1: raise ValueError('cannot identify the immutable original source root')
    return Path(roots[0]).resolve(strict=True)


def link_dependencies(recipe):
    files = []
    for token in recipe['command'][1:]:
        if not token.startswith('-') and (token.endswith('.a') or '.so' in Path(token).name):
            candidate = Path(token)
            if not candidate.is_absolute(): candidate = Path(recipe['cwd']) / candidate
            files.append(candidate.resolve(strict=True))
    return sorted(set(files))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--recorded-link', type=Path, required=True)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--shared-manifest', type=Path, required=True)
    parser.add_argument('--helper-library', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != 'linux': parser.error('real CUDA contracts require the fixed Linux server')
    paths = {key: getattr(args, key).resolve(strict=True) for key in
             ('build_dir', 'recorded_link', 'payload', 'shared_manifest', 'helper_library')}
    out = args.output_dir
    if out.exists() or out.is_symlink(): parser.error('refuse to overwrite existing/partial evidence')
    out = out.parent.resolve(strict=True) / out.name
    for protected in (source_root(paths['build_dir']), paths['build_dir'], paths['payload']):
        if out.is_relative_to(protected) or protected.is_relative_to(out):
            parser.error('output must be outside frozen source, build and payload')
    overlay_record, manifest = verify_payload(paths['payload'], paths['shared_manifest'])
    helper_sha = manifest['optional_frozen_result_helper_archive']['sha256']
    if sha(paths['helper_library']) != helper_sha: parser.error('frozen helper identity changed')
    database = paths['build_dir'] / 'compile_commands.json'
    entries = json.loads(database.read_text())
    recorded_link = json.loads(paths['recorded_link'].read_text())
    objects = [out / name for name in ('CuDssSparseWaveSolver.cpp.o', 'SparseEquilibration.cu.o',
                                      'test_sparse_wave.cpp.o')]
    specs = [('CuDssSparseSolver.cpp', 'src/cuda/microphysics/CuDssSparseWaveSolver.cpp'),
             ('SparseEquilibration.cu', 'src/cuda/microphysics/SparseEquilibration.cu'),
             ('test_cudss_sparse_solver.cpp', 'tests/cuda/test_sparse_wave.cpp')]
    recipes = [rewrite_compile(entries, original, paths['payload'] / replacement, obj, paths['payload'])
               for (original, replacement), obj in zip(specs, objects)]
    executable = out / 'test_sparse_wave'
    recipes.append(rewrite_link(recorded_link, objects, executable, paths['helper_library']))
    if any(Path(r['cwd']).resolve() != paths['build_dir'] for r in recipes):
        parser.error('recorded commands do not belong to the selected frozen build')
    identities = {str(p): sha(p) for p in (database, paths['build_dir'] / 'CMakeCache.txt',
        paths['recorded_link'], paths['shared_manifest'], paths['helper_library'],
        paths['payload'] / 'overlay-record.json', Path(__file__), *link_dependencies(recipes[-1]))}
    out.mkdir()
    record = dict(status='building', scope='standalone-native-wave-provider-not-ODE-or-ARCH',
        release_qualified=False, command_timeouts=dict(build=BUILD_TIMEOUT, contract=CONTRACT_TIMEOUT),
        inputs=identities, payload=overlay_record, commands=[], tests={}, artifacts={},
        environment={key: os.environ.get(key) for key in
                     ('OMP_NUM_THREADS', 'OMP_PROC_BIND', 'OMP_PLACES', 'LD_LIBRARY_PATH', 'CUDA_VISIBLE_DEVICES')})
    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
    def run(name, command, cwd, timeout):
        row = dict(name=name, command=command, cwd=str(cwd), timeout=timeout, status='running')
        record['commands'].append(row)
        save()
        started = time.monotonic()
        try:
            with (out / (name + '.stdout')).open('wb') as stdout, (out / (name + '.stderr')).open('wb') as stderr:
                result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=cwd,
                    stdout=stdout, stderr=stderr, timeout=timeout)
            row.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
            if result.returncode != 0: raise RuntimeError(name + ' failed; first full command and logs retained')
        except BaseException as error:
            row.update(status='failed', error=repr(error))
            raise
        finally:
            row['wall_seconds'] = time.monotonic() - started
            save()
    try:
        for name, command in (
                ('hardware', ['nvidia-smi', '--query-gpu=name,driver_version,memory.total', '--format=csv']),
                ('host-compiler', [recipes[0]['command'][0], '--version']),
                ('cuda-compiler', [recipes[1]['command'][0], '--version']),
                ('kernel', ['uname', '-a'])):
            run(name, command, out, 30)
        for name, recipe in zip(('compile-provider', 'compile-shared-math', 'compile-test', 'link-test'), recipes):
            run(name, recipe['command'], recipe['cwd'], BUILD_TIMEOUT)
        record['artifacts'] = {str(p): sha(p) for p in [*objects, executable]}
        run('dynamic-libraries', ['ldd', str(executable)], out, 30)
        record['status'] = 'running-contracts'
        save()
        for extent, capacity in MATRIX:
            name = f'contract-n{extent}-c{capacity}'
            run(name, [str(executable), str(extent), str(capacity)], out, CONTRACT_TIMEOUT)
            expected = f'SPARSE_WAVE_CONTRACT_PASS extent={extent} capacity={capacity} '
            lines = (out / (name + '.stdout')).read_text().splitlines()
            if len([line for line in lines if line.startswith(expected)]) != 1:
                raise RuntimeError(name + ' lacks its exact completion marker')
            record['tests'][name] = dict(passed=True, extent=extent, capacity=capacity)
            save()
        verify_payload(paths['payload'], paths['shared_manifest'])
        for path, expected in {**identities, **record['artifacts']}.items():
            if sha(path) != expected: raise RuntimeError('input/product changed: ' + path)
        record.update(status='standalone-provider-contracts-passed', identities_verified_after_run=True)
    except BaseException as error:
        record.update(status='failed', error=repr(error))
        raise
    finally:
        save()
    print(json.dumps(dict(status=record['status'], contracts=len(record['tests']), release_qualified=False)))


if __name__ == '__main__':
    main()
