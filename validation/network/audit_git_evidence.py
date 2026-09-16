"""Verify committed evidence matches local original bytes; not a science gate."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--revision', required=True)
p.add_argument('--scope', action='append', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip()).resolve()
revision = subprocess.check_output(['git', 'rev-parse', '--verify', a.revision + '^{commit}'], text=True).strip()
algorithm = subprocess.check_output(['git', 'rev-parse', '--show-object-format'], text=True).strip()
if algorithm not in ('sha1', 'sha256') or a.output.exists():
    raise ValueError('unknown Git format or existing output')
results = []
for scope in a.scope:
    directory = (root / scope).resolve(strict=True)
    if not directory.is_relative_to(root / 'validation/network/results'):
        raise ValueError('not a network results scope')
    scope = directory.relative_to(root).as_posix()
    count = total = 0
    committed_names = set()
    for row in subprocess.check_output(['git', 'ls-tree', '-rz', revision, '--', scope]).split(b'\0'):
        if not row:
            continue
        header, name = row.split(b'\t', 1)
        mode, kind, oid = header.decode().split()
        path = root / name.decode('utf-8')
        committed_names.add(path.relative_to(directory).as_posix())
        if kind != 'blob' or not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(directory):
            raise ValueError('nonregular or escaped evidence')
        data = path.read_bytes()
        if hashlib.new(algorithm, f'blob {len(data)}\0'.encode() + data).hexdigest() != oid:
            raise ValueError('Git bytes differ: ' + str(path))
        count += 1
        total += len(data)
    if not count:
        raise ValueError('empty committed scope')
    local_names = {path.relative_to(directory).as_posix() for path in directory.rglob('*') if path.is_file()}
    if local_names != committed_names:
        raise ValueError('Git/local evidence inventory differs: missing=' + repr(sorted(local_names - committed_names))
                         + ' extra=' + repr(sorted(committed_names - local_names)))
    results.append(dict(scope=scope, files=count, bytes=total))
result = dict(status='committed_evidence_inventory_and_bytes_match', git_commit=revision, scopes=results,
              recipe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              additional_scientific_or_performance_qualification=False)
with a.output.open('x', encoding='utf-8') as stream:
    json.dump(result, stream, indent=2)
    stream.write('\n')
print(json.dumps(result, indent=2))
