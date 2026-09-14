"""Archive one finished formal phase; never run concurrently with timing."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

PROJECTS = Path('/home/ubuntu/projects')
FIX = PROJECTS/'ARCH-multiphysics-fix-20260914'
BASELINE = PROJECTS/'ARCH-corrected-fused-baseline-20260914'
LARGE = PROJECTS/'ARCH-large-application-20260914'
RECIPES = PROJECTS/'ARCH-microphysics-20260914/build/p12-20260914'
MODULES = ['diffusion_rkl1', 'diffusion_rkl2', 'burn_be_nr', 'burn_bd', 'burn_ros4']
MODULES += [f'coupled_{ode}_{diff}_all_transport' for ode in ('be_nr','bd','ros4') for diff in ('rkl1','rkl2')]

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024*1024):
            h.update(chunk)
    return h.hexdigest()

def check_report(r, large):
    assert r['status'] == 'passed' and r['pilot'] is False and 'identity_error' not in r
    assert len(r['cases']) == (6 if large else 3)
    assert len(r['lanes']) == (144 if large else 108)
    assert len(r['comparisons']) == (138 if large else 105)
    assert len(r['statistics']) == (24 if large else 18)
    assert all(v['status']=='passed' and v['returncode']==0 and not v['timed_out'] for v in r['lanes'])
    assert all(v['fields']['passed'] and v['fields']['status']=='pass' and v['workload_aligned'] for v in r['comparisons'])
    keys = ['case'] + ([] if large else ['version']) + ['backend','threads']
    unique = set()
    for statistic in r['statistics']:
        key = tuple(statistic[k] for k in keys)
        assert key not in unique
        unique.add(key)
        lanes = [v for v in r['lanes'] if tuple(v[k] for k in keys)==key]
        warm = [v for v in lanes if v['phase']=='warmup']
        measured = [v for v in lanes if v['phase']=='measured']
        assert len(warm)==1 and warm[0]['repeat']==0
        assert len(measured)==5 and {v['repeat'] for v in measured}==set(range(5))
        samples = [v['arch_wall_seconds'] for v in measured]
        assert all(math.isfinite(v) and v>0 for v in samples)
        assert samples==statistic['samples'] and statistics.median(samples)==statistic['median']
        assert min(samples)==statistic['minimum'] and max(samples)==statistic['maximum']
        assert statistics.pstdev(samples)==statistic['population_stdev']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('module', choices=['large']+MODULES)
    a=p.parse_args()
    large=a.module=='large'
    root=LARGE if large else FIX
    base=root/('build/p12-20260914/large-application-v2' if large else 'build/fix-20260914/timing')
    label='timing-formal-v1' if large else f'formal-{a.module}-v1'
    run=base/label
    out=base/(label+'-archive')
    compact=out/'compact'
    stem='large-application-formal-v1' if large else f'fixed-formal-{a.module}-v1'
    raw=root/'build'/(stem+'.tar.zst')
    pack=root/'build'/(stem+'-compact.tar.zst')
    assert not out.exists() and not raw.exists() and not pack.exists()
    r=json.loads((run/'evidence.json').read_text())
    check_report(r,large)
    if not large:
        product=json.loads((FIX/'build/fix-20260914/timing-products-archive-v1/archive.json').read_text())
        assert product['status']=='passed'
        assert Path(product['archive'])==FIX/'build/fixed-timing-products-v1.tar.zst'
        assert sha(Path(product['archive']))==product['sha256']
    os.environ.update(OMP_NUM_THREADS='8',OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close')
    sys.path.insert(0,str(root/'tools'))
    import validation_provenance as provenance
    sources={'application':(root,base/'release')} if large else {
        'candidate':(FIX,FIX/'build/fix-20260914/release'),
        'baseline':(BASELINE,BASELINE/'build/corrected-fused-20260914/release')}
    for version,(source,build) in sources.items():
        before=r['identity_before'] if large else r['identities_before'][version]
        after=r['identity_after'] if large else r['identities_after'][version]
        provenance.require_unchanged(before,after)
        provenance.require_unchanged(before,provenance.capture(source_root=source,build_dir=build,
            arch=build/'bin/ARCH',checkpoint_validator=build/'arch_cuda_single_level_validation'))
    assert r['recipe']==provenance.file_identity(Path(r['recipe']['path']))
    for row in r['lanes']:
        for key in ('parameter','checkpoint_identity'):
            assert row[key]==provenance.file_identity(Path(row[key]['path']))
    files={path for path in run.rglob('*') if path.is_file()}
    if large:
        files.update(path for path in base.glob('timing-*') if path.is_file())
        recipes=['replay-large-application-timing-20260914.sh',
                 'large-formal-environment-spotcheck-20260914.json',
                 'summarize-large-formal-20260914.py']
    else:
        files.update(path for path in base.glob(label+'*') if path.is_file())
        recipes=['replay-fixed-timing-20260914.sh']
    recipes += ['archive-formal-timing-20260914.py','compact-verified-formal-hdf-20260914.py']
    files.update(RECIPES/name for name in recipes)
    files.add(Path(r['recipe']['path']))
    assert all(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(PROJECTS) for path in files)
    compact.mkdir(parents=True)
    # Retain every evidence byte in raw; shallow per-lane directories are a
    # byte-identical projection for Windows/Git, not a rewritten result.
    mapping={}
    sample_index=[]
    for index,row in enumerate(r['lanes']):
        directory=Path(row['directory'])
        assert directory.resolve().is_relative_to(run.resolve())
        sample_index.append(dict(index=index,**{k:row[k] for k in ('case','backend','threads','phase','repeat')},
            version=row.get('version','application'),original_directory=str(directory)))
        for path in sorted(directory.rglob('*')):
            if not path.is_file() or path.suffix=='.h5': continue
            target=compact/'samples'/f'{index:03d}'/path.relative_to(directory)
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(path,target)
            mapping[str(target.relative_to(compact))]=str(path.relative_to(PROJECTS))
    for index,path in enumerate(sorted(files)):
        if path.is_relative_to(run) and path!=run/'evidence.json': continue
        target=compact/'environment'/f'{index:04d}-{path.name}'
        if path==run/'evidence.json': target=compact/'evidence.json'
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(path,target)
        mapping[str(target.relative_to(compact))]=str(path.relative_to(PROJECTS))
    (compact/'sample-index.json').write_text(json.dumps(sample_index,indent=2)+'\n')
    (compact/'projection-map.json').write_text(json.dumps(mapping,indent=2)+'\n')
    digests={str(path.relative_to(PROJECTS)):dict(sha256=sha(path),bytes=path.stat().st_size) for path in sorted(files)}
    (compact/'raw-manifest.json').write_text(json.dumps(digests,indent=2)+'\n')
    (compact/'binary-backup-scope.txt').write_text(
        'Actual large binaries are in the double-verified large-application-runtime-v1 archive.\n' if large else
        'Candidate products are in fixed-science-v1. Both products and corrected baseline are additionally in fixed-timing-products-v1; verify that separate archive before deleting any expanded data.\n')
    if not large:
        shutil.copy2(FIX/'build/fix-20260914/timing-products-archive-v1/archive.json',compact/'product-archive.json')
    inventory=out/'raw-paths.txt'
    inventory.write_text('\n'.join(digests)+'\n')
    subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(raw),'-C',str(PROJECTS),
        '-T',str(inventory),str(compact.relative_to(PROJECTS))],check=True)
    assert all(sha(PROJECTS/name)==data['sha256'] and (PROJECTS/name).stat().st_size==data['bytes'] for name,data in digests.items())
    record=dict(status='passed',module=a.module,scope=r.get('scope',r.get('timing_scope')),
        runs=len(r['lanes']),comparisons=len(r['comparisons']),archive=str(raw),bytes=raw.stat().st_size,
        sha256=sha(raw),files=len(files),binary_archived_in_this_pack=False)
    (compact/'raw-archive.json').write_text(json.dumps(record,indent=2)+'\n')
    subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(pack),'-C',str(out),'compact'],check=True)
    print(json.dumps(dict(raw=record,compact=dict(path=str(pack),bytes=pack.stat().st_size,sha256=sha(pack))),indent=2))
    print('FORMAL_PHASE_ARCHIVE_PASS',a.module)

if __name__=='__main__': main()
