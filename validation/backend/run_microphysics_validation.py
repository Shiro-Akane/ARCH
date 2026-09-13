"""Replay existing scientific gates for the microphysics execution candidate.

Each phase retains its command, failures and executable/source identity. This
does not build, time, relax budgets or award overall release qualification.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import validation_provenance as provenance


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--output-dir',type=Path,required=True)
    p.add_argument('--phase',required=True,choices=('canonical','first-law','nse','independent',
        'coupled','contracts','amr','curved','lifecycle','restart','tails'))
    a=p.parse_args()
    build,out=a.build_dir.resolve(),a.output_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out == ROOT/'build':
        p.error('use a new source/build subdirectory')
    out.mkdir(parents=True,exist_ok=False)
    identity=dict(arch=build/'bin/ARCH',checkpoint_validator=build/'arch_cuda_single_level_validation',
                  source_root=ROOT,build_dir=build)
    before=provenance.capture(**identity)
    report=dict(status='running',phase=a.phase,release_qualified=False,identity_before=before,
        recipe=provenance.file_identity(Path(__file__)),started_utc=datetime.now(timezone.utc).isoformat())
    common=['--arch',str(identity['arch']),'--checkpoint-validator',str(identity['checkpoint_validator']),
        '--source-root',str(ROOT),'--build-dir',str(build)]
    if a.phase=='canonical':
        original=ROOT/'validation/backend/cases.json'
        selected=[c for c in json.loads(original.read_text())['cases']
                  if c['id'].startswith(('diffusion_rkl1_','diffusion_rkl2_')) or c['id']=='burn_bd_two_block']
        if len(selected)!=7:
            raise RuntimeError('canonical microphysics inventory changed; inspect before running')
        manifest=out/'canonical-cases.json'
        manifest.write_text(json.dumps(dict(schema=1,cases=selected),indent=2)+'\n')
        report['original_manifest']=provenance.file_identity(original)
        command=[sys.executable,str(ROOT/'tools/validate_backend_results.py'),*common,
            '--manifest',str(manifest),'--output-root',str(out/'results')]
    elif a.phase in ('first-law','nse'):
        folder={'first-law':'application-first-law-20260907','nse':'nse-application-native-20260907'}[a.phase]
        command=[sys.executable,str(ROOT/'validation/burn/results'/folder/'replay.py'),
            '--build-dir',str(build),'--output-dir',str(out/'results')]
    elif a.phase=='independent':
        command=[sys.executable,str(ROOT/'validation/burn/time_reference.py'),
            '--binary',str(build/'arch_burn_mainline_reference'),'--build-dir',str(build)]
    elif a.phase=='coupled':
        command=[sys.executable,str(ROOT/'validation/backend/verify_microphysics_coupling.py'),
            '--build-dir',str(build),'--output-dir',str(out/'results')]
    else:
        command=[sys.executable,str(ROOT/'validation/backend/results/hpc-cuda-optimization/S4/validation-20260913/verify_s4.py'),
            '--source-root',str(ROOT),'--build-dir',str(build),'--output-root',str(out/'results'),'--phase',a.phase]
    env=dict(os.environ,OMP_NUM_THREADS='8',OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close')
    report.update(command=command,environment={k:env[k] for k in ('OMP_NUM_THREADS','OMP_DYNAMIC','OMP_PLACES','OMP_PROC_BIND')})
    def save():
        (out/'record.json').write_text(json.dumps(report,indent=2)+'\n')
    save()
    try:
        with (out/'stdout.log').open('w') as stdout,(out/'stderr.log').open('w') as stderr:
            rc=subprocess.run(command,cwd=ROOT,env=env,stdout=stdout,stderr=stderr,timeout=21600).returncode
        report['returncode']=rc
        if rc: raise RuntimeError(f'{a.phase} exited {rc}; original logs retained')
        report['status']='passed'
    except BaseException as error:
        report.update(status='failed',error=repr(error))
        raise
    finally:
        try:
            provenance.require_unchanged(before,provenance.capture(**identity))
            report['identity_verified_after_run']=True
        except BaseException as error:
            report.update(status='failed',identity_error=repr(error))
            raise
        finally:
            report['finished_utc']=datetime.now(timezone.utc).isoformat()
            save()
    print('MICROPHYSICS_PHASE_PASS '+a.phase,flush=True)


if __name__=='__main__': main()
