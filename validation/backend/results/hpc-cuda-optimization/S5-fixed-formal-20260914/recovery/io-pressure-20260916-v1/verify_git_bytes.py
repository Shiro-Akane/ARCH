"""Fixed-scope Git byte audit; this does not qualify the interrupted experiment."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import preserve_failed_formal as p

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--revision', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip()).resolve()
scope = Path(__file__).resolve().parent.relative_to(root).as_posix()
revision = subprocess.check_output(['git', 'rev-parse', '--verify', args.revision + '^{commit}'], text=True).strip()
algorithm = subprocess.check_output(['git', 'rev-parse', '--show-object-format'], text=True).strip()
p.require(algorithm in ('sha1', 'sha256'), 'Unknown Git object format')
rows = subprocess.check_output(['git', 'ls-tree', '-rz', revision, '--', scope]).split(b'\0')
count = total = 0
for row in rows:
    if not row: continue
    header, name = row.split(b'\t', 1)
    mode, kind, oid = header.decode().split()
    path = root / name.decode('utf-8')
    p.require(kind == 'blob' and path.is_file() and not path.is_symlink() and
              path.resolve().is_relative_to(root / scope), 'Nonregular or escaped evidence')
    size = path.stat().st_size
    digest = hashlib.new(algorithm, f'blob {size}\0'.encode())
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024): digest.update(block)
    p.require(digest.hexdigest() == oid, 'Git bytes differ: ' + str(path))
    count += 1
    total += size
p.require(count > 0, 'Empty committed scope')
result = dict(status='git_bytes_verified_for_failed_evidence', git_commit=revision,
              files=count, bytes=total, scientific_validation_complete=False,
              recipe_sha256=p.sha(Path(__file__)))
p.save_new(args.output, result)
print(result)
