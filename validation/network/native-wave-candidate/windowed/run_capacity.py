"""Twelve capacity regressions, only after focused evidence is doubly preserved."""
import json
import os
from pathlib import Path
import subprocess
import time

from capacity_protocol import MATRIX, UNQUALIFIED, focused_helpers, parent_gate, parse_owner, validate_transcript

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')


def main():
    payload = Path(__file__).resolve().parent
    output, factory = ROOT / 'window-capacity-v1', ROOT / 'window-factory-v1'
    if payload != ROOT / 'window-capacity-input-v1' or output.exists() or output.is_symlink():
        raise ValueError('frozen capacity payload and new output required')
    if any(os.environ.get(key) for key in ('LD_PRELOAD', 'ARCH_SPARSE_ADVANCE_THREADS',
            'ARCH_NATIVE_WAVE_WORK_OBSERVER', 'ARCH_NATIVE_WINDOW_CELLS')):
        raise ValueError('inherited runtime override forbidden')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another application or build is active')
    if subprocess.check_output(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'], text=True).strip():
        raise ValueError('GPU must be idle before capacity regression')
    focused = focused_helpers()
    parents = parent_gate(ROOT)
    built = focused.verify_factory_identity(factory)
    original = focused.load_validator()
    p = original.PROFILES['capacity']
    paths = [*parents, factory / 'record.json', *[path for path in payload.iterdir() if path.is_file()]]
    paths += [factory / f'arch_cuda_generated_sparse_burn_audit{n}' for n in (150, 200)]
    if any(path.is_symlink() for path in paths):
        raise ValueError('unsafe capacity input')
    record = dict(status='running', profile='capacity', selected_window=32,
        steps=p['steps'], duration=p['duration'], runtime_timeout_seconds=p['wall'],
        commands=[], runs=[], owners={}, inputs={str(path): focused.sha(path) for path in paths},
        original_harness_sha256=focused.HARNESS_SHA, original_validator_sha256=focused.VALIDATOR_SHA,
        trajectory_matrix_pass=False, **{key: False for key in UNQUALIFIED})
    output.mkdir()

    def save():
        (output / 'record.json').write_text(json.dumps(record, indent=2) + '\n')

    try:
        for network, method, pool in MATRIX:
            executable = factory / f'arch_cuda_generated_sparse_burn_audit{network}'
            if str(executable) not in built['artifacts']:
                raise ValueError('capacity executable is not a verified fresh factory')
            name = f'audit{network}-{method}-pool{pool}'
            command = [str(executable), *original.trajectory_args('capacity', method, pool)]
            row = dict(name=name, command=command, cwd=str(factory),
                       environment={'ARCH_NATIVE_WINDOW_CELLS': '32'},
                       timeout_seconds=p['wall'], status='running', timed_out=False)
            record['commands'].append(row)
            save()
            print('WINDOW_CAPACITY_STARTED ' + name, flush=True)
            start = time.monotonic()
            try:
                with (output / (name + '.stdout')).open('xb') as out, (output / (name + '.stderr')).open('xb') as err:
                    result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=factory,
                        env=dict(os.environ, **row['environment']), stdout=out, stderr=err, timeout=p['wall'])
                row.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
                if result.returncode:
                    raise RuntimeError('first capacity failure retained: ' + name)
            except subprocess.TimeoutExpired:
                # The existing outer memory guard subreaps and terminates this
                # invocation's descendants, including an orphaned /usr/bin/time child.
                row.update(status='failed', returncode=None, timed_out=True)
                raise
            finally:
                row['elapsed_seconds'] = time.monotonic() - start
                save()
            text = (output / (name + '.stdout')).read_text()
            record['owners'][name] = parse_owner(text, pool)
            record['runs'].append(dict(name=name, passed=True, metrics=[line for line in text.splitlines()
                if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]))
            save()
            print('WINDOW_CAPACITY_FINISHED ' + name, flush=True)
        record['status'] = 'passed'
        qualification = validate_transcript(record, output, factory, 0)
        focused.verify_factory_identity(factory)
        for path, expected in record['inputs'].items():
            if focused.sha(path) != expected:
                raise ValueError('capacity input changed: ' + path)
        record.update(**qualification, identities_verified_after=True)
    except BaseException as error:
        record.update(status='failed', trajectory_matrix_pass=False, error=repr(error))
        raise
    finally:
        save()
    print('WINDOW_CAPACITY_NUMERICAL_PASS_PAGED_ODE_APPLICATION_PERFORMANCE_UNQUALIFIED', flush=True)


if __name__ == '__main__':
    main()
