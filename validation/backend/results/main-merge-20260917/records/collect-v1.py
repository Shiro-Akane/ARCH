import hashlib
import json
from pathlib import Path
import tarfile

root = Path('/home/ubuntu/projects/ARCH-main-merge-20260917')
assert (root / 'logs/cpu-build-v2.exit').read_text().strip() == '0'
assert json.loads((root / 'application-v2/report.json').read_text())['status'] == 'passed'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

files = set()
for folder in ('logs', 'recorder-data', 'application-v1', 'application-v2'):
    for path in (root / folder).rglob('*'):
        if path.is_file() and not path.is_symlink():
            files.add(path)
for path in root.glob('*.sh'):
    files.add(path)
for path in root.glob('run-regression-v*.py'):
    files.add(path)
for name in ('source-v2.tar', 'recorder-contract', 'cpu-build-v1/bin/ARCH',
             'cpu-build-v1/CMakeCache.txt', 'cpu-build-v1/compile_commands.json',
             'cpu-build-v1/Testing/Temporary/LastTest.log'):
    path = root / name
    assert path.is_file() and not path.is_symlink(), path
    files.add(path)
entries = [{'path': str(path.relative_to(root)), 'bytes': path.stat().st_size(), 'sha256': sha(path)}
           for path in sorted(files)]
manifest = root / 'collection-members-v1.json'
with manifest.open('x') as stream:
    json.dump(entries, stream, indent=2)
    stream.write('\n')
files.add(manifest)
raw = root / 'main-merge-raw-v1.tar.gz'
compact = root / 'main-merge-compact-v1.tar.gz'
raw_only = {path for path in files if path.suffix == '.h5' or path.name in ('source-v2.tar', 'recorder-contract', 'ARCH')}
for archive, selected in ((raw, files), (compact, files - raw_only)):
    with tarfile.open(archive, 'x:gz') as tar:
        for path in sorted(selected):
            tar.add(path, arcname=str(path.relative_to(root)), recursive=False)
receipt = {'raw': {'name': raw.name, 'bytes': raw.stat().st_size(), 'sha256': sha(raw)},
           'compact': {'name': compact.name, 'bytes': compact.stat().st_size(), 'sha256': sha(compact)},
           'files': len(files), 'raw_only_files': len(raw_only), 'gpu_run': False}
with (root / 'collection-v1.json').open('x') as stream:
    json.dump(receipt, stream, indent=2)
    stream.write('\n')
print(json.dumps(receipt))
