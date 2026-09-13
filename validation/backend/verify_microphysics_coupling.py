"""Burn + diffusion + Hydro + ENUC-driven AMR, with source-aware global balance.

The six combinations reuse the production BurnGradient/Helm/aprox13 case and
the unchanged restart field budget. Species diffusion is explicitly enabled;
all runs retain the same Strang sequence, ODE tolerances and refinement policy.
This is additional coupled coverage, not a substitute for canonical references.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'validation/network'))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance


def cases():
    return [dict(id=f'coupled_{method}_{order.lower()}',problem='BurnGradient',
        input='validation/amr/inputs/burn_enuc_amr.par',accepted_steps=[],scientific_time=1e-10,
        timeout_seconds=1800,overrides=dict(ode_solver=method,use_diffusion='true',
            use_species_diff='true',use_thermal_diff='false',use_viscous_diff='false',
            diff_integrator=order,diff_max_stages='5',diff_cfl='0.8',
            lrefinemin='0',lrefinemax='1'),
        plan_policy=dict(network='aprox13',eos='helmholtz',ode=method,linear='denselu',diffusion=order.lower()),
        reduction_policy=restart.comparison_policy())
        for method in ('be_nr','bd','ros4') for order in ('RKL1','RKL2')]


def field_quality(arrays):
    """Pointwise gates accept arbitrary AMR storage shape, not a uniform mesh."""
    if not all(np.all(np.isfinite(a)) for a in arrays.values()):
        raise ArithmeticError('coupled checkpoint has nonfinite fields')
    rho,energy=arrays['rho'],arrays['eng']
    if not np.all(rho>0) or not np.all(energy>0):
        raise ArithmeticError('coupled checkpoint has nonpositive density/energy')
    fractions=arrays['rhoX']/rho
    error=float(np.max(np.abs(np.sum(fractions,axis=0)-1)))
    if error>1e-12 or np.min(fractions)<-1e-12 or np.max(fractions)>1+1e-12:
        raise ArithmeticError('coupled composition closure/bounds failed')
    return dict(positive=['rho','eng'],species_sum_absolute=error,species_sum_budget=1e-12)


def checkpoint_quality(path):
    import h5py
    with h5py.File(path) as f:
        return field_quality({key:f['Data/'+key][:] for key in
            ('rho','eng','mom_u','mom_v','mom_w','enuc_rate','rhoX')})


def balance(validator,initial,final,parameter):
    import nse_reference
    data=nse_reference.nuclear_data('aprox13')
    before=runtime.read_conservation_metrics(validator,initial,parameter)
    after=runtime.read_conservation_metrics(validator,final,parameter)
    mass=np.asarray(data['arrays']['AION'],dtype=np.longdouble)
    binding=np.asarray(data['arrays']['BION'],dtype=np.longdouble)/mass
    charge=np.asarray(data['arrays']['ZION'],dtype=np.longdouble)/mass
    # Physical-volume integrals from the shared checkpoint geometry reader.
    dx=np.asarray(after['rhoX'],dtype=np.longdouble)-np.asarray(before['rhoX'],dtype=np.longdouble)
    q=np.sum(dx*binding)*np.longdouble(data['energy_conversion'])
    e0,e1=np.longdouble(before['energy']),np.longdouble(after['energy'])
    e=float(abs(e1-e0-q)/max(abs(e0),abs(e1),abs(q)))
    y=float(abs(np.sum(dx*charge))/abs(np.longdouble(before['mass'])))
    m=float(abs(np.longdouble(after['mass'])-np.longdouble(before['mass']))/abs(np.longdouble(before['mass'])))
    if not all(np.isfinite(v) for v in (e,y,m)) or max(e,y,m)>1e-12:
        raise ArithmeticError(f'coupled first-law/mass/charge gate failed: {e}, {m}, {y}')
    if abs(q) <= 1e-12*abs(e0):
        raise ArithmeticError('coupled input did not measurably release/absorb nuclear energy')
    return dict(before=before,after=after,nuclear_data=data,energy_relative=e,mass_relative=m,
                charge_absolute=y,budget=1e-12)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--case',action='append',default=[])
    args=parser.parse_args()
    build,out=args.build_dir.resolve(),args.output_dir.resolve()
    out.mkdir(parents=True,exist_ok=False)
    selected=runtime.select_cases(dict(cases=cases()),args.case)
    arch,validator=build/'bin/ARCH',build/'arch_cuda_single_level_validation'
    identity_args=dict(arch=arch,checkpoint_validator=validator,source_root=ROOT,build_dir=build)
    before=provenance.capture(**identity_args)
    report=dict(status='running',release_qualified=False,started_utc=datetime.now(timezone.utc).isoformat(),
        recipe=provenance.file_identity(Path(__file__)),cases=[],derived_cases=selected,
        runtime_inputs=runtime.runtime_case_inputs(selected,ROOT))
    def save():
        (out/'evidence.json').write_text(json.dumps(report,indent=2,default=str)+'\n')
    save()
    try:
        for case in selected:
            record=runtime.run_case(arch,validator,ROOT,case,out/'runs')
            report['cases'].append(record)
            for backend in ('cpu','cuda'):
                lane=record['scientific'][backend]
                par=Path(lane['parameter_file'])
                initial,=par.parent.glob('*_chk_0000.h5')
                lane['pointwise_quality']=checkpoint_quality(Path(lane['checkpoint']))
                lane['source_balance']=balance(validator,initial,Path(lane['checkpoint']),par)
                changes=[r for r in lane['regrid']['records'] if r['macro_step']>0 and r['topology_changed']]
                if not changes:
                    raise RuntimeError('coupled case did not exercise runtime topology changes')
                lane['runtime_topology_changes']=len(changes)
                if backend=='cuda':
                    schedule=par.parent/(par.stem+'_diffusion_schedule.tsv')
                    # The production stage count may vary; require two complete
                    # diffusion lanes per macro step through the original checker.
                    order=1 if case['overrides']['diff_integrator']=='RKL1' else 2
                    lane['diffusion_schedule_summary']=runtime.validate_cuda_diffusion_schedule(
                        schedule,lane['steps'],dict(order=order,lanes_per_step=2,
                            allowed_stages=[1,2,3,4,5] if order==1 else [2,3,5]))
            save()
            print('COUPLED_PASS '+case['id'],flush=True)
        report['status']='passed'
    except BaseException as error:
        report.update(status='failed',error=repr(error))
        raise
    finally:
        try:
            provenance.require_unchanged(before,provenance.capture(**identity_args))
            if report['runtime_inputs'] != runtime.runtime_case_inputs(selected,ROOT):
                raise RuntimeError('coupled input identity changed')
        except BaseException as error:
            report.update(status='failed',identity_error=repr(error))
            raise
        finally:
            report['finished_utc']=datetime.now(timezone.utc).isoformat()
            save()


if __name__=='__main__': main()
