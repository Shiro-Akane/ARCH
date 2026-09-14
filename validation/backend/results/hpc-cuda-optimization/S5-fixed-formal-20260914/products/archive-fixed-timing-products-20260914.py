"""One shared product/source backup, after timing stops and before HDF compaction."""
import hashlib
import json
from pathlib import Path
import subprocess

P=Path('/home/ubuntu/projects')
F=P/'ARCH-multiphysics-fix-20260914'
B=P/'ARCH-corrected-fused-baseline-20260914'
O=P/'ARCH-microphysics-20260914/build/p12-20260914'
out=F/'build/fix-20260914/timing-products-archive-v1'
raw=F/'build/fixed-timing-products-v1.tar.zst'
assert not out.exists() and not raw.exists()
files=set()
for root,sub in ((F,'fix-20260914'),(B,'corrected-fused-20260914')):
    base=root/'build'/sub
    for name in ('bin/ARCH','libarch_cuda_backend.a','arch_cuda_single_level_validation'):
        files.add(base/'release'/name)
    files.add(root/'EOS_toolkit/tables/helmholtz/helm_table.dat')
    files.add(base/'release/CMakeCache.txt')
    evidence=base/'full-build-v1' if root==F else base
    for name in ('source-head.txt','source-files.sha256','artifacts.sha256'):
        files.add(evidence/name)
    files.update(path for path in evidence.glob('*.patch') if path.is_file())
    subprocess.run(['sha256sum','-c',str(evidence/'artifacts.sha256')],cwd=root,check=True)
files.update(O/name for name in ('multiphysics-closure-muscl-overlay-v3.tar',
    'replay-fixed-timing-20260914.sh','archive-fixed-timing-products-20260914.py',
    'run-fixed-formal-phase-20260914.ps1',
    'verify-fixed-build-pair-20260914.py'))
files.add(F/'build/fix-20260914/build-pair-v1.json')
assert all(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(P) for path in files)
out.mkdir(parents=True)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024): h.update(chunk)
    return h.hexdigest()
manifest={str(path.relative_to(P)):dict(sha256=sha(path),bytes=path.stat().st_size) for path in sorted(files)}
(out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
(out/'paths.txt').write_text('\n'.join(manifest)+'\n')
subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(raw),'-C',str(P),
    '-T',str(out/'paths.txt'),str(out.relative_to(P))],check=True)
assert all(sha(P/name)==row['sha256'] for name,row in manifest.items())
record=dict(status='passed',archive=str(raw),sha256=sha(raw),bytes=raw.stat().st_size,files=len(files))
(out/'archive.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
print('FIXED_TIMING_PRODUCTS_ARCHIVE_PASS')
