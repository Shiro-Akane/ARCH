"""Run frozen nuclear harnesses with the isolated batch-launch provider.

No factory CUDA recompilation, physics changes, observers or performance claims.
Each profile requires the previous stage's completed, locally verified archive.
Run one profile at a time under the existing memory/pressure/GPU guard.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
HELPER_SHA = 'ac8c012e71754548f19cdcd6929eb07fba0dc5959344a8801d8be87f039386e7'
PROFILES = {
    'focused': dict(storage=(2, 3), pools=(2,), steps=4, duration=1e-10, wall=1800,
                    parent='batch-launch', parent_pass='contracts_pass'),
    'capacity': dict(storage=(32, 33), pools=(8, 32), steps=4, duration=1e-10, wall=7200,
                     parent='batch-launch-focused', parent_pass='trajectory_matrix_pass'),
    'long': dict(storage=(32, 33), pools=(8, 32), steps=16, duration=1e-9, wall=21600,
                 parent='batch-launch-capacity', parent_pass='trajectory_matrix_pass'),
}
METHODS = {'be_nr': '1', 'bd': '2', 'ros4': '3'}


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1048576), b''):
            h.update(chunk)
    return h.hexdigest()


def trajectory_args(profile, method, pool):
    p = PROFILES[profile]
    return ['1e7', '3e9', str(p['duration']), '1e8', '1e-7', str(p['steps']),
            '--ode', method, '--storage-cells', *map(str, p['storage']),
            '--pool-cells', str(pool), 'c12=0.5', 'o16=0.5']


def validate(profile, record, output, exit_code):
    p = PROFILES[profile]
    expected = {f'audit{n}-{method}-pool{pool}' for n in (150, 200)
                for method in METHODS for pool in p['pools']}
    if (record.get('steps') != p['steps'] or record.get('duration') != p['duration']
            or record.get('runtime_timeout_seconds') != p['wall'] or record.get('observer')):
        raise ValueError('frozen physical/wall profile without observers required')
    commands = {row['name']: row for row in record['commands']}
    if len(commands) != len(record['commands']):
        raise ValueError('duplicate commands')
    completed = set()
    for run in record['runs']:
        name = run['name']
        if name not in expected or name in completed or run.get('passed') is not True:
            raise ValueError('unexpected/duplicate passed harness')
        network, method, pool_text = name.split('-')
        pool, method_id = int(pool_text[4:]), METHODS[method]
        command = commands[name]
        if (command.get('returncode') != 0 or command.get('timed_out') is not False
                or command['command'][1:] != trajectory_args(profile, method, pool)):
            raise ValueError('passed harness without matching successful execution')
        lines = (output / (name + '.stdout')).read_text().splitlines()
        controls = [line.split(',') for line in lines if line.startswith('controls,')]
        if (len(controls) != 1 or len(controls[0]) != 11
                or controls[0][1:3] != ['custom:' + network, str(int(network[5:]) + 1)]
                or [float(v) for v in controls[0][3:8]] != [1e7, 3e9, p['duration'], 1e8, 1e-7]
                or controls[0][8:] != [str(p['steps']), 'selected_ode', method_id]
                or [line for line in lines if line.startswith('storage_controls,')]
                != ([] if (p['storage'], pool) == ((2, 3), 2) else
                    ['storage_controls,' + ','.join(map(str, (*p['storage'], pool)))])
                or lines.count('GENERATED_SPARSE_BURN_PARITY_PASS') != 1):
            raise ValueError('actual trajectory controls/completion differ')
        metrics = [line for line in lines if line.startswith(('cpu_step,', 'gpu_step,', 'metrics,'))]
        if run['metrics'] != metrics:
            raise ValueError('transcript/record mismatch')
        for kind in ('cpu_step', 'gpu_step'):
            rows = [line.split(',') for line in metrics if line.startswith(kind + ',')]
            keys = [(row[1], int(row[2]), int(row[3])) for row in rows]
            if keys != [(method_id, storage, step) for storage in p['storage'] for step in range(p['steps'])]:
                raise ValueError('missing/reordered/wrong-method physical steps')
        aggregate = [line.split(',') for line in metrics if line.startswith('metrics,')]
        if (len(aggregate) != 1 or len(aggregate[0]) != 9 or aggregate[0][1] != method_id
                or aggregate[0][7] != str(pool)):
            raise ValueError('missing aggregate numerical result')
        for value, bound in zip(aggregate[0][4:6], (2e-10, 2e-8)):
            if not math.isfinite(float(value)) or not 0 <= float(value) <= bound:
                raise ValueError('original field/limiter budget exceeded')
        if not math.isfinite(float(aggregate[0][6])) or float(aggregate[0][6]) <= 0:
            raise ValueError('no measured composition evolution')
        completed.add(name)
    if exit_code == 0 and (record['status'] != 'passed' or completed != expected
                          or any(row.get('returncode') != 0 for row in commands.values())):
        raise ValueError('incomplete/failed matrix cannot pass')
    if exit_code != 0 and record['status'] != 'failed':
        raise ValueError('failure state not retained')
    return dict(completed_harnesses=sorted(completed), planned_harnesses=sorted(expected),
                trajectory_matrix_pass=(exit_code == 0 and completed == expected),
                performance_qualified=False, application_qualified=False, release_qualified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    args = parser.parse_args()
    p = PROFILES[args.profile]
    output = ROOT / ('batch-launch-' + args.profile + '-v1')
    if output.exists() or output.is_symlink() or os.environ.get('LD_PRELOAD') or os.environ.get('ARCH_NATIVE_WAVE_WORK_OBSERVER'):
        raise ValueError('new output and no observers required')
    parent = ROOT / (p['parent'] + '-collection-v1.json')
    local = ROOT / (p['parent'] + '-local-receipt-v1.json')
    collection, backup = json.loads(parent.read_text()), json.loads(local.read_text())
    if collection.get(p['parent_pass']) is not True or backup['raw'] != collection['raw']:
        raise ValueError('prior completed matrix with verified local backup required')
    for comm in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', comm], capture_output=True).returncode != 1:
            raise ValueError('another application/build is active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU is not idle')
    build, source = ROOT / 'factory-release', ROOT / 'source'
    helper = ROOT / 'input/run_sparse_capacity_v1.py'
    contract_path = ROOT / 'batch-launch-contract-v1/record.json'
    contract = json.loads(contract_path.read_text())
    provider = ROOT / 'batch-launch-contract-v1/libarch_cuda_sparse_provider.a'
    if (sha(helper) != HELPER_SHA or contract['status'] != 'kernel-and-provider-contracts-passed'
            or contract.get('identities_verified_after') is not True):
        raise ValueError('frozen helper and completed provider contracts required')
    for path, digest in {**contract['inputs'], **contract['artifacts']}.items():
        if sha(path) != digest:
            raise ValueError('contract source/product changed: ' + path)
    if str(provider) not in contract['artifacts']:
        raise ValueError('tested private provider missing')
    inputs = {str(path): sha(path) for path in
              (helper, provider, source / 'tests/cuda/test_generated_sparse_burn.cpp',
               build / 'compile_commands.json', parent, local, contract_path, Path(__file__))}
    for network in ('audit150', 'audit200'):
        obj = build / (f'CMakeFiles/arch_cuda_generated_sparse_burn_{network}.dir/tests/cuda/test_generated_sparse_burn_factory.cu.o')
        inputs[str(obj)] = sha(obj)
    command = [sys.executable, str(helper), '--build-dir', str(build),
               '--source', str(source / 'tests/cuda/test_generated_sparse_burn.cpp'),
               '--provider', str(provider), '--output-dir', str(output),
               '--methods', *METHODS, '--pools', *map(str, p['pools']),
               '--storage', *map(str, p['storage']), '--steps', str(p['steps']),
               '--duration', str(p['duration']), '--run-timeout', str(p['wall'])]
    result = subprocess.run(command, cwd=source)
    record = json.loads((output / 'record.json').read_text())
    qualification = dict(profile=args.profile, command=command, inputs=inputs,
                         worker_exit_code=result.returncode, identities_verified_after=False)
    try:
        qualification.update(validate(args.profile, record, output, result.returncode))
        for path, expected in inputs.items():
            if sha(path) != expected:
                raise ValueError('trajectory input changed: ' + path)
        if (record['provider_sha256'] != inputs[str(provider)]
                or record['source_sha256'] != inputs[str(source / 'tests/cuda/test_generated_sparse_burn.cpp')]
                or record['compile_commands_sha256'] != inputs[str(build / 'compile_commands.json')]
                or record['recipe_sha256'] != HELPER_SHA):
            raise ValueError('unexpected provider/helper actually executed')
        qualification['identities_verified_after'] = True
    except BaseException as error:
        qualification.update(trajectory_matrix_pass=False, error=repr(error))
        raise
    finally:
        with (output / 'qualification.json').open('x') as stream:
            json.dump(qualification, stream, indent=2)
            stream.write('\n')
    print(json.dumps({k: v for k, v in qualification.items() if k not in ('inputs', 'command')}))
    sys.exit(result.returncode)


if __name__ == '__main__':
    main()
