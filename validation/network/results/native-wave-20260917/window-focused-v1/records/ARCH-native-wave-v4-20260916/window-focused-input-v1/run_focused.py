"""Original six real-network focused trajectories on fresh window factories.

No compilation, observers, new science settings or performance qualification.
The 2->3-cell gate does not qualify multi-page ODE execution or large windows.
"""
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
VALIDATOR_SHA = '1e0806d57fc539c72afe3ebd88e3ebd9b1181e8386c48d45442ff168c09173f6'
HARNESS_SHA = '551d021ac6ce26378dff6823e135a499cc9bbdc76276a7612a45ef21b5cab205'
METHODS = ('be_nr', 'bd', 'ros4')


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            value.update(block)
    return value.hexdigest()


def load_validator():
    here = Path(__file__).resolve().parent
    path = here/'trajectory_validation.py'
    if not path.is_file():
        path = here.parent/'batched-kernels/run_trajectories.py'
    if sha(path) != VALIDATOR_SHA:
        raise ValueError('original numerical transcript validator changed')
    spec = importlib.util.spec_from_file_location('original_focused_numerical_validation', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_owner(text):
    lines = [line for line in text.splitlines() if line.startswith('WINDOW_OWNER')]
    if len(lines) != 1:
        raise ValueError('one actual window owner declaration required')
    match = re.fullmatch(r'WINDOW_OWNER selected_window=(\d+) actual_capacity=(\d+) native_capacity=(\d+) '
                         r'workspace_bytes=(\d+) workspace_budget=(\d+) device_warp=(\d+)', lines[0])
    if not match:
        raise ValueError('unexpected window owner declaration')
    selected, capacity, native, size, budget, warp = map(int, match.groups())
    aggregate = [line.split(',') for line in text.splitlines() if line.startswith('metrics,')]
    if (len(aggregate) != 1 or len(aggregate[0]) != 9 or selected != 32 or capacity != 2 or native != 2
            or budget != 32*1024*1024 or warp != 32 or not 0 < size <= budget
            or aggregate[0][7] != '2' or size != 2*int(aggregate[0][8])):
        raise ValueError('focused owner does not match real bounded workspace')
    return dict(selected_window=selected, actual_capacity=capacity, native_capacity=native,
                workspace_bytes=size, workspace_budget=budget, device_warp=warp)


def factory_gate(root):
    collection_path = root/'window-factory-collection-v2.json'
    receipt_path = root/'window-factory-local-receipt-v2.json'
    collection, receipt = json.loads(collection_path.read_text()), json.loads(receipt_path.read_text())
    if (collection.get('factory_build_pass') is not True or collection.get('worker_exit_code') != 0
            or collection.get('fresh_factories') != 2
            or receipt.get('status') != 'both_archives_and_all_members_byte_verified'
            or any(collection.get(name) is not False for name in
                   ('nuclear_qualified', 'performance_qualified', 'release_qualified'))):
        raise ValueError('completed fresh build and local byte-verified archive required')
    for kind in ('raw', 'compact'):
        item = collection[kind]
        path = Path(item['path'])
        if (receipt.get(kind) != item or not path.is_file() or path.is_symlink()
                or path.stat().st_size != item['bytes'] or sha(path) != item['sha256']):
            raise ValueError('factory predecessor archive identity changed')
    return collection_path, receipt_path


def verify_factory_identity(factory):
    path = factory/'record.json'
    record = json.loads(path.read_text())
    if (record.get('status') != 'factory-built-not-runtime-qualified'
            or record.get('identities_verified_after') is not True
            or len(record.get('artifacts', {})) != 7):
        raise ValueError('two fresh factories and wrapper are required')
    for group in (record['inputs'], record['artifacts'], *record['dependencies'].values()):
        for name, expected in group.items():
            if sha(name) != expected:
                raise ValueError('frozen factory input/product changed: '+name)
    private = factory/'source'
    files = list(private.rglob('*'))
    if any(item.is_symlink() for item in files):
        raise ValueError('private factory source cannot contain symlinks')
    if {item.relative_to(private).as_posix(): sha(item) for item in files if item.is_file()} != record['source_copy']['private']:
        raise ValueError('private factory inventory changed')
    if sha(private/'tests/cuda/test_generated_sparse_burn.cpp') != HARNESS_SHA:
        raise ValueError('original physical harness changed')
    return record


def validate_transcript(record, output, factory, exit_code):
    """Re-audit exact execution and original numerical bounds after the worker."""
    original = load_validator()
    if (record.get('profile') != 'focused'
            or record.get('original_harness_sha256') != HARNESS_SHA
            or record.get('original_validator_sha256') != VALIDATOR_SHA
            or any(record.get(key) is not False for key in
                   ('paged_ode_qualified', 'application_qualified', 'performance_qualified', 'release_qualified'))):
        raise ValueError('focused-only original harness and validator required')
    expected = [f'audit{n}-{method}-pool2' for n in (150, 200) for method in METHODS]
    commands = record.get('commands', [])
    names = [row['name'] for row in commands]
    if names != expected[:len(names)] or (exit_code == 0 and names != expected):
        raise ValueError('missing, duplicated or reordered focused commands')
    for row in commands:
        network, method, _ = row['name'].split('-')
        command = [str(factory/('arch_cuda_generated_sparse_burn_'+network)),
                   *original.trajectory_args('focused', method, 2)]
        if (row.get('command') != command or row.get('cwd') != str(factory)
                or row.get('environment') != {'ARCH_NATIVE_WINDOW_CELLS': '32'}
                or row.get('timeout_seconds') != 1800):
            raise ValueError('unexpected factory, controls or execution environment')
        if row.get('status') == 'passed' and (row.get('returncode') != 0
                or row.get('timed_out') is not False
                or not isinstance(row.get('elapsed_seconds'), (int, float))
                or not math.isfinite(row['elapsed_seconds']) or row['elapsed_seconds'] < 0):
            raise ValueError('inconsistent successful command')
    result = original.validate('focused', record, output, exit_code)
    owners = {name: parse_owner((output/(name+'.stdout')).read_text())
              for name in result['completed_harnesses']}
    if record.get('owners') != owners:
        raise ValueError('window owner record differs from real transcripts')
    return dict(**result, paged_ode_qualified=False, owners_verified=True)


def main():
    payload, output, factory = Path(__file__).resolve().parent, ROOT/'window-focused-v1', ROOT/'window-factory-v1'
    if payload != ROOT/'window-focused-input-v1' or output.exists() or output.is_symlink():
        raise ValueError('expected frozen payload and new focused output required')
    if any(os.environ.get(name) for name in ('LD_PRELOAD', 'ARCH_SPARSE_ADVANCE_THREADS',
        'ARCH_NATIVE_WAVE_WORK_OBSERVER', 'ARCH_NATIVE_WINDOW_CELLS')):
        raise ValueError('no inherited experimental runtime overrides permitted')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another application or build is active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU must be idle before the focused matrix')
    parents = factory_gate(ROOT)
    built = verify_factory_identity(factory)
    original = load_validator()
    paths = [*parents, factory/'record.json', *[p for p in payload.iterdir() if p.is_file()]]
    paths += [factory/f'arch_cuda_generated_sparse_burn_audit{n}' for n in (150, 200)]
    if any(path.is_symlink() for path in paths):
        raise ValueError('unsafe focused input')
    record = dict(status='running', profile='focused', duration=1e-10, steps=4, runtime_timeout_seconds=1800,
                  commands=[], runs=[], owners={}, inputs={str(path): sha(path) for path in paths},
                  original_harness_sha256=HARNESS_SHA, original_validator_sha256=VALIDATOR_SHA,
                  trajectory_matrix_pass=False, paged_ode_qualified=False, application_qualified=False,
                  performance_qualified=False, release_qualified=False)
    output.mkdir()

    def save():
        (output/'record.json').write_text(json.dumps(record, indent=2)+'\n')

    try:
        for network in (150, 200):
            exe = factory/f'arch_cuda_generated_sparse_burn_audit{network}'
            if str(exe) not in built['artifacts']:
                raise ValueError('runtime is not one of the newly built factories')
            for method in METHODS:
                name = f'audit{network}-{method}-pool2'
                command = [str(exe), *original.trajectory_args('focused', method, 2)]
                row = dict(name=name, command=command, cwd=str(factory), environment={'ARCH_NATIVE_WINDOW_CELLS': '32'},
                           timeout_seconds=1800, status='running', timed_out=False)
                record['commands'].append(row)
                save()
                print('WINDOW_FOCUSED_STARTED '+name, flush=True)
                start = time.monotonic()
                try:
                    with (output/(name+'.stdout')).open('wb') as stdout, (output/(name+'.stderr')).open('wb') as stderr:
                        result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=factory,
                            env=dict(os.environ, **row['environment']), stdout=stdout, stderr=stderr, timeout=1800)
                    row.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
                    if result.returncode:
                        raise RuntimeError('first real window trajectory failure retained: '+name)
                except subprocess.TimeoutExpired:
                    row.update(status='failed', returncode=None, timed_out=True)
                    raise
                finally:
                    row['elapsed_seconds'] = time.monotonic()-start
                    save()
                text = (output/(name+'.stdout')).read_text()
                record['owners'][name] = parse_owner(text)
                metrics = [line for line in text.splitlines() if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]
                record['runs'].append(dict(name=name, passed=True, metrics=metrics))
                save()
                print('WINDOW_FOCUSED_FINISHED '+name, flush=True)
        record['status'] = 'passed'
        qualification = validate_transcript(record, output, factory, 0)
        verify_factory_identity(factory)
        for path, expected in record['inputs'].items():
            if sha(path) != expected:
                raise ValueError('focused runtime input changed: '+path)
        record.update(**qualification, identities_verified_after=True)
    except BaseException as error:
        record.update(status='failed', trajectory_matrix_pass=False, error=repr(error))
        raise
    finally:
        save()
    print('WINDOW_FOCUSED_NUMERICAL_PASS_PAGED_ODE_APPLICATION_PERFORMANCE_UNQUALIFIED', flush=True)


if __name__ == '__main__':
    main()
