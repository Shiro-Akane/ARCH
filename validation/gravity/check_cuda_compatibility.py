#!/usr/bin/env python3
"""Production CUDA gravity qualification: independent physics, parity and restart.
The existing external-gravity checks remain in this single entry point.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import statistics
import threading
import time

import h5py
import numpy as np
from gravity_box import BoxCampaign
from user_boundaries import UserBoundaryCampaign

ROOT = Path(__file__).resolve().parents[2]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def benchmark(executable, output, terminal_time=.1, only=None, repeats=5,
              cpu_executable=None, baseline_cpu=None, cuda_host_threads=1):
    """Alternating complete physical-time runs; same binary for CPU/GPU ratios.

    An optional frozen CPU binary adds a separate same-compiler regression
    pair. That pair never replaces the same-binary backend speedup denominator.
    """
    records, summaries, device_memory = [], [], []
    stop = threading.Event()
    def monitor():
        while not stop.is_set():
            result = subprocess.run(['nvidia-smi','--query-gpu=memory.used,temperature.gpu,utilization.gpu,power.draw',
                                     '--format=csv,noheader,nounits'],
                                    capture_output=True,text=True,timeout=10)
            if result.returncode == 0:
                values=result.stdout.splitlines()[0].split(',')
                device_memory.append(dict(time=time.time(),used_mib=float(values[0]),
                    temperature_c=float(values[1]),utilization_percent=float(values[2]),power_w=float(values[3])))
            stop.wait(.5)
    thread = threading.Thread(target=monitor,daemon=True)
    thread.start()
    cases = [
        ('small-periodic',dict(nblockx1=4)),
        ('medium-periodic',dict(nblockx1=2,nblockx2=2,nblockx3=2,x2_max=1e8,x3_max=1e8)),
        ('large-periodic',dict(nblockx1=4,nblockx2=4,nblockx3=4,x2_max=1e8,x3_max=1e8,max_blocks=128)),
        ('large-isolated-amr',BoxCampaign.cloud_config(roots=2,width=.06,center_x=.22,center_y=.22,center_z=.22,
            lrefinemax=1,refine_threshold=.1,derefine_threshold=.01))]
    if only == 'large-user-boundary':
        cases = [('large-user-boundary', dict(nblockx1=4,nblockx2=4,nblockx3=4,
                                              max_blocks=128,dimension=3))]
    images={'cpu':executable,'cuda':executable}
    if baseline_cpu:
        images.update({'baseline-cpu':baseline_cpu,'candidate-cpu':cpu_executable})
    try:
        for name,config in cases:
            if only and name!=only:continue
            if name == 'large-user-boundary':
                require(not baseline_cpu, 'the previous release has no UserBoundary model; benchmark its built-in BC separately')
            config.update(max_steps=-1,tmax=terminal_time)
            reference=None
            def run(label,count,sample):
                nonlocal reference
                backend='cuda' if label=='cuda' else 'cpu'
                if name == 'large-user-boundary':
                    campaign=UserBoundaryCampaign(images[label],output/name,backend,count,measure_resources=True)
                    _,_,end,_,record=campaign.run(sample,**config)
                    final=end['state'] | dict(time=end['time'])
                else:
                    campaign=BoxCampaign(images[label],output/name,backend,count,True)
                    data,folder,record=campaign.run(sample,**config)
                    final=data[-1]
                require(abs(final['time']-terminal_time)<=8*np.finfo(float).eps*terminal_time,
                        sample+': benchmark did not reach the shared physical endpoint')
                record.update(lane=label,final_time=final['time'],physical_time=terminal_time)
                if reference is None:reference=final
                for key in reference:
                    require(np.allclose(final[key],reference[key],rtol=2e-11,atol=2e-13),
                            sample+': final-state parity failed for '+key)
                records.append(record)
                return record
            sweep=[]
            for count in [1,4,8]:
                sweep.append(run('cpu',count,'warmup-cpu-'+str(count)))
            best=min(sweep,key=lambda r:r['elapsed_seconds'])['threads']
            warm=run('cuda',cuda_host_threads,'warmup-cuda')
            if baseline_cpu:
                run('baseline-cpu',best,'warmup-baseline-cpu')
                run('candidate-cpu',best,'warmup-candidate-cpu')
            samples={label:[] for label in images}
            for repeat in range(repeats):
                order=list(images) if repeat%2==0 else list(reversed(images))
                for label in order:
                    samples[label].append(run(label,cuda_host_threads if label=='cuda' else best,label+'-'+str(repeat)))
            groups={}
            for label,values in samples.items():
                keys=['elapsed_seconds','driver_seconds','output_seconds','setup_seconds','solve_seconds',
                      'source_boundary_seconds','poisson_seconds','force_seconds','stage_poisson_seconds','peak_rss_kib']
                groups[label]={key:dict(median=statistics.median(r[key] for r in values),
                    minimum=min(r[key] for r in values),maximum=max(r[key] for r in values)) for key in keys}
            speedup={key:groups['cpu'][key]['median']/groups['cuda'][key]['median']
                     for key in ['elapsed_seconds','solve_seconds','poisson_seconds','stage_poisson_seconds']}
            summary=dict(name=name,cells=warm['cells'],cpu_threads=best,cuda_host_threads=cuda_host_threads,physical_time=terminal_time,
                         repeats=repeats,groups=groups,speedup=speedup)
            if baseline_cpu:
                summary['candidate_over_baseline_cpu']=groups['candidate-cpu']['elapsed_seconds']['median']/groups['baseline-cpu']['elapsed_seconds']['median']
            summaries.append(summary)
            print(name,speedup,flush=True)
    finally:
        stop.set();thread.join(timeout=15)
        report=dict(executable=str(executable),sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                    status='passed' if len(summaries)==sum(1 for name,_ in cases if not only or name==only) else 'incomplete',
                    protocol='warmup, CPU thread selection, alternating complete physical endpoints',
                    images={label:dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for label,path in images.items()},
                    device_memory_scope='whole-device usage including baseline, not per-process allocation',
                    device_memory=device_memory,results=records,summaries=summaries)
        (output/'performance.json').write_text(json.dumps(report,indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu-arch', type=Path, required=True)
    parser.add_argument('--cuda-arch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--benchmark-only', action='store_true', help='run local performance matrix instead of qualification')
    parser.add_argument('--benchmark-time', type=float, default=.1, help='common physical endpoint in seconds; no accepted-step cap')
    parser.add_argument('--benchmark-repeats', type=int, default=5, help='at least three alternating measured runs per lane')
    parser.add_argument('--benchmark-cuda-host-threads', type=int, default=1,
                        help='OpenMP threads for Host callbacks/initialization in the CUDA lane; default retains serial Host measurements')
    parser.add_argument('--baseline-cpu-arch', type=Path, help='optional frozen same-compiler CPU image for the separate regression pair')
    parser.add_argument('--benchmark-case', choices=['small-periodic','medium-periodic','large-periodic','large-user-boundary','large-isolated-amr'])
    args = parser.parse_args()
    args.output = args.output.resolve()
    require(not args.output.exists() or not any(args.output.iterdir()),
            'choose a new or empty local output directory')
    args.output.mkdir(parents=True, exist_ok=True)
    if args.benchmark_only:
        require(math.isfinite(args.benchmark_time) and args.benchmark_time>0,
                'benchmark physical endpoint must be finite and positive')
        require(args.benchmark_repeats>=3,'benchmark needs at least three alternating repetitions')
        require(args.benchmark_cuda_host_threads>=1,'CUDA Host execution requires a positive thread count')
        benchmark(args.cuda_arch.resolve(),args.output,args.benchmark_time,args.benchmark_case,
                  args.benchmark_repeats,args.cpu_arch.resolve(),
                  args.baseline_cpu_arch.resolve() if args.baseline_cpu_arch else None,
                  args.benchmark_cuda_host_threads)
        return
    base = {}
    for line in (ROOT/'simulation/JeansWave/JeansWave.par').read_text().splitlines():
        if '=' in line and not line.startswith('#'):
            k, v = line.split('=', 1)
            base[k] = v
    records = []

    def run(name, executable, case='ExternalGravity', **changes):
        folder = args.output/name
        folder.mkdir(exist_ok=True)
        config = base | dict(gravity_type='external', gravity_g_x=.7, rho0=1., pressure0=10.,
                             velocity_x0=.2, nblockx1=2, time_integrator='RK2', cfl=.4,
                             tmax=.1, out_dir=str(folder), base_name='compat') | changes
        path = folder/'input.par'
        path.write_text('\n'.join(f'{k}={v}' for k, v in config.items())+'\n')
        result = subprocess.run([str(executable.resolve()), case, str(path)], cwd=ROOT,
                                env=os.environ | {'OMP_NUM_THREADS':'1'}, capture_output=True, text=True, timeout=180)
        (folder/'run.log').write_text(result.stdout+result.stderr)
        return folder, result

    def inspect(folder, cuda=False):
        path = sorted(folder.glob('*chk*.h5'))[-1]
        with h5py.File(path) as f:
            require(int(f.attrs['checkpoint_version'])==7, 'checkpoint version')
            require(bool(f.attrs.get('boundary_identity', '')), 'checkpoint boundary identity')
            require(f.attrs['gravity_type']=='external', 'gravity identity')
            require(np.array_equal(f['gravity_controls'][:],[.7,0.,0.]), 'external controls')
            time = float(f.attrs['time'])
            velocity = .2+.7*time
            expected = {'rho':1., 'mom_u':velocity, 'mom_v':0., 'mom_w':0., 'eng':15.+.5*velocity**2}
            error = 0.
            for key, reference in expected.items():
                current = float(np.max(np.abs(f['Data/'+key][:]-reference))/max(1.,abs(reference)))
                error = max(error,current)
                require(current<1e-12, key+': analytic uniform acceleration')
            require(f['state_repairs'][0]==0., 'unexpected repair')
        kernels = 0
        if cuda:
            plan = (folder/'compat_backend_plan.txt').read_text()
            require('resolved=cuda\n' in plan, 'GPU run silently used CPU')
            with (folder/'compat_backend_trace.tsv').open() as stream:
                kernels = sum(int(row['kernel_count']) for row in csv.DictReader(stream,delimiter='\t'))
            require(kernels>0, 'no actual device kernel execution')
        records.append(dict(name=folder.name,time=time,max_relative_error=error,device_kernels=kernels))
        return path

    cpu, result = run('cpu-external',args.cpu_arch,compute_backend='cpu')
    require(result.returncode==0,'CPU external failed')
    inspect(cpu)
    gpu, result = run('cuda-external',args.cuda_arch,compute_backend='cuda')
    require(result.returncode==0,'CUDA external failed')
    inspect(gpu,True)
    short, result = run('cpu-checkpoint',args.cpu_arch,compute_backend='cpu',max_steps=4)
    require(result.returncode==0,'CPU checkpoint failed')
    checkpoint = inspect(short)
    resumed, result = run('cuda-resumed',args.cuda_arch,compute_backend='cuda',restart='true',restart_file=str(checkpoint))
    require(result.returncode==0,'CUDA restart failed')
    inspect(resumed,True)
    cpu_box=BoxCampaign(args.cuda_arch,args.output/'self-cpu','cpu')
    gpu_box=BoxCampaign(args.cuda_arch,args.output/'self-cuda','cuda')
    def compare(left,right,name):
        errors={}
        for key in left:
            if key in ['time','level','volume']: continue
            require(left[key].shape==right[key].shape,name+': layout mismatch '+key)
            scale=max(float(np.max(abs(left[key]))),1e-100)
            error=float(np.max(abs(left[key]-right[key])))/scale
            require(error<=2e-8+2e-10,name+': normalized parity '+key)
            errors[key]=error
        records.append(dict(name=name,normalized_max_errors=errors))
    for dimension in [1,2,3]:
        changes=dict(nblockx1=2,nblockx2=int(dimension>=2),nblockx3=int(dimension==3),
                     x2_max=5e7,x3_max=5e7,max_steps=2)
        x,_,_=cpu_box.run(f'periodic-{dimension}d',**changes)
        y,_,_=gpu_box.run(f'periodic-{dimension}d',**changes)
        compare(x[-1],y[-1],f'periodic-{dimension}d')
    for label,changes in [('isolated',{}),('isolated-evolve',dict(tmax=10.,max_steps=2)),
                          ('isolated-amr',dict(roots=2,width=.06,center_x=.22,center_y=.22,center_z=.22,
                                               lrefinemax=1,refine_threshold=.1,derefine_threshold=.01))]:
        x,_,_=cpu_box.cloud(label,**changes)
        y,_,_=gpu_box.cloud(label,**changes)
        compare(x[-1],y[-1],label)
    for campaign in [cpu_box,gpu_box]:
        campaign.diffusion_reference()
        a,_,_=campaign.coupled('burn',False,**campaign.compact_config())
        b,_,record=campaign.coupled('combined',True,**campaign.compact_config())
        effect=max(float(np.max(abs(a[-1][k]-b[-1][k])))/max(float(np.max(abs(a[-1][k]))),1e-100)
                   for k in ['TEMP','ENER','c12'])
        require(effect>1e-8,'combined transport inactive');record['transport_relative_effect']=effect
    for name in ['thermal-linear-4','burn','combined']:
        from gravity_box import load
        config=next(r['config'] for r in cpu_box.results if r['name']==name)
        a=load(sorted((cpu_box.output/name).glob('*plt*.h5'))[-1],config)
        b=load(sorted((gpu_box.output/name).glob('*plt*.h5'))[-1],config)
        compare(a,b,name)
    coupled_amr=cpu_box.compact_config(nblockx1=8,lrefinemax=1,refine_threshold=5e-4,
                                      derefine_threshold=1e-4,chk_dstep=4,tmax=2.5e-11)
    x,folder,_=cpu_box.coupled('combined-amr',True,**coupled_amr)
    y,_,_=gpu_box.coupled('combined-amr',True,**coupled_amr)
    require(len(set(x[0]['level']))>1,'combined AMR lacks a coarse/fine interface')
    compare(x[-1],y[-1],'combined-amr')
    checkpoint=sorted(folder.glob('*chk*.h5'))[1]
    resumed=cpu_box.burning_config(**coupled_amr)|dict(use_diffusion='true',use_thermal_diff='true',
        use_species_diff='true',diff_integrator='RKL2',restart='true',restart_file=str(checkpoint))
    y,_,_=gpu_box.run('combined-amr-resumed',**resumed)
    compare(x[-1],y[-1],'combined-amr-cpu-to-cuda-restart')
    for backend in ['cpu','cuda']:
        folder,result=run('reject-self-convergence-'+backend,args.cuda_arch,case='JeansWave',
            gravity_type='self',compute_backend=backend,gravity_max_cycles=1,gravity_rtol=1e-14)
        require(result.returncode!=0 and 'Poisson solve failed' in result.stdout+result.stderr,
                backend+': nonconverged self gravity was not rejected')
        require(not list(folder.glob('*plt*.h5')),backend+': failed self field published')
        records.append(dict(name='reject-self-convergence-'+backend,rejected=True))
    # Native checkpoint remains backend independent; phi is recomputed from
    # the resumed density, and no old device allocation is serialized.
    x,_,_=cpu_box.run('continuous',tmax=.03)
    _,folder,_=cpu_box.run('checkpoint',tmax=.03,max_steps=2)
    checkpoint=sorted(folder.glob('*chk*.h5'))[-1]
    y,_,_=gpu_box.run('resumed',tmax=.03,restart='true',restart_file=str(checkpoint))
    compare(x[-1],y[-1],'cpu-to-cuda-self-restart')
    records.extend(cpu_box.results+gpu_box.results)
    report = dict(status='passed',scope='external and self gravity; Cartesian periodic/isolated, coupled physics and restart',
                  cpu_sha256=hashlib.sha256(args.cpu_arch.read_bytes()).hexdigest(),
                  cuda_sha256=hashlib.sha256(args.cuda_arch.read_bytes()).hexdigest(),results=records)
    (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print('CUDA gravity qualification passed: external/self, independent physics, AMR, coupling and portable restart')


if __name__=='__main__':
    main()
