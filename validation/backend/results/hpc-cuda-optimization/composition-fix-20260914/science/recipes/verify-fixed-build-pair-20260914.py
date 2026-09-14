"""Bind the physical-fix baseline and S5 candidate to their actual built inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

original=Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
candidate=Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914')
baseline=Path('/home/ubuntu/projects/ARCH-corrected-fused-baseline-20260914')
sys.path.insert(0,str(original/'tools'))
import summarize_cuda_compile_memory as metrics
out=candidate/'build/fix-20260914/build-pair-v1.json'
assert not out.exists()
allowed={'src/cuda/amr/RefinementIndicators.cu','src/cuda/amr/RefinementIndicators.h',
         'src/cuda/runtime/amr/CudaBackendIndicators.cpp','src/cuda/runtime/control/CudaBackendInternal.h',
         'tests/cuda/test_cuda_multiblock_hydro.cu','tests/cuda/test_refinement_indicators.cpp'}
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
report=dict(status='running',scope='identical shared physical fixes; differences restricted to S5 batching and its two tests',builds={})
manifests={}
for label,root,folder in (('candidate',candidate,'build/fix-20260914/full-build-v1'),
                          ('baseline',baseline,'build/corrected-fused-20260914')):
    source=root/folder/'source-files.sha256'
    artifacts=root/folder/'artifacts.sha256'
    subprocess.run(['sha256sum','-c',str(source)],cwd=root,stdout=subprocess.DEVNULL,check=True)
    subprocess.run(['sha256sum','-c',str(artifacts)],cwd=root,check=True)
    manifests[label]={name:checksum for checksum,name in (line.split('  ',1) for line in source.read_text().splitlines())}
    log=root/folder/'build-child.log'
    calls=metrics.parse_samples(log,'commands')
    assert calls and all(row['exit_code']==0 for row in calls)
    report['builds'][label]=dict(root=str(root),source_manifest_sha256=sha(source),
        artifact_manifest_sha256=sha(artifacts),build_log_sha256=sha(log),
        completed_invocations=len(calls),compile_metrics=calls)
assert manifests['candidate'].keys()==manifests['baseline'].keys()
changed={name for name,value in manifests['candidate'].items() if manifests['baseline'][name]!=value}
assert changed==allowed,sorted(changed)
report.update(status='passed',different_source_files=sorted(changed),
              common_input_files=len(manifests['candidate'])-len(changed))
out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(status=report['status'],differences=sorted(changed),builds={label:dict(completed=v['completed_invocations'],top=v['compile_metrics'][:2]) for label,v in report['builds'].items()}),indent=2))
print('FIXED_BUILD_PAIR_IDENTITY_PASS')
