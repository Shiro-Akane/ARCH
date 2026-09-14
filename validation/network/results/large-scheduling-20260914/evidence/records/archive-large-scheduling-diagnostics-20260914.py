"""Preserve failed long BE, API-attribution prefixes and an unadopted experiment."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
BASE = ROOT / 'build/p12-20260914'
OUT = BASE / 'factor-cache/scheduling-diagnostics-archive-v1'
COMPACT = OUT / 'compact'
RAW = ROOT / 'build/large-scheduling-diagnostics-v1.tar.zst'
PACKED = ROOT / 'build/large-scheduling-diagnostics-compact-v1.tar.zst'
assert not OUT.exists() and not RAW.exists() and not PACKED.exists()
def read(path):
    return json.loads(path.read_text())
def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        while chunk := f.read(1024*1024):
            h.update(chunk)
    return h.hexdigest()
old = read(BASE / 'factor-cache/long-be-observer-v1/record.json')
assert old['status'] == 'failed' and 'TimeoutExpired' in old['error']
for phase in ('be-api-prefix-v1', 'pinned-be-api-prefix-v1'):
    r = read(BASE / 'factor-cache' / phase / 'record.json')
    assert r['status'] == 'diagnostic_completed_not_qualification'
    assert r['identity_verified_after'] and r['contract']['passed']
    assert not r['numerical_gate_passed'] and not r['formal_timing']
candidate = read(BASE / 'factor-cache/pinned-status-candidate-v2/record.json')
assert candidate['status'] == 'focused-passed' and not candidate['large_network_numerical_gates_run']
paths = [BASE / 'factor-cache' / name for name in (
    'long-be-observer-v1', 'be-api-prefix-v1', 'pinned-be-api-prefix-v1',
    'pinned-status-candidate-v2', 'pinned-status-overlay-v1')]
paths += list((BASE / 'factor-cache').glob('long-be-observer-v1-*.log'))
for pattern in ('native-progress-*', 'native-progress-observer-*', 'cuda-progress-*',
                'be-api-prefix-*.log', 'pinned-be-api-prefix-*.log', 'pinned-status-contract-*.log'):
    paths += list(BASE.glob(pattern))
paths += [BASE / name for name in (
    'probe-cudss-progress-20260914.cpp', 'probe-cuda-progress-20260914.cpp',
    'replay-large-be-observer-20260914.sh', 'replay-be-api-prefix-20260914.py',
    'replay-pinned-be-prefix-20260914.py', 'archive-large-scheduling-diagnostics-20260914.py')]
files = sorted({f for path in paths for f in (path.rglob('*') if path.is_dir() else [path]) if f.is_file()})
assert all(f.resolve().is_relative_to(ROOT) and not f.is_symlink() for f in files)
COMPACT.mkdir(parents=True)
manifest = {str(f.relative_to(ROOT)): sha(f) for f in files}
for f in files:
    with f.open('rb') as stream:
        binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
    if binary or f.suffix in ('.o', '.a', '.so', '.tar', '.zst'):
        continue
    target = COMPACT / 'records' / f.relative_to(BASE)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(f, target)
(COMPACT / 'raw-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
list_file = OUT / 'raw-paths.txt'
list_file.write_text('\n'.join(manifest) + '\n')
subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(RAW), '-C', str(ROOT),
                '-T', str(list_file), str(COMPACT.relative_to(ROOT))], check=True)
assert all(sha(ROOT / name) == digest for name, digest in manifest.items())
raw = dict(path=str(RAW), bytes=RAW.stat().st_size, sha256=sha(RAW), files=len(manifest),
           full_trajectory_status='failed_timeout', prefixes='incomplete_diagnostics_not_qualification',
           pinned_experiment_adopted=False)
(COMPACT / 'raw-archive.json').write_text(json.dumps(raw, indent=2) + '\n')
subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(PACKED), '-C', str(OUT), 'compact'], check=True)
print(json.dumps(dict(raw=raw, compact=dict(path=str(PACKED),bytes=PACKED.stat().st_size,sha256=sha(PACKED))), indent=2))
print('LARGE_SCHEDULING_DIAGNOSTICS_ARCHIVE_PASS_NOT_SCIENCE_PASS')
