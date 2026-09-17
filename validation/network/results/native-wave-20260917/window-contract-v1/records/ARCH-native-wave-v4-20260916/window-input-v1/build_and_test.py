"""Build ONLY the Host window wrapper and manufactured GPU contracts.

Uses the previously qualified unmodified native batch-kernel provider. No CUDA
factory, ODE, generated network, CMake registration, library or precision change.
This program is not a dispatcher: run only after current work is archived and
under the existing memory/pressure guard on an idle GPU.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

from protocol import COMMANDS_SHA, MATRIX, PROVIDER_SHA, extract_support, link_recipe, parse_result, verify_record

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def backed_up(collection_name, receipt_name, required=None):
    collection = json.loads((ROOT/collection_name).read_text())
    receipt = json.loads((ROOT/receipt_name).read_text())
    if (receipt.get('status') != 'both_archives_and_all_members_byte_verified'
            or receipt['raw'] != collection['raw'] or receipt['compact'] != collection['compact']
            or (required and collection.get(required) is not True)):
        raise ValueError('required predecessor evidence/local backup not complete: '+collection_name)
    for name in ('raw', 'compact'):
        item = collection[name]
        path = Path(item['path'])
        if not path.is_file() or path.stat().st_size != item['bytes'] or sha(path) != item['sha256']:
            raise ValueError('predecessor archive identity changed: '+collection_name)


def main():
    payload = Path(__file__).resolve().parent
    out = ROOT/'window-contract-v1'
    if payload != ROOT/'window-input-v1' or out.exists() or out.is_symlink():
        raise ValueError('new isolated output and expected frozen payload are required')
    if any(os.environ.get(name) for name in ('LD_PRELOAD', 'ARCH_SPARSE_ADVANCE_THREADS', 'ARCH_NATIVE_WAVE_WORK_OBSERVER')):
        raise ValueError('no inherited experimental overrides permitted')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another application or build is active')
    gpu = subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                         capture_output=True, text=True, check=True).stdout.strip()
    if gpu:
        raise ValueError('GPU must be idle')
    # Leaf outcome can be negative; finishing and preserving it is mandatory.
    # Native provider contracts must have passed, not just been archived.
    backed_up('leaf-inline-collection-v1.json', 'leaf-inline-local-receipt-v1.json')
    backed_up('batch-launch-collection-v1.json', 'batch-launch-local-receipt-v1.json', 'contracts_pass')
    build, source = ROOT/'factory-release', ROOT/'source'
    provider = ROOT/'batch-launch-contract-v1/libarch_cuda_sparse_provider.a'
    if sha(provider) != PROVIDER_SHA or sha(build/'compile_commands.json') != COMMANDS_SHA:
        raise ValueError('qualified provider or strict factory recipe changed')
    helper = ROOT/'input/run_standalone_contracts.py'
    spec = importlib.util.spec_from_file_location('original_strict_window_recipe', helper)
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    entries = json.loads((build/'compile_commands.json').read_text())
    original_support = source/'tests/cuda/test_sparse_wave.cpp'
    support = extract_support(original_support.read_bytes())
    # Entire immutable source/network/vendor/old-artifact manifests are checked
    # by the outer worker before/after. These are the new command dependencies.
    paths = [provider, helper, original_support, build/'compile_commands.json', build/'CMakeCache.txt']
    paths += [path for path in payload.iterdir() if path.is_file()]
    paths += [source/'src/cuda/microphysics/CuDssSparseWaveSolver.h',
              source/'src/cuda/common/DeviceAllocation.h', source/'src/numerics/linalg/CsrMatrixView.h']
    if any(path.is_symlink() for path in paths):
        raise ValueError('symlink payload/dependency not permitted')
    inputs = {str(path): sha(path) for path in paths}
    out.mkdir()
    support_path = out/'shared_sparse_wave_test_support.h'
    support_path.write_bytes(support)
    inputs[str(support_path)] = sha(support_path)
    record = dict(status='building', inputs=inputs, commands=[], tests={}, artifacts={},
                  nuclear_qualified=False, performance_qualified=False, release_qualified=False,
                  shared_math_modified=False, native_provider_modified=False,
                  manufactured_solution_budget=1e-12, provider_budget_bytes=256*1024*1024,
                  matrix=[list(row) for row in MATRIX])

    def save():
        (out/'record.json').write_text(json.dumps(record, indent=2)+'\n')

    def run(name, command, cwd=build, timeout=300):
        row = dict(name=name, command=command, cwd=str(cwd), timeout=timeout, status='running')
        record['commands'].append(row)
        save()
        print('WINDOW_STARTED '+name, flush=True)
        start = time.monotonic()
        try:
            with (out/(name+'.stdout')).open('wb') as stdout, (out/(name+'.stderr')).open('wb') as stderr:
                result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=cwd,
                                        stdout=stdout, stderr=stderr, timeout=timeout)
            row.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
            if result.returncode:
                raise RuntimeError('first window failure preserved: '+name)
        except BaseException as error:
            row.update(status='failed', error=repr(error))
            raise
        finally:
            row['elapsed_seconds'] = time.monotonic()-start
            save()

    try:
        for name, original, actual, obj in (
                ('compile-window', 'CuDssSparseWaveSolver.cpp', 'CuDssSparseWindowSolver.cpp', 'window.o'),
                ('compile-test', 'test_cudss_sparse_solver.cpp', 'test_sparse_window.cpp', 'window-test.o')):
            rewritten = recipe.rewrite_compile(entries, original, payload/actual, out/obj, source)
            if not rewritten['command'][0].endswith('/g++-11'):
                raise ValueError('window contracts must not trigger a heavy CUDA compilation')
            rewritten['command'][1:1] = ['-I'+str(payload), '-I'+str(out)]
            run(name, rewritten['command'], Path(rewritten['cwd']))
        original = shlex.split(subprocess.run(['ninja', '-t', 'commands', 'arch_cuda_cudss_sparse_solver'],
            cwd=build, capture_output=True, text=True, check=True).stdout.splitlines()[-1])
        run('link-test', link_recipe(original, out/'window.o', out/'window-test.o', provider, out/'window-test'))
        run('ldd-test', ['ldd', str(out/'window-test')], timeout=30)
        record['artifacts'] = {str(out/name): sha(out/name) for name in ('window.o', 'window-test.o', 'window-test')}
        for n, w, c in MATRIX:
            name = f'window-n{n}-w{w}-c{c}'
            run(name, [str(out/'window-test'), str(n), str(w), str(c)])
            record['tests'][name] = parse_result((out/(name+'.stdout')).read_text(), n, w, c)
            save()
        for path, expected in {**inputs, **record['artifacts']}.items():
            if sha(path) != expected:
                raise ValueError('frozen window input/artifact changed: '+path)
        record.update(status='window-contracts-passed', identities_verified_after=True)
        verify_record(record, out)
    except BaseException as error:
        record.update(status='failed_partial_preserved', error=repr(error))
        raise
    finally:
        save()
    print(json.dumps(dict(status=record['status'], contracts=len(record['tests']),
                         nuclear_qualified=False, performance_qualified=False)), flush=True)


if __name__ == '__main__':
    main()
