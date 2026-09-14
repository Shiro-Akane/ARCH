"""Remove only redundant own HDF copies after validating the exact raw archive.

The complete archive has a separately reverified Windows copy. This script
does not remove logs, source, binaries, directories, or unarchived data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914').resolve(strict=True)
PREFIX = 'build/fix-20260914/validation-v1/curved/'
SCOPE = (ROOT / PREFIX).resolve(strict=True)
ARCHIVE = ROOT / 'build/fixed-science-v1.tar.zst'
LOCAL_PATH = 'C:/tmp/ARCH-perf-20260909/build/fixed-science-v1.tar.zst'

def digest(stream):
    h = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        h.update(chunk)
    return h.hexdigest()

def file_hash(path):
    with path.open('rb') as stream:
        return digest(stream)

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--local-verified-sha256', required=True)
p.add_argument('--local-verified-bytes', required=True, type=int)
p.add_argument('--apply', action='store_true')
args = p.parse_args()
SHA = args.local_verified_sha256
LOCAL_BYTES = args.local_verified_bytes
assert len(SHA) == 64 and all(c in '0123456789abcdef' for c in SHA) and LOCAL_BYTES > 0
assert SCOPE.is_relative_to(ROOT / 'build/fix-20260914/validation-v1')
assert ARCHIVE.stat().st_size == LOCAL_BYTES and file_hash(ARCHIVE) == SHA
record = json.loads((ROOT / 'build/fix-20260914/coupled-scale-v1/evidence.json').read_text())
assert record['status'] == 'passed' and record['identity_verified']
assert len(record['cases']) == 18 and len(record['comparisons']) == 18
assert json.loads((ROOT / PREFIX / 'record.json').read_text())['status'] == 'passed'
targets = {str(path.relative_to(ROOT)).replace('\\', '/'): path
           for path in SCOPE.rglob('*.h5') if path.is_file()}
assert targets and all(not path.is_symlink() and path.resolve().is_relative_to(SCOPE)
                       for path in targets.values())
manifest = {}
for line in (ROOT / 'build/fix-20260914/science-archive-v1/compact/raw-files.sha256').read_text().splitlines():
    checksum, name = line.split('  ', 1)
    manifest[name] = checksum
assert set(targets) <= manifest.keys()
verified = {}
with subprocess.Popen(['zstd', '-dc', str(ARCHIVE)], stdout=subprocess.PIPE) as proc:
    with tarfile.open(fileobj=proc.stdout, mode='r|') as tar:
        for member in tar:
            if member.name not in targets:
                continue
            assert member.isfile() and member.name not in verified
            path = targets[member.name]
            assert path.stat().st_size == member.size
            assert file_hash(path) == manifest[member.name]
            stream = tar.extractfile(member)
            assert stream is not None and digest(stream) == manifest[member.name]
            verified[member.name] = dict(bytes=member.size, sha256=manifest[member.name])
    assert proc.wait() == 0
assert verified.keys() == targets.keys()
assert file_hash(ARCHIVE) == SHA
report = dict(scope=str(SCOPE), archive=str(ARCHIVE), archive_sha256=SHA,
              windows_copy=LOCAL_PATH, windows_copy_bytes=LOCAL_BYTES,
              windows_copy_sha256=args.local_verified_sha256,
              verified=verified, bytes=sum(v['bytes'] for v in verified.values()),
              applied=False, removed=[])
output = ROOT / 'build/fix-20260914/curved-compaction-v1.json'
assert not output.exists()
output.write_text(json.dumps(report, indent=2) + '\n')
if args.apply:
    for name, path in targets.items():
        assert path.resolve().is_relative_to(SCOPE) and not path.is_symlink()
        assert file_hash(path) == verified[name]['sha256']
        path.unlink()
        report['removed'].append(name)
    report['applied'] = True
    output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('verified','removed')}, indent=2))
print('VERIFIED_FIXED_CURVED_COMPACTION_PASS', len(verified))
