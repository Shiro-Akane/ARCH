"""Isolated same-binary launch-shape ABBA diagnostic, never formal performance.

Runs only after the batched-kernel focused trajectories and verified backup.
Compiles one Host preload probe; does not rebuild factory or alter production.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
METHODS = {'bd': (2, 1), 'ros4': (3, 2)}
THREADS = (32, 1, 1, 32)


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            h.update(chunk)
    return h.hexdigest()


def arguments(exe, method):
    return [str(exe), '1e7', '3e9', '1e-10', '1e8', '1e-7', '4',
            '--ode', method, '--storage-cells', '32', '33', '--pool-cells', '32',
            'c12=0.5', 'o16=0.5']


def validate(stdout, stderr, network, method, threads):
    method_id, kernel_index = METHODS[method]
    kernel_index += 3 if network == 200 else 0
    lines = stdout.splitlines()
    controls = [line.split(',') for line in lines if line.startswith('controls,')]
    if (len(controls) != 1 or len(controls[0]) != 11
            or controls[0][1:3] != [f'custom:audit{network}', str(network + 1)]
            or [float(x) for x in controls[0][3:8]] != [1e7, 3e9, 1e-10, 1e8, 1e-7]
            or controls[0][8:] != ['4', 'selected_ode', str(method_id)]
            or lines.count('storage_controls,32,33,32') != 1
            or lines.count('GENERATED_SPARSE_BURN_PARITY_PASS') != 1):
        raise ValueError('actual controls/completion mismatch')
    result = {}
    for kind in ('cpu_step', 'gpu_step'):
        rows = [line.split(',') for line in lines if line.startswith(kind + ',')]
        if (any(len(row) != 7 for row in rows)
                or [(int(r[1]), int(r[2]), int(r[3])) for r in rows]
                != [(method_id, storage, step) for storage in (32, 33) for step in range(4)]
                or any(not math.isfinite(float(r[6])) or float(r[6]) < 0 for r in rows)):
            raise ValueError('missing/invalid actual macro steps')
        result[kind + '_seconds'] = sum(float(row[6]) for row in rows)
        result[kind + '_work_counts'] = [sum(int(row[i]) for row in rows) for i in (4, 5)]
    aggregate = [line.split(',') for line in lines if line.startswith('metrics,')]
    if len(aggregate) != 1 or len(aggregate[0]) != 9 or aggregate[0][1] != str(method_id) or aggregate[0][7] != '32':
        raise ValueError('missing aggregate')
    for value, bound in zip(aggregate[0][4:6], (2e-10, 2e-8)):
        if not math.isfinite(float(value)) or not 0 <= float(value) <= bound:
            raise ValueError('original numerical budget exceeded')
    if not math.isfinite(float(aggregate[0][6])) or float(aggregate[0][6]) <= 0:
        raise ValueError('no real composition evolution')
    shape_lines = [line for line in stderr.splitlines() if line.startswith('ADVANCE_SHAPE_DIAGNOSTIC ')]
    pattern = (r'ADVANCE_SHAPE_DIAGNOSTIC index=(\d+) calls=(\d+) errors=(\d+) '
               r'max_cells=(\d+) max_blocks=(\d+) threads=(\d+) kernel=(\S+)')
    parsed = re.fullmatch(pattern, shape_lines[0]) if len(shape_lines) == 1 else None
    if parsed is None:
        raise ValueError('missing or ambiguous actual shape observer')
    index, calls, errors, cells, blocks, selected = map(int, parsed.groups()[:6])
    if (index != kernel_index or calls <= 0 or errors != 0 or cells != 32
            or blocks != (32 + threads - 1) // threads or selected != threads
            or f'NetCustom_audit{network}' not in parsed.group(7)):
        raise ValueError('wrong/unexecuted shape or launch error')
    if stderr.splitlines().count(f'ADVANCE_SHAPE_DIAGNOSTIC_END matched_launches={calls} scope=not_formal_performance') != 1:
        raise ValueError('incomplete observer completion')
    result.update(metrics=','.join(aggregate[0]), shape=dict(index=index, calls=calls,
                  max_cells=cells, max_blocks=blocks, threads=selected), numerical_pass=True)
    return result


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    output, inputs_dir = ROOT / 'advance-shape-diagnostic-v1', Path(__file__).resolve().parent
    if output.exists() or os.environ.get('LD_PRELOAD') or os.environ.get('ARCH_SPARSE_ADVANCE_THREADS'):
        raise ValueError('new output without inherited probe required')
    parent = ROOT / 'batch-launch-focused-collection-v1.json'
    receipt = ROOT / 'batch-launch-focused-local-receipt-v1.json'
    p, r = json.loads(parent.read_text()), json.loads(receipt.read_text())
    if p.get('trajectory_matrix_pass') is not True or p['raw'] != r['raw']:
        raise ValueError('completed nuclear focused matrix and local backup required')
    focused = ROOT / 'batch-launch-focused-v1'
    qualified = json.loads((focused / 'qualification.json').read_text())
    if qualified.get('identities_verified_after') is not True or qualified.get('trajectory_matrix_pass') is not True:
        raise ValueError('focused identity/numerical qualification missing')
    for path, digest in qualified['inputs'].items():
        if sha(path) != digest:
            raise ValueError('focused input changed: ' + path)
    original = json.loads((focused / 'record.json').read_text())
    executables = {}
    for artifact in original['artifacts']:
        n = int(artifact['network'][5:])
        for key in ('executable', 'factory'):
            if sha(artifact[key + '_path']) != artifact[key + '_sha256']:
                raise ValueError('frozen executable/factory changed')
        executables[n] = Path(artifact['executable_path'])
    if set(executables) != {150, 200}:
        raise ValueError('both actual factory executables required')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another application/build active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU not idle')
    probe = inputs_dir / 'advance_shape_probe.cpp'
    paths = [parent, receipt, focused / 'qualification.json', focused / 'record.json',
             probe, Path(__file__), *executables.values()]
    identities = {str(p): sha(p) for p in paths}
    output.mkdir()
    library = output / 'advance_shape_probe.so'
    record = dict(status='running', scope='same-binary-ABBA-shape-diagnostic-not-formal-performance',
                  inputs=identities, artifacts={}, commands=[], runs=[],
                  performance_qualified=False, application_qualified=False, release_qualified=False)

    def save():
        (output / 'record.json').write_text(json.dumps(record, indent=2) + '\n')

    def run(name, command, env=None, wall=1800):
        row = dict(name=name, command=command, cwd=str(ROOT / 'source'), timeout_seconds=wall,
                   environment={k: env[k] for k in ('LD_PRELOAD', 'ARCH_SPARSE_ADVANCE_THREADS')} if env else {})
        record['commands'].append(row)
        save()
        start = time.monotonic()
        try:
            with (output / (name + '.stdout')).open('w') as stdout, (output / (name + '.stderr')).open('w') as stderr:
                code = subprocess.run(command, cwd=ROOT / 'source', env=env, stdout=stdout,
                                      stderr=stderr, timeout=wall).returncode
        except subprocess.TimeoutExpired:
            row.update(returncode=None, timed_out=True, elapsed_seconds=time.monotonic() - start)
            save()
            raise
        row.update(returncode=code, timed_out=False, elapsed_seconds=time.monotonic() - start)
        save()
        if code != 0:
            raise RuntimeError(f'{name} failed: {code}')

    try:
        run('compile-probe', ['/usr/bin/time', '-v', '/usr/bin/g++-11', '-std=c++17', '-O2',
            '-fno-fast-math', '-ffp-contract=off', '-fPIC', '-shared',
            '-I/home/ubuntu/projects/.envs/arch/targets/x86_64-linux/include',
            str(probe), '-o', str(library), '-ldl', '-pthread'], wall=300)
        record['artifacts'][str(library)] = sha(library)
        save()
        for network in (150, 200):
            for method in METHODS:
                for sample, threads in enumerate(THREADS):
                    name = f'audit{network}-{method}-sample{sample}-threads{threads}'
                    env = dict(os.environ, LD_PRELOAD=str(library), ARCH_SPARSE_ADVANCE_THREADS=str(threads))
                    run(name, arguments(executables[network], method), env=env)
                    data = validate((output / (name + '.stdout')).read_text(),
                                    (output / (name + '.stderr')).read_text(), network, method, threads)
                    record['runs'].append(dict(name=name, network=network, method=method,
                                               threads=threads, sample=sample, **data))
                    save()
        for path, digest in {**identities, **record['artifacts']}.items():
            if sha(path) != digest:
                raise ValueError('diagnostic input/product identity changed')
        record.update(status='passed', identities_verified_after=True)
    except BaseException as error:
        record.update(status='failed', error=repr(error))
        raise
    finally:
        save()
    print('ADVANCE_SHAPE_ABBA_PASS runs=16 numerical_and_launch_contracts_only')


if __name__ == '__main__':
    main()
