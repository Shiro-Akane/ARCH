"""Compile and test an isolated batch-launch provider only after v4 capacity.

Uses the frozen strict-FP compile/link recipes. No giant nuclear CUDA factory is
rebuilt, no production registration is changed, and no performance is qualified.
Run serially under the existing memory/pressure/GPU guard on the idle server.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import time

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
HOST_MEMBER = 'CuDssSparseWaveSolver.cpp.o'
CUDA_MEMBER = 'SparseWaveKernels.cu.o'
BASE_PROVIDER = '699e13a64c93b1322118f692c7d488f2b29a4360e669d285ff8405f3db67cd5f'
KERNEL_COMBINATIONS = {(n, p, s, a) for n in (1, 151, 201, 513)
                       for p in (1, 2, 8, 32) for s in range(4) for a in (0, 1)}


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def members(archive):
    names = subprocess.run(['/usr/bin/gcc-ar-11', 't', str(archive)],
                           capture_output=True, text=True, check=True).stdout.splitlines()
    if len(names) != len(set(names)) or names.count(HOST_MEMBER) != 1:
        raise ValueError('unambiguous original archive required')
    return {name: hashlib.sha256(subprocess.run(['/usr/bin/gcc-ar-11', 'p', str(archive), name],
                     capture_output=True, check=True).stdout).hexdigest() for name in names}


def rewrite_link(tokens, obj, provider, executable):
    if tokens[:2] != [':', '&&'] or tokens[-2:] != ['&&', ':']:
        raise ValueError('unsupported native link wrapper')
    tokens = list(tokens[2:-2])
    objects = [i for i, value in enumerate(tokens) if value.endswith('.o')]
    if (len(objects) != 1 or not tokens[objects[0]].endswith('/test_cudss_sparse_solver.cpp.o')
            or tokens.count('libarch_cuda_sparse_provider.a') != 1 or tokens.count('-o') != 1):
        raise ValueError('ambiguous native link object/provider')
    if '-ffp-contract=off' not in tokens or '-fno-fast-math' not in tokens or '-ffast-math' in tokens:
        raise ValueError('strict FP link changed')
    tokens[objects[0]] = str(obj)
    tokens[tokens.index('libarch_cuda_sparse_provider.a')] = str(provider)
    tokens[tokens.index('-o') + 1] = str(executable)
    return tokens


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    payload, out = args.input.resolve(strict=True), args.output.resolve()
    if (payload.parent != ROOT or out.parent != ROOT or out.exists() or out.is_symlink()
            or os.environ.get('LD_PRELOAD') or os.environ.get('ARCH_NATIVE_WAVE_WORK_OBSERVER')):
        raise ValueError('new isolated output, original inputs and no observers required')
    if (ROOT / 'capacity-control-v2/exit-code').read_text().strip() != '0':
        raise ValueError('complete original v4 capacity matrix required first')
    collection = json.loads((ROOT / 'capacity-collection-v2.json').read_text())
    backup = json.loads((ROOT / 'capacity-local-receipt-v2.json').read_text())
    if collection.get('capacity_matrix_pass') is not True or backup['raw'] != collection['raw']:
        raise ValueError('complete capacity evidence and byte-verified local backup required')
    for comm in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', comm], capture_output=True).returncode != 1:
            raise ValueError('another application/build is active')
    gpu = subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                         capture_output=True, text=True, check=True).stdout.strip()
    if gpu:
        raise ValueError('GPU is not idle')
    build, source = ROOT / 'factory-release', ROOT / 'source'
    original = build / 'libarch_cuda_sparse_provider.a'
    if sha(original) != BASE_PROVIDER:
        raise ValueError('frozen v4 provider changed')
    prepared = json.loads((payload / 'overlay-record.json').read_text())
    if prepared['shared_math_modified'] or prepared['factory_ABI_modified']:
        raise ValueError('execution-only overlay required')
    for name, identity in prepared['files'].items():
        if Path(name).name != name or sha(payload / name) != identity['sha256']:
            raise ValueError('payload identity changed')
    if sha(payload / 'CuDssSparseWaveSolver.h') != sha(source / 'src/cuda/microphysics/CuDssSparseWaveSolver.h'):
        raise ValueError('factory ABI header changed')
    shared = json.loads((ROOT / 'input/shared-inputs.json').read_text())['sha256']
    for name, expected in shared.items():
        if sha(source / name) != expected:
            raise ValueError('shared numerical input changed: ' + name)
    helper = ROOT / 'input/run_standalone_contracts.py'
    spec = importlib.util.spec_from_file_location('strict_recipe', helper)
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    entries = json.loads((build / 'compile_commands.json').read_text())
    before_members = members(original)
    out.mkdir()
    paths = [original, helper, build / 'compile_commands.json', build / 'CMakeCache.txt', Path(__file__),
             payload / 'overlay-record.json', payload / 'test_wave_kernels.cpp',
             source / 'tests/cuda/test_sparse_wave.cpp']
    paths += [payload / name for name in prepared['files']]
    paths += [source / name for name in shared]
    inputs = {str(path): sha(path) for path in paths}
    record = dict(status='building', inputs=inputs, commands=[], tests={}, artifacts={},
                  original_archive_members=before_members, release_qualified=False,
                  nuclear_qualified=False, performance_qualified=False)

    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')

    def run(name, command, cwd=build, timeout=1800):
        row = dict(name=name, command=command, cwd=str(cwd), timeout=timeout, status='running')
        record['commands'].append(row)
        save()
        started = time.monotonic()
        try:
            with (out / (name + '.stdout')).open('wb') as stdout, (out / (name + '.stderr')).open('wb') as stderr:
                result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=cwd,
                                        stdout=stdout, stderr=stderr, timeout=timeout)
            row.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
            if result.returncode:
                raise RuntimeError('first failure retained: ' + name)
        except BaseException as error:
            row.update(status='failed', error=repr(error))
            raise
        finally:
            row['elapsed_seconds'] = time.monotonic() - started
            save()

    try:
        for name, original_source, replacement, obj in (
                ('compile-provider', 'CuDssSparseWaveSolver.cpp', payload / 'CuDssSparseWaveSolver.cpp', out / HOST_MEMBER),
                ('compile-batch-kernels', 'SparseEquilibration.cu', payload / 'SparseWaveKernels.cu', out / CUDA_MEMBER),
                ('compile-kernel-test', 'test_cudss_sparse_solver.cpp', payload / 'test_wave_kernels.cpp', out / 'kernel-test.o'),
                ('compile-provider-test', 'test_cudss_sparse_solver.cpp', source / 'tests/cuda/test_sparse_wave.cpp', out / 'provider-test.o')):
            rewritten = recipe.rewrite_compile(entries, original_source, replacement, obj, source)
            rewritten['command'][1:1] = ['-I' + str(payload)]
            run(name, rewritten['command'], Path(rewritten['cwd']))
        private = out / 'libarch_cuda_sparse_provider.a'
        shutil.copyfile(original, private)
        run('private-archive', ['/usr/bin/gcc-ar-11', 'rs', str(private), str(out / HOST_MEMBER), str(out / CUDA_MEMBER)])
        after_members = members(private)
        expected_members = dict(before_members, **{HOST_MEMBER: sha(out / HOST_MEMBER), CUDA_MEMBER: sha(out / CUDA_MEMBER)})
        if after_members != expected_members:
            raise ValueError('archive changed beyond one Host object and one added CUDA object')
        record['private_archive_members'] = after_members
        native = shlex.split(subprocess.run(['ninja', '-t', 'commands', 'arch_cuda_cudss_sparse_solver'],
            cwd=build, capture_output=True, text=True, check=True).stdout.splitlines()[-1])
        for name in ('kernel-test', 'provider-test'):
            run('link-' + name, rewrite_link(native, out / (name + '.o'), private, out / name))
            run('ldd-' + name, ['ldd', str(out / name)], timeout=30)
        record['artifacts'] = {str(path): sha(path) for path in
            (out / HOST_MEMBER, out / CUDA_MEMBER, out / 'kernel-test.o', out / 'provider-test.o',
             private, out / 'kernel-test', out / 'provider-test')}
        run('kernel-matrix', [str(out / 'kernel-test')], timeout=300)
        lines = (out / 'kernel-matrix.stdout').read_text().splitlines()
        expected_lines = {f'WAVE_KERNEL_BITWISE_PARITY_PASS extent={n} capacity={p} scenario={s} all_active={a}'
                          for n, p, s, a in KERNEL_COMBINATIONS}
        results = [line for line in lines if line.startswith('WAVE_KERNEL_BITWISE_PARITY_PASS ')]
        if (len(results) != len(expected_lines) or set(results) != expected_lines or not lines
                or lines[-1] != f'WAVE_KERNEL_MATRIX_PASS cases={len(KERNEL_COMBINATIONS)} scope=execution-equivalence-not-nuclear-or-performance'):
            raise ValueError('missing complete kernel matrix')
        record['tests']['kernel-matrix'] = dict(passed=True, cases=len(KERNEL_COMBINATIONS))
        for extent in (151, 201):
            for capacity in (1, 2, 8, 32):
                name = f'provider-n{extent}-c{capacity}'
                run(name, [str(out / 'provider-test'), str(extent), str(capacity)], timeout=300)
                prefix = f'SPARSE_WAVE_CONTRACT_PASS extent={extent} capacity={capacity} '
                if sum(line.startswith(prefix) for line in (out / (name + '.stdout')).read_text().splitlines()) != 1:
                    raise ValueError('missing provider contract completion')
                record['tests'][name] = dict(passed=True)
        for path, expected in {**inputs, **record['artifacts']}.items():
            if sha(path) != expected:
                raise ValueError('input/product changed: ' + path)
        record.update(status='kernel-and-provider-contracts-passed', identities_verified_after=True)
    except BaseException as error:
        record.update(status='failed_partial_preserved', error=repr(error))
        raise
    finally:
        save()
    print(json.dumps(dict(status=record['status'], tests=len(record['tests']), nuclear_qualified=False)))


if __name__ == '__main__':
    main()
