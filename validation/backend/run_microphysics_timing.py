"""Matched full-application burn/diffusion timing; no physics or budget tuning.

Uses the existing parameter renderer, checkpoint comparator and scientific
qualifiers. Every sample is qualified after (outside) its timed ARCH process.
Pilot results are diagnostics, never a formal speedup claim. Run only when the
server is otherwise quiet; this recipe does not stop any other process.
"""
import argparse
from datetime import datetime, timezone
import itertools
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as validation
import validation_provenance as provenance


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def comparable_inputs(left,right):
    def normalize(value):
        return (value['parameter_sha256'],value['scientific_overrides'],value['eos_type'],
                sorted((v['parameter'],v['sha256']) for v in value['dependencies']))
    return left.keys() == right.keys() and all(normalize(left[k]) == normalize(right[k]) for k in left)


def make_cases(modules, blocks):
    inventory = json.loads((ROOT / 'validation/backend/cases.json').read_text())['cases']
    cases = []
    for module, count in itertools.product(modules, blocks):
        require(count > 0, 'block count must be positive')
        if module.startswith('coupled_'):
            import verify_microphysics_coupling as coupled
            selected=[c for c in coupled.cases(module.endswith('_all_transport')) if c['id']==module]
            require(len(selected)==1,'unknown coupled method')
            case=selected[0]
            case['id']=f'{module}_b{count}'
            case['overrides'].update(nblockx1=str(count),max_blocks=str(max(128,8*count)))
            cases.append(case)
            continue
        if module.startswith('burn_'):
            method = module.removeprefix('burn_')
            require(method in ('be_nr', 'bd', 'ros4'), 'unknown burn method')
            case = dict(id=f'{module}_b{count}', problem='BurnOneZone',
                input='validation/burn/inputs/bd.par', accepted_steps=[], scientific_time=1e-10,
                overrides=dict(ode_solver=method),
                plan_policy=dict(network='aprox13', eos='helmholtz', ode=method, linear='denselu'),
                reduction_policy=dict(rtol=2e-8, atol=1e-12),
                qualification=dict(reference='cpu_and_network_conservation',
                    positive=['rho', 'eng'], species_sum_atol=1e-12))
        else:
            require(module in ('diffusion_rkl1', 'diffusion_rkl2'), 'unknown diffusion method')
            original = next(v for v in inventory if v['id'] == module + '_n128')
            case = {k: v for k, v in original.items()
                    if k not in ('resolution', 'qualification_group')}
            case.update(id=f'{module}_b{count}', accepted_steps=[], overrides={})
            # Pointwise analytic budgets remain original; the separate canonical
            # 64/128/256 regression, not this scale scan, qualifies convergence.
        case['overrides'].update(nblockx1=str(count), max_blocks=str(max(8,2*count)),
                                 lrefinemin='0', lrefinemax='0')
        cases.append(case)
    return cases


def lane_configurations(threads, baseline_threads=None, gpu_threads=None):
    """Keep a complete CPU/cuda pair per version while avoiding redundant GPU scans."""
    baseline_threads = threads if baseline_threads is None else baseline_threads
    gpu_selection = threads if gpu_threads is None else [gpu_threads]
    for selection in (threads, baseline_threads, gpu_selection):
        require(selection and min(selection) > 0 and len(selection) == len(set(selection)),
                'thread selections must be positive, nonempty and unique')
    return [(v,backend,t) for v in ('baseline','candidate') for backend in ('cpu','cuda')
            for t in (gpu_selection if backend == 'cuda' else
                      baseline_threads if v == 'baseline' else threads)]


def timed_process(command, cwd, env, directory, timeout):
    expired = False
    with (directory / 'arch.stdout').open('w') as stdout, (directory / 'arch.stderr').open('w') as stderr:
        start = time.perf_counter()
        proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr)
        def kill_at_deadline():
            nonlocal expired
            if proc.poll() is None:
                expired = True
                proc.kill()
        timer = threading.Timer(timeout, kill_at_deadline)
        timer.daemon = True
        timer.start()
        try:
            rc = proc.wait()
        except BaseException:
            proc.kill()
            proc.wait()
            raise
        finally:
            elapsed = time.perf_counter() - start
            timer.cancel()
    return dict(returncode=rc, timed_out=expired, arch_wall_seconds=elapsed)


def burn_balance(initial, final):
    """Source-aware first law from independent nuclear data, not ARCH's RHS."""
    import h5py
    import numpy as np
    sys.path.insert(0, str(ROOT / 'validation/network'))
    import nse_reference
    data = nse_reference.nuclear_data('aprox13')
    mass = np.asarray(data['arrays']['AION'], dtype=np.longdouble)
    binding = np.asarray(data['arrays']['BION'], dtype=np.longdouble) / mass
    charge = np.asarray(data['arrays']['ZION'], dtype=np.longdouble) / mass
    def state(path):
        with h5py.File(path) as f:
            rho = f['Data/rho'][:].astype(np.longdouble)
            require(np.all(rho == 1e7), 'one-zone density changed')
            require(all(np.all(f['Data/'+v][:] == 0) for v in ('mom_u','mom_v','mom_w')),
                    'uniform one-zone acquired momentum')
            return f['Data/X'][:].astype(np.longdouble), f['Data/eng'][:].astype(np.longdouble)/rho
    x0, e0 = state(initial)
    x1, e1 = state(final)
    dx = x1-x0
    shape = (-1,) + (1,)*(dx.ndim-1)
    q = np.sum(dx*binding.reshape(shape),axis=0)*np.longdouble(data['energy_conversion'])
    error = float(np.max(np.abs(e1-e0-q)/np.maximum(np.maximum(np.abs(e0),np.abs(e1)),np.abs(q))))
    ye = float(np.max(np.abs(np.sum(dx*charge.reshape(shape),axis=0))))
    evolution = float(np.max(np.abs(dx)))
    require(all(math.isfinite(v) for v in (error,ye,evolution)), 'nonfinite burn balance')
    require(error <= 1e-12 and ye <= 1e-12, 'original energy/charge budget failed')
    require(evolution > 1e-9, 'timing input did not measurably burn')
    return dict(energy_relative=error, charge_absolute=ye, max_species_evolution=evolution,
                energy_budget=1e-12, charge_budget=1e-12, nuclear_data=data)


def run_lane(args, case, version, backend, threads, phase, repeat, record, save):
    source, build = args.versions[version]
    directory = args.output_root / case['id'] / f'{phase}-{repeat}-{version}-{backend}-t{threads}'
    directory.mkdir(parents=True)
    par = directory / 'run.par'
    validation.render_terminal_parameter_file(source/case['input'],par,backend=backend,
        output_dir=directory,base_name='MicrophysicsTiming',terminal_time=case['scientific_time'],
        scientific_overrides=case['overrides'])
    env = dict(os.environ,OMP_NUM_THREADS=str(threads),OMP_DYNAMIC='FALSE',OMP_PLACES='cores',OMP_PROC_BIND='close')
    if args.preload and backend == 'cuda': env['LD_PRELOAD']=str(args.preload)
    record.update(case=case['id'],version=version,backend=backend,threads=threads,phase=phase,repeat=repeat,
        directory=str(directory),parameter=provenance.file_identity(par),status='running',
        effective_parameters=validation.read_parameter_map(par),
        environment={k:env.get(k) for k in ('OMP_NUM_THREADS','OMP_DYNAMIC','OMP_PLACES','OMP_PROC_BIND','LD_PRELOAD')},
        command=[str(build/'bin/ARCH'),case['problem'],str(par)])
    save()
    record.update(timed_process(record['command'],source,env,directory,args.timeout))
    save()
    require(record['returncode'] == 0 and not record['timed_out'], f'ARCH failed: {directory}')
    require(provenance.file_identity(par) == record['parameter'], 'parameter identity changed')
    initial, final = [directory/f'MicrophysicsTiming_chk_{i:04d}.h5' for i in (0,1)]
    require(sorted(directory.glob('*_chk_*.h5')) == [initial,final], 'unexpected checkpoint count')
    validator = build/'arch_cuda_single_level_validation'
    meta = validation.checkpoint_metadata(validator=validator,checkpoint=final,parameters=par,expected_steps=None)
    require(meta['time'] == case['scientific_time'] and meta['step'] > 0, 'wrong terminal step/time')
    match = validation.STEP_RE.search((directory/'arch.stdout').read_text())
    require(match is not None and int(match.group(1)) == meta['step'], 'stdout step mismatch')
    record.update(checkpoint=str(final),initial_checkpoint=str(initial),metadata=meta,
        checkpoint_identity=provenance.file_identity(final),
        plan=validation.validate_resolved_plan(directory/'MicrophysicsTiming_backend_plan.txt',backend,case.get('plan_policy')),
        qualification=validation.qualify_checkpoint(validator,final,case,scientific=True,parameter_file=par),
        regrid=validation.read_regrid_metrics(directory/'MicrophysicsTiming_regrid.tsv',backend,meta['step']))
    if backend == 'cuda':
        record['trace'] = validation.validate_cuda_trace(directory/'MicrophysicsTiming_backend_trace.tsv',meta['step'])
    if case['problem'] == 'BurnOneZone':
        record['source_balance'] = burn_balance(initial,final)
    elif case['problem'] == 'BurnGradient':
        import verify_microphysics_coupling as coupled
        record['pointwise_quality']=coupled.checkpoint_quality(final)
        record['source_balance']=coupled.balance(validator,initial,final,par)
        changes=[v for v in record['regrid']['records'] if v['macro_step']>0 and v['topology_changed']]
        require(bool(changes),'coupled timing did not exercise runtime regrid')
        if backend=='cuda':
            order=1 if case['overrides']['diff_integrator']=='RKL1' else 2
            record['diffusion_schedule']=validation.validate_cuda_diffusion_schedule(
                directory/'MicrophysicsTiming_diffusion_schedule.tsv',meta['step'],
                dict(order=order,lanes_per_step=2,allowed_stages=[1,2,3,4,5] if order==1 else [2,3,5]))
    record['status'] = 'passed'
    save()


def compare(case, reference, candidate, validator):
    result = validation.compare_hdf5_checkpoints(Path(reference['checkpoint']),Path(candidate['checkpoint']),
        case['reduction_policy'],validator,comparison_mode='physical-time',target_time=case['scientific_time'])
    excluded = {'compute_backend','out_dir'}
    params = [{k:v for k,v in lane['effective_parameters'].items() if k not in excluded}
              for lane in (reference,candidate)]
    fields = ('macro_step','old_blocks','new_blocks','topology_changed')
    regrids = [[tuple(v[k] for k in fields) for v in lane['regrid']['records']]
               for lane in (reference,candidate)]
    times = [[v['physical_time'] for v in lane['regrid']['records']] for lane in (reference,candidate)]
    times_match = len(times[0]) == len(times[1]) and all(math.isclose(a,b,rel_tol=1e-12) for a,b in zip(*times))
    aligned = reference['metadata']['step'] == candidate['metadata']['step'] and params[0] == params[1] and regrids[0] == regrids[1] and times_match
    require(result['passed'] and aligned, 'numeric/work alignment failed; sample retained, not qualified for speed')
    return dict(reference=reference['directory'],candidate=candidate['directory'],fields=result,workload_aligned=aligned)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--candidate-source',type=Path,required=True)
    p.add_argument('--candidate-build',type=Path,required=True)
    p.add_argument('--baseline-source',type=Path,required=True)
    p.add_argument('--baseline-build',type=Path,required=True)
    p.add_argument('--output-root',type=Path,required=True)
    p.add_argument('--modules',nargs='+',default=['burn_bd','burn_ros4','diffusion_rkl1','diffusion_rkl2'])
    p.add_argument('--blocks',type=int,nargs='+',default=[8,32,128])
    p.add_argument('--threads',type=int,nargs='+',default=[1,8,16])
    p.add_argument('--warmups',type=int,default=1)
    p.add_argument('--repeats',type=int,default=5)
    p.add_argument('--timeout',type=float,default=1200)
    p.add_argument('--pilot',action='store_true')
    p.add_argument('--preload',type=Path,help='test-only CUDA API observer; requires --pilot')
    p.add_argument('--baseline-threads',type=int,nargs='+',
                   help='optional baseline CPU selection; candidate CPU still scans --threads')
    p.add_argument('--gpu-threads',type=int,
                   help='optional fixed Host OpenMP count for both CUDA lanes; default scans --threads')
    args = p.parse_args()
    require(not os.environ.get('LD_PRELOAD'), 'inherited LD_PRELOAD is not a controlled timing environment')
    require(not args.preload or args.pilot, 'instrumented runs cannot be formal speedup samples')
    if args.preload: args.preload=args.preload.resolve(strict=True)
    require(min(args.threads) > 0 and args.repeats >= 5 and args.warmups >= 1 and math.isfinite(args.timeout) and args.timeout > 0,
            'formal protocol requires positive threads/timeout, >=1 warmup and >=5 repeats')
    require(all(len(v)==len(set(v)) for v in (args.threads,args.blocks,args.modules)), 'duplicate selection')
    configurations = lane_configurations(args.threads,args.baseline_threads,args.gpu_threads)
    if args.pilot: args.warmups,args.repeats = 0,1
    args.output_root = args.output_root.resolve()
    require(args.output_root.is_relative_to(ROOT/'build') and args.output_root != ROOT/'build','output must be a new build subdirectory')
    args.output_root.mkdir(parents=True,exist_ok=False)
    args.versions = {v:(getattr(args,v+'_source').resolve(),getattr(args,v+'_build').resolve()) for v in ('baseline','candidate')}
    identities = {v:dict(source_root=s,build_dir=b,arch=b/'bin/ARCH',checkpoint_validator=b/'arch_cuda_single_level_validation')
                  for v,(s,b) in args.versions.items()}
    cases = make_cases(args.modules,args.blocks)
    report = dict(status='running',pilot=args.pilot,release_qualified=False,started_utc=datetime.now(timezone.utc).isoformat(),
        timing_scope='ARCH startup-to-exit including initial/final I/O; excludes qualification; no steady-state claim',
        cases=cases,identities_before={v:provenance.capture(**kw) for v,kw in identities.items()},
        inputs={v:validation.runtime_case_inputs(cases,s) for v,(s,b) in args.versions.items()},
        recipe=provenance.file_identity(Path(__file__)),configurations=configurations,
        lanes=[],comparisons=[],statistics=[])
    if args.preload: report['observer']=provenance.file_identity(args.preload)
    def save():
        (args.output_root/'evidence.json').write_text(json.dumps(report,indent=2,default=str)+'\n')
    save()
    try:
        require(comparable_inputs(report['inputs']['baseline'],report['inputs']['candidate']),
                'baseline and candidate input/table content differ')
        settings = ('CMAKE_BUILD_TYPE','CMAKE_CXX_COMPILER','CMAKE_CUDA_COMPILER',
            'CMAKE_CUDA_HOST_COMPILER','CMAKE_CUDA_ARCHITECTURES','CMAKE_CXX_FLAGS',
            'CMAKE_CXX_FLAGS_RELEASE','CMAKE_CUDA_FLAGS','CMAKE_CUDA_FLAGS_RELEASE',
            'CMAKE_EXE_LINKER_FLAGS','ARCH_ENABLE_CUDA','ARCH_ENABLE_CUDSS',
            'ARCH_ENABLE_KLU','ARCH_ENABLE_OPENMP','ARCH_CUSTOM_NETWORKS')
        options = {v:report['identities_before'][v]['build']['cmake_options'] for v in identities}
        require(all(options['baseline'].get(k)==options['candidate'].get(k) for k in settings),
                'baseline and candidate build/physics settings differ')
        for case in cases:
            reference = None
            for phase,count in [('warmup',args.warmups),('measured',args.repeats)]:
                for repeat in range(count):
                    order = configurations if repeat%2 == 0 else list(reversed(configurations))
                    for version,backend,threads in order:
                        lane = {}
                        report['lanes'].append(lane)
                        print(f'TIMING_START {case["id"]} {phase} {repeat} {version} {backend} t{threads}',flush=True)
                        run_lane(args,case,version,backend,threads,phase,repeat,lane,save)
                        if reference is None: reference = lane
                        else:
                            report['comparisons'].append(compare(case,reference,lane,args.candidate_build.resolve()/'arch_cuda_single_level_validation'))
                        save()
                        print(f'TIMING_PASS {lane["arch_wall_seconds"]:.6f}s',flush=True)
            for v,b,t in configurations:
                samples = [lane['arch_wall_seconds'] for lane in report['lanes'] if
                    (lane['case'],lane['version'],lane['backend'],lane['threads'],lane['phase']) == (case['id'],v,b,t,'measured')]
                report['statistics'].append(dict(case=case['id'],version=v,backend=b,threads=t,samples=samples,
                    median=statistics.median(samples),minimum=min(samples),maximum=max(samples),
                    population_stdev=statistics.pstdev(samples)))
        report['status'] = 'passed'
    except BaseException as error:
        report.update(status='interrupted' if isinstance(error,KeyboardInterrupt) else 'failed',error=repr(error))
        raise
    finally:
        try:
            report['identities_after'] = {v:provenance.capture(**kw) for v,kw in identities.items()}
            for v in identities:
                provenance.require_unchanged(report['identities_before'][v],report['identities_after'][v])
            require(report['recipe'] == provenance.file_identity(Path(__file__)), 'recipe changed')
            if args.preload:
                require(report['observer']==provenance.file_identity(args.preload),'observer changed')
            require(report['inputs'] == {v:validation.runtime_case_inputs(cases,s) for v,(s,b) in args.versions.items()},'input dependencies changed')
        except BaseException as error:
            report.update(status='failed',identity_error=repr(error))
            raise
        finally:
            report['finished_utc'] = datetime.now(timezone.utc).isoformat()
            save()


if __name__ == '__main__':
    main()
