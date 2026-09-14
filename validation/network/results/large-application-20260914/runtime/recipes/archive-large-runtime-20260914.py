"""Archive only after the original six-case application matrix has completed."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECTS = Path('/home/ubuntu/projects')
ROOT = PROJECTS / 'ARCH-large-application-20260914'
# Use the original wrapper's declared environment for the identity comparison;
# this archive does not rerun ARCH or change its recorded execution environment.
os.environ.update(OMP_NUM_THREADS='8', OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
BASE = ROOT / 'build/p12-20260914/large-application-v2'
BUILD = BASE / 'release'
RUN = BASE / 'runtime-v1'
OUT = BASE / 'runtime-archive-v1'
COMPACT = OUT / 'compact'
RECIPE_ROOT = PROJECTS / 'ARCH-microphysics-20260914/build/p12-20260914'
NETWORKS = PROJECTS / 'ARCH-large-networks-20260909'
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
sys.path.insert(0, str(ROOT / 'validation/network'))
import run_large_application_timing as timing

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024*1024):
            h.update(chunk)
    return h.hexdigest()

assert not OUT.exists()
evidence = json.loads((RUN/'backend-validation-evidence.json').read_text())
manifest = ROOT/'validation/network/large_runtime_cases.json'
cases = json.loads(manifest.read_text())['cases']
assert evidence['identity_verified_after_run'] is True
assert evidence['manifest_sha256'] == sha(manifest)
assert [row['id'] for row in evidence['cases']] == [row['id'] for row in cases] and len(cases) == 6
for expected, row in zip(cases, evidence['cases']):
    assert [v['cpu']['steps'] for v in row['checkpoints']] == expected['accepted_steps'] == [1,2,5]
    assert [v['cuda']['steps'] for v in row['checkpoints']] == expected['accepted_steps']
    assert all(v['parity']['passed'] for v in row['checkpoints'])
    assert row['scientific']['parity']['passed']
assert len(list(RUN.rglob('*.par'))) == 48
provenance.require_unchanged(evidence['provenance'], provenance.capture(
    source_root=ROOT,build_dir=BUILD,arch=BUILD/'bin/ARCH',checkpoint_validator=BUILD/'arch_cuda_single_level_validation'))
for name in ('artifacts.sha256','source-files.sha256','large-objects-after.sha256'):
    subprocess.run(['sha256sum','-c',str(BASE/'fixed-integration-v1'/name)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
subprocess.run(['sha256sum','-c',str(BASE/'network-files.sha256')],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
files = set(path for path in RUN.rglob('*') if path.is_file())
files.update(path for path in NETWORKS.rglob('*') if path.is_file())
files.update(path for path in (BASE/'integration-evidence-v1').rglob('*') if path.is_file())
files.update(path for path in BASE.glob('runtime-*') if path.is_file())
files.update(BUILD/name for name in ('bin/ARCH','libarch_cuda_backend.a','libarch_cuda_sparse_provider.a','arch_cuda_single_level_validation'))
files.update(ROOT/name for name in ('EOS_toolkit/tables/helmholtz/helm_table.dat',
    'validation/network/large_runtime_cases.json','validation/burn/inputs/bd.par',
    'validation/network/run_large_application_timing.py','tests/tooling/test_large_application_timing.py'))
files.update(RECIPE_ROOT/name for name in ('replay-large-runtime-20260914.sh','replay-large-cudss-v2-20260914.sh',
    'integrate-fixed-large-20260914.sh','archive-large-runtime-20260914.py'))
assert all(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(PROJECTS) for path in files)
COMPACT.mkdir(parents=True)
quality = []
for case in cases:
    for backend in ('cpu','cuda'):
        directory = RUN/case['id']/'scientific'/backend
        initial = list(directory.glob('*_chk_0000.h5'))
        final = list(directory.glob('*_chk_0001.h5'))
        assert len(initial) == len(final) == 1
        quality.append(dict(case=case['id'],backend=backend,**timing.trajectory_quality(
            timing.read_state(initial[0]),timing.read_state(final[0]),
            timing.expected_species(case),case['qualification']['species_sum_atol'])))
(COMPACT/'additional-trajectory-quality.json').write_text(json.dumps(quality,indent=2)+'\n')
digests = {str(path.relative_to(PROJECTS)):sha(path) for path in sorted(files)}
for path in sorted(files):
    if path.is_relative_to(NETWORKS) or path.name == 'helm_table.dat':
        continue
    with path.open('rb') as stream:
        magic = stream.read(8)
    if magic.startswith((b'\x7fELF',b'!<arch>',b'\x89HDF')) or path.suffix in ('.o','.a','.so','.zst'):
        continue
    target = COMPACT/'files'/path.relative_to(PROJECTS)
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(path,target)
(COMPACT/'raw-manifest.json').write_text(json.dumps(digests,indent=2)+'\n')
inventory=OUT/'raw-paths.txt'
inventory.write_text('\n'.join(digests)+'\n')
raw=ROOT/'build/large-application-runtime-v1.tar.zst'
compact=ROOT/'build/large-application-runtime-compact-v1.tar.zst'
assert not raw.exists() and not compact.exists()
subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(raw),'-C',str(PROJECTS),
    '-T',str(inventory),str(COMPACT.relative_to(PROJECTS))],check=True)
assert all(sha(PROJECTS/name)==digest for name,digest in digests.items())
record=dict(status='passed',scope='original full application numerical matrix, not formal timing or independent weak-reaction oracle',
    cases=6,runs=48,checkpoint_pairs=18,scientific_pairs=6,archive=str(raw),bytes=raw.stat().st_size,
    sha256=sha(raw),files=len(files),source_commit=evidence['provenance']['source']['commit'])
(COMPACT/'raw-archive.json').write_text(json.dumps(record,indent=2)+'\n')
subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(compact),'-C',str(OUT),'compact'],check=True)
print(json.dumps(dict(raw=record,compact=dict(path=str(compact),bytes=compact.stat().st_size,sha256=sha(compact))),indent=2))
print('LARGE_APPLICATION_NUMERIC_ARCHIVE_PASS_NOT_PERFORMANCE_PASS')
