#!/usr/bin/env python3
"""Scoped P3/P4 compatibility: real CUDA external gravity, v6 restart and self gate.

This does not run self gravity on a device or measure gravity GPU speedup.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu-arch', type=Path, required=True)
    parser.add_argument('--cuda-arch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
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
    rejected, result = run('cuda-self-rejected',args.cuda_arch,case='JeansWave',compute_backend='cuda',gravity_type='self',rho0=1e7,pressure0=6e6)
    require(result.returncode!=0 and 'gravity unsupported' in (result.stdout+result.stderr).lower(),'CUDA self gate missing')
    require(not list(rejected.glob('*chk*.h5')), 'CUDA self published output')
    records.append(dict(name='cuda-self-rejected',rejected=True))
    fallback, result = run('auto-self-cpu',args.cuda_arch,case='JeansWave',compute_backend='auto',gravity_type='self',rho0=1e7,pressure0=6e6,max_steps=2)
    require(result.returncode==0,'Auto CPU self fallback failed')
    require('resolved=cpu\n' in (fallback/'compat_backend_plan.txt').read_text(),'Auto did not resolve CPU')
    with h5py.File(sorted(fallback.glob('*plt*.h5'))[-1]) as f:
        require('GPOT' in f['Data'] and np.max(np.abs(f['Data/GPOT'][:]))>0., 'CPU fallback did not solve gravity')
    records.append(dict(name='auto-self-cpu',resolved='cpu'))
    report = dict(status='passed',scope='existing CUDA external gravity/checkpoint compatibility; no GPU self solve',
                  cpu_sha256=hashlib.sha256(args.cpu_arch.read_bytes()).hexdigest(),
                  cuda_sha256=hashlib.sha256(args.cuda_arch.read_bytes()).hexdigest(),results=records)
    (args.output/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print('CUDA compatibility passed: external analytic, CPU-to-GPU v6 restart, self rejection and CPU fallback')


if __name__=='__main__':
    main()
