"""All six original coupled methods at 8/32/128 blocks, same-binary CPU/CUDA.

Full terminal time, original pointwise/source/reduction budgets and aligned
workload required. This is a numerical gate during builds, NEVER formal timing.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914')
sys.path[:0] = [str(ROOT/'validation/backend'),str(ROOT/'tools')]
import run_microphysics_timing as timing
import validation_provenance as provenance
import validate_backend_results as validation

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--label',default='coupled-scale-v1')
p.add_argument('--modules',nargs='+',default=[f'coupled_{ode}_{diff}_all_transport'
    for ode in ('bd','be_nr','ros4') for diff in ('rkl2','rkl1')])
p.add_argument('--blocks',type=int,nargs='+',default=[128,8,32])
a=p.parse_args()
assert a.label and '/' not in a.label and '..' not in a.label
out=ROOT/'build/fix-20260914'/a.label
out.mkdir(parents=True,exist_ok=False)
build=ROOT/'build/fix-20260914/release'
args=SimpleNamespace(versions={'candidate':(ROOT,build)},output_root=out,preload=None,timeout=1800)
identity=dict(source_root=ROOT,build_dir=build,arch=build/'bin/ARCH',
              checkpoint_validator=build/'arch_cuda_single_level_validation')
cases=timing.make_cases(a.modules,a.blocks)
assert len(cases)==len(a.modules)*len(a.blocks)
report=dict(status='running',release_qualified=False,pilot=True,
    scope='numerical and work-alignment gate; concurrent builds; durations unqualified',
    identity_before=provenance.capture(**identity),recipe=provenance.file_identity(Path(__file__)),
    cases=cases,inputs=validation.runtime_case_inputs(cases,ROOT),lanes=[],comparisons=[],
    started_utc=datetime.now(timezone.utc).isoformat())
def save():
    (out/'evidence.json').write_text(json.dumps(report,indent=2,default=str)+'\n')
save()
try:
    for case in cases:
        reference=None
        for backend in ('cpu','cuda'):
            lane={}
            report['lanes'].append(lane)
            print('FIXED_SCALE_START',case['id'],backend,flush=True)
            timing.run_lane(args,case,'candidate',backend,8,'numeric',0,lane,save)
            if reference is None: reference=lane
            else:
                report['comparisons'].append(timing.compare(case,reference,lane,build/'arch_cuda_single_level_validation'))
            save()
            print('FIXED_SCALE_PASS',case['id'],backend,flush=True)
    assert len(report['lanes'])==2*len(cases) and len(report['comparisons'])==len(cases)
    report['status']='passed'
except BaseException as error:
    report.update(status='failed',error=repr(error))
    raise
finally:
    try:
        report['identity_after']=provenance.capture(**identity)
        provenance.require_unchanged(report['identity_before'],report['identity_after'])
        assert report['recipe']==provenance.file_identity(Path(__file__))
        assert report['inputs']==validation.runtime_case_inputs(cases,ROOT)
        report['identity_verified']=True
    except BaseException as error:
        report.update(status='failed',identity_error=repr(error))
        raise
    finally:
        report['finished_utc']=datetime.now(timezone.utc).isoformat()
        save()
print('FIXED_COUPLED_SCALE_ALL_PASS',flush=True)
