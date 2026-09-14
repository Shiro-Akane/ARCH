"""Bounded, explicitly incomplete API-attribution diagnostic, never a pass gate."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
BASE = ROOT / 'build/p12-20260914'
OUT = BASE / 'factor-cache/pinned-be-api-prefix-v1'
EXE = OUT / 'arch_cuda_generated_sparse_burn_audit150'
OBSERVER = BASE / 'cuda-progress-observer-v1.so'
CONTRACT = BASE / 'factor-cache/pinned-status-candidate-v2/arch_cuda_cudss_sparse_solver'
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

OUT.mkdir()
original = json.loads((BASE / 'factor-cache/long-be-observer-v1/record.json').read_text())
link = next(r for r in original['commands'] if r['name'] == 'link-audit150')
command = list(link['command'])
baseline_provider = str(BASE / 'factor-cache/candidate-v2/libarch_cuda_sparse_provider.a')
provider = BASE / 'factor-cache/pinned-status-candidate-v2/libarch_cuda_sparse_provider.a'
assert command.count(baseline_provider) == 1
command[command.index(baseline_provider)] = str(provider)
command[command.index('-o') + 1] = str(EXE)
inputs = {}
for value in command:
    if value.endswith(('.o', '.a')):
        path = Path(value)
        if not path.is_absolute():
            path = Path(link['cwd']) / path
        inputs[str(path)] = sha(path)
with (OUT / 'link.log').open('w') as stream:
    subprocess.run(command, cwd=link['cwd'], stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=120)
record = dict(status='running', scope='API attribution only; bounded incomplete prefix, not numerical qualification or formal timing',
    experiment='Pinned eight-byte provider completion; identical frozen capacity-selectable harness and device factory objects',
    link_command=command, link_cwd=link['cwd'], link_input_sha256=inputs,
    overlap='separate full large application integration build may be active',
    duration=1e-9, steps=16, diagnostic_wall_limit_seconds=300,
    original_full_trajectory_timeout_seconds=1800,
    executable=dict(path=str(EXE), sha256=sha(EXE)),
    observer=dict(path=str(OBSERVER), sha256=sha(OBSERVER)),
    contract=dict(path=str(CONTRACT), sha256=sha(CONTRACT)),
    numerical_gate_passed=False, formal_timing=False)
def save():
    (OUT / 'record.json').write_text(json.dumps(record, indent=2) + '\n')
save()
env = dict(os.environ, LD_PRELOAD=str(OBSERVER), OMP_NUM_THREADS='8', OMP_DYNAMIC='FALSE',
           OMP_PLACES='cores', OMP_PROC_BIND='close')
try:
    with (OUT / 'observer-contract.log').open('w') as stream:
        check = subprocess.run([str(CONTRACT)], env=env, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
    assert check.returncode == 0 and sha(CONTRACT) == record['contract']['sha256']
    record['contract']['passed'] = True
    command = [str(EXE), '1e7', '3e9', '1e-9', '1e8', '1e-7', '16', '--ode', 'be_nr',
               '--storage-cells', '32', '33', '--pool-cells', '8', 'c12=0.5', 'o16=0.5']
    record['command'] = command
    record['start_utc'] = time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())
    save()
    start = time.monotonic()
    with (OUT / 'stdout.log').open('w') as stdout, (OUT / 'stderr.log').open('w') as stderr:
        try:
            run = subprocess.run(command, env=env, cwd=ROOT, stdout=stdout, stderr=stderr, timeout=300)
            record.update(returncode=run.returncode, timed_out=False)
        except subprocess.TimeoutExpired as error:
            record.update(returncode=None, timed_out=True, error=repr(error))
    record.update(elapsed_seconds=time.monotonic()-start, status='diagnostic_completed_not_qualification')
    assert sha(EXE) == record['executable']['sha256'] and sha(OBSERVER) == record['observer']['sha256']
    assert all(sha(Path(path)) == digest for path, digest in inputs.items())
    record['identity_verified_after'] = True
except BaseException as error:
    record.update(status='diagnostic_failed', error=repr(error))
    raise
finally:
    save()
print(json.dumps({k:v for k,v in record.items() if k not in ('command',)}, indent=2))
