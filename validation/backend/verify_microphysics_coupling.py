"""Burn + diffusion + Hydro + ENUC-driven AMR, with source-aware global balance.

The six combinations reuse the production BurnGradient/Helm/aprox13 case and
the unchanged restart field budget. Species diffusion is explicitly enabled;
all runs retain the same Strang sequence, ODE tolerances and refinement policy.
This is additional coupled coverage, not a substitute for canonical references.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT/'validation/network'))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance


def cases(all_transport=False):
    return [dict(id=f'coupled_{method}_{order.lower()}'+('_all_transport' if all_transport else ''),problem='BurnGradient',
        input='validation/amr/inputs/burn_enuc_amr.par',accepted_steps=[],scientific_time=1e-10,
        timeout_seconds=1800,overrides=dict(ode_solver=method,use_diffusion='true',
            use_species_diff='true',use_thermal_diff=str(all_transport).lower(),use_viscous_diff=str(all_transport).lower(),
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


def checkpoint_enuc_limits(parameter):
    """Read the next-step ceilings already owned by the checkpoint schema."""
    import h5py
    values=runtime.read_parameter_map(parameter)
    paths=list(parameter.parent.glob(values['base_name']+'_chk_*.h5'))
    if values.get('restart','false').lower()=='true' and values.get('restart_file'):
        paths.append(Path(values['restart_file']))
    limits={}
    for path in paths:
        with h5py.File(path) as checkpoint:
            attributes=checkpoint.attrs
            limits[int(attributes['step'])+1]=(float(attributes['dt_burn']),
                                                float(attributes['dt_old']))
    return limits


def active_enuc_steps(transcript, limits, growth):
    """Match accepted timesteps to saved ENUC ceilings, not Strang half-steps.

    The console's dt_burn column is the executed burn half-step. Its next-step
    ceiling lives in checkpoint metadata. Match that ceiling at the precision
    actually printed for dt, and require it to be below CFL, the explicit
    diffusion scale and the timestep-growth ceiling. This is a control-flow
    witness; scientific comparisons retain their existing independent budgets.
    """
    matches=[]
    for line in transcript.splitlines():
        fields=line.split()
        if len(fields)!=6 or not fields[0].isdigit():
            continue
        try:
            step=int(fields[0])
            time,dt,hydro,burn_half_step,diffusion=map(float,fields[1:])
        except ValueError:
            continue
        if not all(math.isfinite(v) for v in (time,dt,hydro,burn_half_step,diffusion)):
            raise ArithmeticError('nonfinite coupled timestep diagnostic')
        if step not in limits:
            continue
        ceiling,previous=limits[step]
        if not all(math.isfinite(v) for v in (ceiling,previous,growth)) or growth<=0:
            raise ArithmeticError('invalid checkpoint timestep controller')
        # DriverControl uses scientific notation; infer its precision instead
        # of adding a new floating-point acceptance tolerance.
        mantissa=fields[2].lower().split('e')[0]
        precision=len(mantissa.split('.')[1]) if '.' in mantissa else 0
        printed_ceiling=float(format(ceiling,f'.{precision}e'))
        if step>1 and 0<dt==printed_ceiling<min(hydro,diffusion) and ceiling<previous*growth:
            matches.append(step)
    if not matches:
        raise RuntimeError('no accepted timestep is bound by the ENUC limiter')
    return matches


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
    parser.add_argument('--restart',action='store_true',help='replay original cross-backend split-run protocol on all coupled inputs')
    parser.add_argument('--all-transport',action='store_true',help='also enable physical Helm thermal/viscous transport, without constant coefficients')
    parser.add_argument('--active-enuc-factor',type=float,
        help='positive ENUC factor for a short restart witness; requires --restart and --terminal-time')
    parser.add_argument('--terminal-time',type=float,
        help='common physical endpoint for the active-ENUC restart witness')
    args=parser.parse_args()
    if args.active_enuc_factor is not None and (not args.restart or
            not math.isfinite(args.active_enuc_factor) or args.active_enuc_factor<=0 or
            args.terminal_time is None):
        parser.error('--active-enuc-factor requires --restart, --terminal-time and a finite positive value')
    if args.terminal_time is not None and (args.active_enuc_factor is None or
            not math.isfinite(args.terminal_time) or args.terminal_time<=0):
        parser.error('--terminal-time requires --active-enuc-factor and a finite positive value')
    build,out=args.build_dir.resolve(),args.output_dir.resolve()
    out.mkdir(parents=True,exist_ok=False)
    selected=runtime.select_cases(dict(cases=cases(args.all_transport)),args.case)
    if args.active_enuc_factor is not None:
        for case in selected:
            case['overrides']['enucDtFactor']=repr(args.active_enuc_factor)
    arch,validator=build/'bin/ARCH',build/'arch_cuda_single_level_validation'
    identity_args=dict(arch=arch,checkpoint_validator=validator,source_root=ROOT,build_dir=build)
    before=provenance.capture(**identity_args)
    report=dict(status='running',release_qualified=False,started_utc=datetime.now(timezone.utc).isoformat(),
        recipe=provenance.file_identity(Path(__file__)),cases=[],derived_cases=selected,restart=args.restart,
        runtime_inputs=runtime.runtime_case_inputs(selected,ROOT))
    def save():
        (out/'evidence.json').write_text(json.dumps(report,indent=2,default=str)+'\n')
    save()
    try:
        for case in selected:
            if args.restart:
                directory=out/case['id']
                directory.mkdir()
                parameter=directory/'coupled.par'
                runtime.render_parameter_file(ROOT/case['input'],parameter,backend='cpu',
                    output_dir=directory/'unused',base_name='CoupledRestart',accepted_steps=4,
                    scientific_overrides=case['overrides'])
                command=[sys.executable,str(ROOT/'tools/validate_cuda_amr_restart.py'),
                    '--arch',str(arch),'--checkpoint-validator',str(validator),
                    '--source-root',str(ROOT),'--build-dir',str(build),'--problem',case['problem'],
                    '--input',str(parameter),'--output-root',str(directory/'runs')]
                if args.terminal_time is not None:
                    command.extend(['--terminal-time',repr(args.terminal_time)])
                row=dict(id=case['id'],command=command,parameter=provenance.file_identity(parameter),status='running')
                report['cases'].append(row)
                save()
                with (directory/'stdout.log').open('w') as stdout,(directory/'stderr.log').open('w') as stderr:
                    rc=subprocess.run(command,cwd=ROOT,stdout=stdout,stderr=stderr,timeout=1800).returncode
                row.update(returncode=rc,status='passed' if rc==0 else 'failed')
                if rc: raise RuntimeError('coupled restart failed; complete logs retained')
                evidence=json.loads((directory/'runs/restart-validation-evidence.json').read_text())
                row['schedules']=[]
                for lane in [*evidence['continuous'].values(),*evidence['sources'].values(),*evidence['resumed']]:
                    runtime.validate_resolved_plan(Path(lane['plan']),lane['backend'],case['plan_policy'])
                    checkpoint_quality(Path(lane['checkpoint']))
                    if args.active_enuc_factor is not None:
                        par=Path(lane['parameter'])
                        row.setdefault('active_enuc',[]).append(dict(lane=lane['name'],
                            steps=active_enuc_steps((par.parent/'arch.stdout').read_text(),
                                checkpoint_enuc_limits(par),
                                float(runtime.read_parameter_map(par)['tstep_change_factor']))))
                        if lane['name'].endswith('_continuous'):
                            regrids=runtime.read_regrid_metrics(
                                par.with_name(par.stem+'_regrid.tsv'),lane['backend'],lane['steps'])
                            if not any(r['macro_step']>0 and r['topology_changed'] for r in regrids['records']):
                                raise RuntimeError('active ENUC witness did not change runtime topology')
                            row.setdefault('active_regrids',{})[lane['backend']]=regrids
                    if lane['backend']!='cuda': continue
                    start=lane.get('restored_from',{}).get('step',0)
                    order=1 if case['overrides']['diff_integrator']=='RKL1' else 2
                    par=Path(lane['parameter'])
                    summary=runtime.validate_cuda_diffusion_schedule(
                        par.parent/(par.stem+'_diffusion_schedule.tsv'),lane['run_completed_steps']-start,
                        dict(order=order,lanes_per_step=2,first_macro_step=start,
                            allowed_stages=[1,2,3,4,5] if order==1 else [2,3,5]))
                    row['schedules'].append(dict(lane=lane['name'],summary=summary))
                if row['parameter']!=provenance.file_identity(parameter):
                    raise RuntimeError('coupled restart input changed')
                save()
                print('COUPLED_RESTART_PASS '+case['id'],flush=True)
                continue
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
