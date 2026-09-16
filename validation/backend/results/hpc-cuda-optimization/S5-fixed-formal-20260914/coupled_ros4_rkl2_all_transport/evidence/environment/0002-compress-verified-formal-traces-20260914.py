"""Losslessly compress large, doubly archived per-kernel traces; retain all log bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

PROJECTS=Path('/home/ubuntu/projects').resolve(strict=True)
MODULES=['diffusion_rkl1','diffusion_rkl2','burn_be_nr','burn_bd','burn_ros4']
MODULES += [f'coupled_{ode}_{diff}_all_transport' for ode in ('be_nr','bd','ros4') for diff in ('rkl1','rkl2')]

def digest(stream):
    h=hashlib.sha256()
    while chunk := stream.read(1024*1024): h.update(chunk)
    return h.hexdigest()

def sha(path):
    with path.open('rb') as stream: return digest(stream)

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('module',choices=['large']+MODULES)
p.add_argument('--local-verified-sha256',required=True)
p.add_argument('--local-verified-bytes',required=True,type=int)
p.add_argument('--apply',action='store_true')
a=p.parse_args()
large=a.module=='large'
root=PROJECTS/('ARCH-large-application-20260914' if large else 'ARCH-multiphysics-fix-20260914')
base=root/('build/p12-20260914/large-application-v2' if large else 'build/fix-20260914/timing')
label='timing-formal-v1' if large else f'formal-{a.module}-v1'
scope=(base/label).resolve(strict=True)
assert scope.is_relative_to(base.resolve(strict=True)) and not (base/label).is_symlink()
compact=base/(label+'-archive')/'compact'
record=json.loads((compact/'raw-archive.json').read_text())
archive=Path(record['archive']).resolve(strict=True)
assert archive.is_relative_to(root/'build') and not Path(record['archive']).is_symlink()
assert record['module']==a.module and record['status']=='passed'
assert record['sha256']==a.local_verified_sha256 and record['bytes']==a.local_verified_bytes
assert archive.stat().st_size==a.local_verified_bytes and sha(archive)==a.local_verified_sha256
evidence=json.loads((scope/'evidence.json').read_text())
assert evidence['status']=='passed' and not evidence['pilot'] and 'identity_error' not in evidence
assert len(evidence['lanes'])==(144 if large else 108)
manifest=json.loads((compact/'raw-manifest.json').read_text())
allowed={'MicrophysicsTiming_backend_trace.tsv','MicrophysicsTiming_diffusion_schedule.tsv',
         'MicrophysicsTiming_regrid.tsv'}
targets={str(path.relative_to(PROJECTS)):path for path in scope.rglob('*.tsv')
         if path.is_file() and path.name in allowed and path.stat().st_size>2*1024*1024}
assert set(targets) <= manifest.keys()
assert all(not path.is_symlink() and path.resolve().is_relative_to(scope) for path in targets.values())
verified={}
with subprocess.Popen(['zstd','-dc',str(archive)],stdout=subprocess.PIPE) as proc:
    with tarfile.open(fileobj=proc.stdout,mode='r|') as tar:
        for member in tar:
            if member.name not in targets: continue
            assert member.isfile() and member.name not in verified
            path=targets[member.name]
            expected=manifest[member.name]
            assert path.stat().st_size==member.size==expected['bytes']
            assert sha(path)==expected['sha256']
            stream=tar.extractfile(member)
            assert stream is not None and digest(stream)==expected['sha256']
            verified[member.name]=expected
    assert proc.wait()==0
assert verified.keys()==targets.keys() and sha(archive)==a.local_verified_sha256
output=base/(label+'-trace-compression.json')
assert not output.exists()
report=dict(scope=str(scope),archive=str(archive),archive_sha256=a.local_verified_sha256,
    windows_copy='C:/tmp/ARCH-perf-20260909/build/'+archive.name,
    windows_copy_bytes=a.local_verified_bytes,windows_copy_sha256=a.local_verified_sha256,
    verified=verified,bytes=sum(v['bytes'] for v in verified.values()),applied=False,removed=[],compressed=[])
output.write_text(json.dumps(report,indent=2)+'\n')
if a.apply:
    try:
        for name,path in targets.items():
            assert path.resolve().is_relative_to(scope) and not path.is_symlink()
            assert sha(path)==verified[name]['sha256']
            destination=path.with_suffix(path.suffix+'.zst')
            assert not destination.exists() and destination.parent.resolve().is_relative_to(scope)
            subprocess.run(['zstd','-T1','-3','--quiet','--keep',str(path),'-o',str(destination)],check=True)
            with subprocess.Popen(['zstd','-dc',str(destination)],stdout=subprocess.PIPE) as decoder:
                assert digest(decoder.stdout)==verified[name]['sha256']
                assert decoder.wait()==0
            assert sha(path)==verified[name]['sha256']
            report['compressed'].append(dict(original=name,path=str(destination),
                bytes=destination.stat().st_size,sha256=sha(destination),
                decompressed_sha256=verified[name]['sha256']))
            path.unlink()
            report['removed'].append(name)
        report['applied']=True
    finally:
        output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('verified','removed','compressed')},indent=2))
print('VERIFIED_FORMAL_TRACE_COMPRESSION_PASS',len(verified))
