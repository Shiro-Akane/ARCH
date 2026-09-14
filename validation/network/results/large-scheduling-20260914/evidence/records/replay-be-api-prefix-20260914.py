"""Bounded, explicitly incomplete API-attribution diagnostic, never a pass gate."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
BASE = ROOT / 'build/p12-20260914'
OUT = BASE / 'factor-cache/be-api-prefix-v1'
EXE = BASE / 'factor-cache/long-be-observer-v1/arch_cuda_generated_sparse_burn_audit150'
OBSERVER = BASE / 'cuda-progress-observer-v1.so'
CONTRACT = BASE / 'factor-cache/candidate-v2/arch_cuda_cudss_sparse_solver'
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

OUT.mkdir()
record = dict(status='running', scope='API attribution only; bounded incomplete prefix, not numerical qualification or formal timing',
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
    record['identity_verified_after'] = True
except BaseException as error:
    record.update(status='diagnostic_failed', error=repr(error))
    raise
finally:
    save()
print(json.dumps({k:v for k,v in record.items() if k not in ('command',)}, indent=2))
