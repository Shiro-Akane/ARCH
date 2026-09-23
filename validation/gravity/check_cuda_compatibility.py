#!/usr/bin/env python3
"""Production CUDA gravity qualification: independent physics, parity and restart.
The existing external-gravity checks remain in this single entry point.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import statistics
import threading
import time

import h5py
import numpy as np
from gravity_box import BoxCampaign

ROOT = Path(__file__).resolve().parents[2]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def benchmark(executable, output, steps=2, only=None):
    """Same Release binary, warmup + three samples; no speedup pass threshold."""
    records, summaries, device_memory = [], [], []
    stop = threading.Event()
    def monitor():
        while not stop.is_set():
            result = subprocess.run(['nvidia-smi','--query-gpu=memory.used','--format=csv,noheader,nounits'],
                                    capture_output=True,text=True,timeout=10)
            if result.returncode == 0:
                device_memory.append(dict(time=time.time(),used_mib=float(result.stdout.splitlines()[0])))
            stop.wait(.2)
    thread = threading.Thread(target=monitor,daemon=True)
    thread.start()
    cases = [
        ('small-periodic',dict(nblockx1=4,max_steps=2)),
        ('medium-periodic',dict(nblockx1=2,nblockx2=2,nblockx3=2,x2_max=1e8,x3_max=1e8,max_steps=2)),
        ('large-periodic',dict(nblockx1=4,nblockx2=4,nblockx3=4,x2_max=1e8,x3_max=1e8,max_steps=2,max_blocks=128)),
        ('large-isolated-amr',BoxCampaign.cloud_config(roots=2,width=.06,center_x=.22,center_y=.22,center_z=.22,
            lrefinemax=1,refine_threshold=.1,derefine_threshold=.01,tmax=10.,max_steps=2))]
    try:
        for name,config in cases:
            if only and name!=only:continue
            if steps!=2:
                config.update(max_steps=steps,tmax=1. if 'periodic' in name else 1e6)
            sweep=[]
            for count in [1,4,8]:
                c=BoxCampaign(executable,output/name,'cpu',count,True)
                _,_,record=c.run('warmup-cpu-'+str(count),**config)
                records.append(record);sweep.append(record)
            best=min(sweep,key=lambda r:r['elapsed_seconds'])['threads']
            gpu=BoxCampaign(executable,output/name,'cuda',1,True)
            _,_,warm=gpu.run('warmup-cuda',**config);records.append(warm)
            groups={}
            for backend,count in [('cpu',best),('cuda',1)]:
                samples=[]
                c=BoxCampaign(executable,output/name,backend,count,True)
                for repeat in range(3):
                    _,_,r=c.run(f'{backend}-{repeat}',**config);samples.append(r);records.append(r)
                keys=['elapsed_seconds','driver_seconds','output_seconds','setup_seconds','solve_seconds',
                      'source_boundary_seconds','poisson_seconds','force_seconds','stage_poisson_seconds','peak_rss_kib']
                groups[backend]={key:dict(median=statistics.median(r[key] for r in samples),
                    minimum=min(r[key] for r in samples),maximum=max(r[key] for r in samples)) for key in keys}
            speedup={key:groups['cpu'][key]['median']/groups['cuda'][key]['median']
                     for key in ['elapsed_seconds','solve_seconds','poisson_seconds','stage_poisson_seconds']}
            summaries.append(dict(name=name,cells=warm['cells'],cpu_threads=best,groups=groups,speedup=speedup))
            print(name,speedup,flush=True)
    finally:
        stop.set();thread.join(timeout=15)
        report=dict(executable=str(executable),sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                    device_memory_scope='whole-device usage including baseline, not per-process allocation',
                    device_memory=device_memory,results=records,summaries=summaries)
        (output/'performance.json').write_text(json.dumps(report,indent=2)+'\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu-arch', type=Path, required=True)
    parser.add_argument('--cuda-arch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--benchmark-only', action='store_true', help='run local performance matrix instead of qualification')
    parser.add_argument('--benchmark-steps', type=int, default=2, help='positive macro-step limit for performance runs')
    parser.add_argument('--benchmark-case', choices=['small-periodic','medium-periodic','large-periodic','large-isolated-amr'])
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.benchmark_only:
        require(args.benchmark_steps>0,'benchmark steps must be positive')
        benchmark(args.cuda_arch.resolve(),args.output,args.benchmark_steps,args.benchmark_case)
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
            require(int(f.attrs['checkpoint_version'])==6, 'checkpoint version')
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
    # Native v6 checkpoint remains backend independent; phi is recomputed from
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
    print('CUDA gravity qualification passed: external/self, independent physics, AMR, coupling and v6 restart')


if __name__=='__main__':
    main()
