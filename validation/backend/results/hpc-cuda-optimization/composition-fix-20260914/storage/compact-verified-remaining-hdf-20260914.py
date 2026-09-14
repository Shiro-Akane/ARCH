"""Reclaim ONLY manifest-listed, duplicate HDF outputs from two frozen stages.

Both complete raw archives remain on server and Windows. New validation,
source files, logs, binaries, directories and unarchived data are untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

profiles={
    'fused':dict(root='/home/ubuntu/projects/ARCH-microphysics-20260914',
        prefix='build/p12-20260914/',archive='p12-fused-validation-timing-v1.tar.zst',
        sha='fe0bbe45e885121b7d816eff380f06415f46b6c73b7f48e220764ac08550d0b8',bytes=1488897148,
        manifest='build/p12-20260914/fused-evidence-archive-v1/compact/raw-files.sha256'),
    's5':dict(root='/home/ubuntu/projects/ARCH-s5-indicator-20260914',
        prefix='build/s5-20260914/validation-v1/',archive='s5-numeric-validation-v1.tar.zst',
        sha='658ec5c364fde845d64fa2f6af56ab04cfd02b977781915c45894e3e0370deff',bytes=1521541667,
        manifest='build/s5-20260914/numeric-archive-v1/compact/raw-files.sha256')}
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--dataset',choices=profiles,required=True)
p.add_argument('--local-verified-sha256',required=True)
p.add_argument('--apply',action='store_true')
args=p.parse_args()
profile=profiles[args.dataset]
root=Path(profile['root']).resolve(strict=True)
scope=(root/profile['prefix']).resolve(strict=True)
assert scope.is_relative_to(root/'build')
archive=root/'build'/profile['archive']
assert args.local_verified_sha256==profile['sha']
def digest(stream):
    h=hashlib.sha256()
    while chunk:=stream.read(1024*1024): h.update(chunk)
    return h.hexdigest()
def sha(path):
    with path.open('rb') as stream: return digest(stream)
assert archive.stat().st_size==profile['bytes'] and sha(archive)==profile['sha']
manifest={name:value for value,name in (line.split('  ',1) for line in (root/profile['manifest']).read_text().splitlines())}
names=[name for name in manifest if name.startswith(profile['prefix']) and name.endswith('.h5')]
targets={name:root/name for name in names if (root/name).is_file()}
assert targets and all(not path.is_symlink() and path.resolve().is_relative_to(scope) for path in targets.values())
verified={}
with subprocess.Popen(['zstd','-dc',str(archive)],stdout=subprocess.PIPE) as process:
    with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
        for member in tar:
            if member.name not in targets: continue
            assert member.isfile() and member.name not in verified
            path=targets[member.name]
            assert path.stat().st_size==member.size and sha(path)==manifest[member.name]
            stream=tar.extractfile(member)
            assert stream is not None and digest(stream)==manifest[member.name]
            verified[member.name]=dict(bytes=member.size,sha256=manifest[member.name])
    assert process.wait()==0
assert verified.keys()==targets.keys() and sha(archive)==profile['sha']
report=dict(dataset=args.dataset,scope=str(scope),archive=str(archive),archive_sha256=profile['sha'],
    windows_copy='C:/tmp/ARCH-perf-20260909/build/'+profile['archive'],
    windows_copy_sha256=args.local_verified_sha256,windows_copy_bytes=profile['bytes'],
    already_absent=[name for name in names if name not in targets],verified=verified,
    bytes=sum(row['bytes'] for row in verified.values()),applied=False,removed=[])
output=root/'build'/('remaining-'+args.dataset+'-hdf-compaction-v1.json')
assert not output.exists()
output.write_text(json.dumps(report,indent=2)+'\n')
if args.apply:
    for name,path in targets.items():
        assert not path.is_symlink() and path.resolve().is_relative_to(scope)
        assert sha(path)==verified[name]['sha256']
        path.unlink()
        report['removed'].append(name)
    report['applied']=True
    output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('verified','removed','already_absent')},indent=2))
print('VERIFIED_REMAINING_HDF_COMPACTION_PASS',len(verified),'already_absent',len(report['already_absent']))
