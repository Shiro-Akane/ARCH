"""Confirm that published formal-stage working files equal their Git blob bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--revision', required=True)
    p.add_argument('--modules', nargs='+', required=True)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    base = 'validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/'
    allowed = ['diffusion_rkl1', 'diffusion_rkl2', 'burn_be_nr', 'burn_bd', 'burn_ros4']
    allowed += [f'coupled_{ode}_{diff}_all_transport' for ode in ('be_nr','bd','ros4') for diff in ('rkl1','rkl2')]
    assert a.modules and len(set(a.modules)) == len(a.modules) and set(a.modules) <= set(allowed)
    root = Path(subprocess.check_output(['git','rev-parse','--show-toplevel'], text=True).strip())
    revision = subprocess.check_output(['git','rev-parse','--verify',a.revision+'^{commit}'], text=True).strip()
    algorithm = subprocess.check_output(['git','rev-parse','--show-object-format'], text=True).strip()
    assert algorithm in ('sha1', 'sha256')
    paths = [base+x for x in a.modules]
    rows = subprocess.check_output(['git','ls-tree','-rz',revision,'--',*paths]).split(b'\0')
    counts = dict.fromkeys(a.modules, 0)
    total = 0
    for row in rows:
        if not row:
            continue
        header, name = row.split(b'\t', 1)
        mode, kind, oid = header.decode().split()
        assert kind == 'blob'
        name = name.decode('utf-8')
        path = root/name
        assert path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(root.resolve())
        size = path.stat().st_size
        h = hashlib.new(algorithm)
        h.update(f'blob {size}\0'.encode())
        with path.open('rb') as stream:
            while chunk := stream.read(1024*1024):
                h.update(chunk)
        assert h.hexdigest() == oid, name
        module = name.removeprefix(base).split('/')[0]
        counts[module] += 1
        total += size
    assert all(counts.values()), 'No committed evidence for a selected module'
    result = json.dumps(dict(status='passed', git_commit=revision, files=counts, bytes=total,
        scope='all tracked selected-module files byte-identical to Git blobs; raw archive SHA-256 checks are separate'), indent=2)
    if a.output:
        with a.output.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(result+'\n')
    print(result)


if __name__ == '__main__':
    main()
