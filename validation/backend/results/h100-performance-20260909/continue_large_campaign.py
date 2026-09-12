"""One-shot continuation of the already running server build/validation jobs.

Not a recurring service. Deploys only the four reviewed test-harness files after
the pristine focused job exits, then runs bounded validation. Production source,
generated networks, numerical budgets and Git refs are never modified.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

BASE = Path('/home/ubuntu/projects/ARCH-perf-20260909')
INTEGRATION = Path('/home/ubuntu/projects/ARCH-large-integration-20260909')
OUT = BASE / 'build/large-network-20260909'
HELPERS = BASE / 'build/performance-20260909'
PYTHON = BASE / 'build/network-python-20260909/bin/python'
COMMIT = '0266d96f20b184d4b17ebc6a066ac3b9021f1642'
STAGED = OUT / 'test-extension'
FILES = ('cmake/tests/HostTests.cmake', 'tests/cuda/test_generated_sparse_burn.cpp',
         'validation/network/run_sparse_validation.py', 'validation/network/test_sparse_validation.py')
ENV = dict(os.environ, PATH='/home/ubuntu/projects/.envs/arch/bin:' + os.environ['PATH'],
           OMP_NUM_THREADS='8', OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
STATE = dict(status='starting', started_utc=datetime.now(timezone.utc).isoformat(),
             source_commit=COMMIT, commands=[], focused={},
             safety_gate='blocked: vGPU debugging features disabled', release_qualified=False)


def save():
    STATE['updated_utc'] = datetime.now(timezone.utc).isoformat()
    path = OUT / 'campaign-status.json'
    temporary = path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(STATE, indent=2) + '\n')
    temporary.replace(path)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def process_identity(pid):
    try:
        # starttime is field 22; comm in parentheses can contain spaces.
        fields = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        return fields[19] if fields[0] != 'Z' else None
    except FileNotFoundError:
        return None


def wait_existing(pid, identity, phase):
    STATE['status'] = phase
    save()
    started = time.monotonic()
    while identity is not None and process_identity(pid) == identity:
        if time.monotonic() - started > 12 * 3600:
            raise TimeoutError(f'{phase}: build still active after 12 hours; not terminated')
        time.sleep(10)


def execute(name, command, cwd=BASE):
    record = dict(name=name, command=[str(v) for v in command], cwd=str(cwd),
                  started_utc=datetime.now(timezone.utc).isoformat(), status='running')
    STATE['commands'].append(record)
    STATE['status'] = name
    save()
    with (OUT / f'continuation-{name}.log').open('w') as log:
        result = subprocess.run(record['command'], cwd=cwd, env=ENV, stdout=log, stderr=subprocess.STDOUT)
    record.update(status='passed' if result.returncode == 0 else 'failed',
                  returncode=result.returncode, finished_utc=datetime.now(timezone.utc).isoformat())
    save()
    return result.returncode == 0


def guarded(name, command, cwd=BASE, memory=16384):
    return execute(name, [PYTHON, cwd / 'tools/run_memory_guarded.py',
        '--min-available-mib', memory, '--max-swap-growth-mib', 0, '--pressure-guard',
        '--gpu-memory-device', 0, '--log', OUT / f'guard-{name}.log', '--', *command], cwd)


def main():
    focused_pid, integration_pid = map(int, sys.argv[1:])
    focused_identity = process_identity(focused_pid)
    integration_identity = process_identity(integration_pid)
    STATE['build_processes'] = dict(focused=[focused_pid, focused_identity],
                                   integration=[integration_pid, integration_identity])
    observed = {}
    for relative in FILES:
        pristine = subprocess.check_output(['git', 'show', f'{COMMIT}:{relative}'], cwd=BASE)
        old = hashlib.sha256(pristine).hexdigest()
        if digest(BASE / relative) != old:
            raise RuntimeError(f'source already differs from pristine harness: {relative}')
        observed[relative] = dict(before=old, staged=digest(STAGED / relative))
    STATE['test_patch'] = observed
    save()
    wait_existing(focused_pid, focused_identity, 'waiting-for-pristine-focused-matrix')
    eligible = []
    for network in ('audit150', 'audit200'):
        evidence_path = OUT / f'results/{network}-fourstep/evidence.json'
        evidence = json.loads(evidence_path.read_text()) if evidence_path.exists() else {}
        passed = evidence.get('focused_gate_pass') is True and evidence.get('identity_verified_after_run') is True
        STATE['focused'][network] = dict(passed=passed, evidence=str(evidence_path))
        if passed:
            eligible.append(network)
    save()
    if eligible:
        for relative, hashes in observed.items():
            if digest(BASE / relative) != hashes['before'] or digest(STAGED / relative) != hashes['staged']:
                raise RuntimeError(f'harness changed while builds ran; refusing overwrite: {relative}')
        for relative in FILES:
            subprocess.run(['cp', '--', str(STAGED / relative), str(BASE / relative)], check=True)
        if not execute('sparse-parser-contracts', [PYTHON, '-B', 'validation/network/test_sparse_validation.py']):
            raise RuntimeError('sparse parser contracts failed')
        if not execute('validator-contracts', [PYTHON, '-B', '-m', 'unittest', 'discover',
                                             '-s', 'tests/tooling', '-p', 'test_*validation*.py']):
            raise RuntimeError('validation tooling contracts failed')
        targets = [f'arch_cuda_generated_sparse_burn_{n}' for n in eligible]
        if not guarded('capacity-harness-build', ['cmake', '--build', OUT / 'release',
                                                '--parallel', 2, '--target', *targets]):
            raise RuntimeError('capacity harness build failed')
        execute('extended-matrix', ['bash', HELPERS / 'run_extended_network_validation.sh'])
    wait_existing(integration_pid, integration_identity, 'waiting-for-full-application-build')
    full_output = INTEGRATION / 'build/large-integration-20260909'
    build = full_output / 'release'
    if not (full_output / 'artifact-sha256.txt').exists() or not (build / 'bin/ARCH').exists():
        raise RuntimeError('complete pristine ARCH build did not finish successfully; inspect full build log')
    if subprocess.check_output(['git', 'status', '--porcelain'], cwd=INTEGRATION).strip():
        raise RuntimeError('integration worktree changed; refusing application qualification')
    if eligible:
        if not execute('application-comparator-configure', ['cmake', '-S', INTEGRATION, '-B', build,
                '-DBUILD_TESTING=ON', f'-DPython3_EXECUTABLE={PYTHON}'], INTEGRATION):
            raise RuntimeError('application comparator configuration failed')
        if not guarded('application-comparator-build', ['cmake', '--build', build, '--parallel', 6,
                '--target', 'ARCH', 'arch_cuda_single_level_validation'], INTEGRATION, 32768):
            raise RuntimeError('application/comparator build failed')
        manifest = full_output / 'large-runtime-cases.json'
        if not execute('application-manifest', [PYTHON, HELPERS / 'make_large_runtime_manifest.py',
                '--source-root', INTEGRATION, '--output', manifest], INTEGRATION):
            raise RuntimeError('application manifest generation failed')
        for network in eligible:
            for ode in ('be_nr', 'bd', 'ros4'):
                case = f'generated_{network}_{ode}_helm'
                guarded(case, [PYTHON, INTEGRATION / 'tools/validate_backend_results.py',
                    '--manifest', manifest, '--case', case, '--source-root', INTEGRATION,
                    '--build-dir', build, '--arch', build / 'bin/ARCH',
                    '--checkpoint-validator', build / 'arch_cuda_single_level_validation',
                    '--output-root', full_output / 'results' / case], INTEGRATION, 32768)
    STATE['status'] = ('completed-bounded-campaign' if len(eligible) == 2 and
        all(c['status'] == 'passed' for c in STATE['commands']) else 'completed-with-failed-or-missing-gates')
    STATE['finished_utc'] = datetime.now(timezone.utc).isoformat()
    save()


try:
    main()
except Exception as error:
    STATE.update(status='failed', error=str(error), finished_utc=datetime.now(timezone.utc).isoformat())
    save()
    raise
