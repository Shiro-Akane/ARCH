"""Observe native Host API costs without new fences or changed launch shapes.

Uses the same six already-qualified focused executables/routes. Nested Host API
latencies are NOT additive GPU kernel time. No production/performance approval.
"""
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from run_trajectories import validate, trajectory_args, METHODS, sha

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
PINNED = {'cuda_progress_after_launch.cpp': 'c046281beb191b5a3f00ce62c41b17880c9a71ae15e3cad5ce9c3349708ab7d2',
          'probe-cudss-progress.cpp': 'a4196bf3eab9fe6363f871159bb25fb6da6f38fe1d107e0ed3b7611e9fe71b39'}


def observer_result(text, network, method):
    native, cuda = {}, {}
    for line in text.splitlines():
        if line.startswith('NATIVE_OBSERVER,'):
            fields = line.split(',')
            if len(fields) != 4 or fields[1] in native:
                raise ValueError('invalid/duplicate native observer row')
            native[fields[1]] = dict(calls=int(fields[2]), host_seconds=float(fields[3]))
        if line.startswith('CUDA_OBSERVER,'):
            fields = line.split(',')
            if len(fields) != 8 or fields[1] in cuda:
                raise ValueError('invalid/duplicate CUDA observer row')
            cuda[fields[1]] = dict(calls=int(fields[2]), host_seconds=float(fields[3]),
                first_host_seconds=float(fields[4]), bytes=int(fields[5]), blocks=int(fields[6]), threads=int(fields[7]))
    if set(native) != {'analysis', 'factorization', 'solve', 'stream_sync'} or not cuda:
        raise ValueError('missing complete final observer data')
    for row in [*native.values(), *cuda.values()]:
        if row['calls'] <= 0 or not math.isfinite(row['host_seconds']) or row['host_seconds'] < 0:
            raise ValueError('invalid observer count/duration')
    solver = {'be_nr': 'Solver_BE_NR', 'bd': 'Solver_BD', 'ros4': 'Solver_ROS4'}[method]
    matched = [key for key in cuda if key.startswith('launch:') and 'advance_ode' in key
               and f'NetCustom_audit{network}' in key and solver in key]
    if len(matched) != 1 or cuda[matched[0]]['threads'] != 32 * cuda[matched[0]]['blocks']:
        raise ValueError('expected unmodified ODE launch not observed')
    if not any(key.startswith('memcpy_async:2:after:') and 'advance_ode' in key for key in cuda):
        raise ValueError('D2H-after-advance observation missing')
    return dict(native=native, cuda=cuda, scope='nested-host-latency-not-additive-GPU-kernel-time')


def main():
    inputs_dir, out = Path(__file__).resolve().parent, ROOT / 'api-cost-focused-v1'
    if out.exists() or os.environ.get('LD_PRELOAD') or os.environ.get('ARCH_SPARSE_ADVANCE_THREADS'):
        raise ValueError('new output and no inherited observers required')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another build/application active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU is not idle')
    parent, receipt = ROOT / 'batch-launch-focused-reaudit-collection-v1.json', ROOT / 'batch-launch-focused-reaudit-local-receipt-v1.json'
    p, local = json.loads(parent.read_text()), json.loads(receipt.read_text())
    if not p.get('revalidation_pass') or not p.get('trajectory_matrix_pass') or p['raw'] != local['raw']:
        raise ValueError('completed scientific re-audit and verified backup required')
    shape, shape_receipt = ROOT / 'advance-shape-collection-v1.json', ROOT / 'advance-shape-local-receipt-v1.json'
    s, sl = json.loads(shape.read_text()), json.loads(shape_receipt.read_text())
    if not s.get('diagnostic_contracts_pass') or s['raw'] != sl['raw']:
        raise ValueError('previous isolated diagnostic must be archived first')
    qualification = ROOT / 'focused-reaudit-v1/qualification.json'
    q = json.loads(qualification.read_text())
    for path, digest in q['inputs'].items():
        if sha(path) != digest:
            raise ValueError('qualified scientific input changed')
    native_record = ROOT / 'batch-launch-focused-v1/record.json'
    old = json.loads(native_record.read_text())
    exes = {}
    for artifact in old['artifacts']:
        for key in ('executable', 'factory'):
            if sha(artifact[key + '_path']) != artifact[key + '_sha256']:
                raise ValueError('original executable/factory changed')
        exes[int(artifact['network'][5:])] = Path(artifact['executable_path'])
    if set(exes) != {150, 200}:
        raise ValueError('both actual executables required')
    for filename, digest in PINNED.items():
        if sha(inputs_dir / filename) != digest:
            raise ValueError('reviewed observer source changed')
    paths = [parent, receipt, shape, shape_receipt, qualification, native_record, *exes.values(),
             *[p for p in inputs_dir.iterdir() if p.is_file()]]
    inputs = {str(path): sha(path) for path in paths}
    out.mkdir()
    record = dict(status='running', steps=4, duration=1e-10, runtime_timeout_seconds=1800,
                  commands=[], runs=[], inputs=inputs, artifacts={}, observer=dict(sources=PINNED),
                  scope='six-focused-trajectories-native-api-cost-diagnostic-only',
                  performance_qualified=False, application_qualified=False, release_qualified=False)

    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')

    def run(name, command, *, env=None, wall=1800):
        row = dict(name=name, command=command, cwd=str(ROOT / 'source'), timeout_seconds=wall,
                   environment={'LD_PRELOAD': env['LD_PRELOAD']} if env else {})
        record['commands'].append(row)
        save()
        start = time.monotonic()
        try:
            with (out / (name + '.stdout')).open('w') as stdout, (out / (name + '.stderr')).open('w') as stderr:
                code = subprocess.run(command, cwd=ROOT / 'source', env=env, stdout=stdout, stderr=stderr, timeout=wall).returncode
        except subprocess.TimeoutExpired:
            row.update(returncode=None, timed_out=True, elapsed_seconds=time.monotonic() - start)
            save()
            raise
        row.update(returncode=code, timed_out=False, elapsed_seconds=time.monotonic() - start)
        save()
        if code:
            raise RuntimeError(f'{name} exit {code}')

    try:
        libraries = []
        for filename in PINNED:
            library = out / (Path(filename).stem + '.so')
            command = ['/usr/bin/time', '-v', '/usr/bin/g++-11', '-std=c++17', '-O2', '-fPIC', '-shared',
                '-fno-fast-math', '-ffp-contract=off', '-I/home/ubuntu/projects/.envs/arch/targets/x86_64-linux/include',
                '-I/home/ubuntu/projects/ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12/include',
                str(inputs_dir / filename), '-o', str(library), '-ldl', '-pthread']
            run('compile-' + Path(filename).stem, command, wall=300)
            record['artifacts'][str(library)] = sha(library)
            libraries.append(str(library))
            save()
        preload = ':'.join(libraries)
        record['observer']['preload'] = preload
        for network in (150, 200):
            for method in METHODS:
                name = f'audit{network}-{method}-pool2'
                run(name, [str(exes[network]), *trajectory_args('focused', method, 2)],
                    env=dict(os.environ, LD_PRELOAD=preload))
                lines = (out / (name + '.stdout')).read_text().splitlines()
                observations = observer_result((out / (name + '.stderr')).read_text(), network, method)
                record['runs'].append(dict(name=name, passed=True, observations=observations,
                    metrics=[line for line in lines if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]))
                save()
        record['status'] = 'passed'
        record['numerical_validation'] = validate('focused', record, out, 0, diagnostic=True)
        for path, digest in {**inputs, **record['artifacts']}.items():
            if sha(path) != digest:
                raise ValueError('diagnostic input/product changed')
        record['identities_verified_after'] = True
    except BaseException as error:
        record.update(status='failed', error=repr(error))
        raise
    finally:
        save()
    print('API_COST_DIAGNOSTIC_PASS six_original_focused_routes no_formal_performance_claim')


if __name__ == '__main__':
    main()
