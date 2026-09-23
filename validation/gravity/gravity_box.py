"""Independent finite-cloud and coupled-physics checks for the standard GravityBox.

This module extends the existing gravity campaign; it is not another CI job.
"""
import csv
import math
import os
from pathlib import Path
import subprocess
import time

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
G = 6.67430e-8
CV = 1.2471693927e8
# Timmes aprox13 alpha-chain binding energies (MeV per nucleus). This independent
# endpoint budget does not integrate ARCH's reported ENUC diagnostic.
NUCLEI = ['he4', 'c12', 'o16', 'ne20', 'mg24', 'si28', 's32', 'ar36', 'ca40', 'ti44', 'cr48', 'fe52', 'ni56']
A = np.array([4, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56], dtype=np.longdouble)
B = np.array([28.29603, 92.16294, 127.62093, 160.64788, 198.25790, 236.53790,
              271.78250, 306.72020, 342.05680, 375.47720, 411.46900, 447.70800, 484.00300], dtype=np.longdouble)
MEV_PER_GRAM = np.longdouble('6.02214076e23') * np.longdouble('1.602176634e-6')


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def load(path, config):
    with h5py.File(path) as f:
        result = {k: f['Data/' + k][:].ravel() for k in f['Data']}
        result.update({k: f['Grid/' + k][:].ravel() for k in ['x', 'y', 'z', 'level']})
        result['time'] = float(f.attrs['time'])
        dimension = int(f.attrs['dim'])
    root_volume = math.prod((float(config[f'x{a}_max']) - float(config.get(f'x{a}_min', 0))) /
                            (16 * int(config[f'nblockx{a}'])) for a in range(1, dimension + 1))
    result['volume'] = np.repeat(root_volume * 2. ** (-dimension * result['level']),
                                 result['DENS'].size // result['level'].size).astype(np.longdouble)
    for field, value in result.items():
        require(np.all(np.isfinite(value)), str(path) + ': nonfinite ' + field)
    return result


def totals(d):
    rho = d['DENS'].astype(np.longdouble)
    volume = d['volume']
    gas = np.sum(volume * d['ENER'])
    energy = gas + np.sum(volume * rho * d.get('GPOT', 0.) / 2)
    binding = np.longdouble(0)
    if 'he4' in d:
        for i, name in enumerate(NUCLEI):
            binding += np.sum(volume * rho * d[name]) * B[i] / A[i] * MEV_PER_GRAM
    return dict(mass=np.sum(volume * rho), gas=gas, energy=energy, binding=binding)


class BoxCampaign:
    def __init__(self, executable, output, backend='cpu', threads=1, measure_resources=False):
        self.executable = Path(executable).resolve()
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.backend, self.threads = backend, threads
        self.measure_resources = measure_resources
        self.results = []
        self.base = dict(line.split('=', 1) for line in (ROOT/'simulation/GravityBox/GravityBox.par').read_text().splitlines()
                         if '=' in line and not line.startswith('#'))

    def run(self, name, *, case='GravityBox', energy_budget=1e-6, **changes):
        folder = self.output/name
        folder.mkdir(exist_ok=True)
        config = self.base | dict(compute_backend=self.backend, out_dir=str(folder),
                                  plt_variables='DENS,PRES,TEMP,VELX,VELY,VELZ,ENER,ENUC,SPECIES') | changes
        path = folder/'input.par'
        path.write_text('\n'.join(f'{k}={v}' for k, v in config.items())+'\n')
        command = [str(self.executable), case, str(path)]
        if self.measure_resources:
            command = ['/usr/bin/time', '-f', '%M', '-o', str(folder/'peak_rss_kib.txt')] + command
        started = time.perf_counter()
        with (folder/'run.log').open('w') as log:
            result = subprocess.run(command, cwd=ROOT,
                                    env=os.environ | {'OMP_NUM_THREADS':str(self.threads), 'OMP_DYNAMIC':'FALSE'},
                                    stdout=log, stderr=subprocess.STDOUT, timeout=1200)
        elapsed = time.perf_counter() - started
        require(result.returncode == 0, name+': run failed; '+str(folder/'run.log'))
        plots = [load(p, config) for p in sorted(folder.glob('*plt*.h5'))]
        require(bool(plots), name+': missing plot')
        repairs = dict(line.split('=', 1) for line in (folder/'state_repairs.txt').read_text().splitlines() if '=' in line)
        require(float(repairs['events']) == 0, name+': smooth state required a floor repair')
        record = dict(name=name, config=config, backend=self.backend, threads=self.threads,
                      elapsed_seconds=elapsed, cells=plots[-1]['DENS'].size)
        if self.measure_resources:
            record['peak_rss_kib'] = int((folder/'peak_rss_kib.txt').read_text().strip())
        timing_path = folder/'run_timings.tsv'
        if timing_path.exists():
            with timing_path.open() as stream:
                record.update({k:float(v) for k,v in next(csv.DictReader(stream,delimiter='\t')).items()})
        if config['gravity_type'] == 'self':
            with (folder/'gravity_solves.tsv').open() as stream:
                solves = list(csv.DictReader(stream, delimiter='\t'))
            require(solves and all(float(s['residual']) <= float(s['target']) for s in solves), name+': unaccepted gravity residual')
            require(len({s['generation'] for s in solves}) == len(solves), name+': reused density lease')
            record['solves'] = len(solves)
            record['max_iterations'] = max(int(s['iterations']) for s in solves)
            for key in ['setup_seconds', 'solve_seconds', 'kernels', 'bytes_h2d', 'bytes_d2h', 'synchronizations',
                        'source_boundary_seconds', 'poisson_seconds', 'force_seconds']:
                record[key] = sum(float(s.get(key, 0)) for s in solves)
            stages = [s for s in solves if int(s['stage']) > 0]
            if stages:
                record['stage_solves'] = len(stages)
                record['stage_poisson_seconds'] = sum(float(s.get('poisson_seconds',0)) for s in stages)
            if self.backend == 'cuda':
                require(all(s.get('device') == '1' for s in solves), name+': host gravity fallback')
                require(record['kernels'] > 0, name+': no device gravity kernels')
                require(record['bytes_h2d'] <= len(solves)*int(config['max_blocks'])*8,
                        name+': iterative full-field host upload')
        if len(plots)>1 and energy_budget is not None:
            initial, final = totals(plots[0]), totals(plots[-1])
            record['mass_relative_drift'] = float(abs(final['mass'] / initial['mass'] - 1))
            record['energy_minus_nuclear_over_initial_gas'] = float(abs(final['energy']-initial['energy']-final['binding']+initial['binding']) / initial['gas'])
            record['nuclear_heat_over_initial_gas'] = float((final['binding']-initial['binding']) / initial['gas'])
            require(record['mass_relative_drift'] <= 1e-12, name+': mass drift')
            require(record['energy_minus_nuclear_over_initial_gas'] <= energy_budget, name+': energy minus nuclear budget')
        self.results.append(record)
        return plots, folder, record

    @staticmethod
    def cloud_config(roots=1, extent=1., **changes):
        return dict(gravity_boundary='isolated', nblockx1=roots, nblockx2=roots, nblockx3=roots,
                    x1_max=extent, x2_max=extent, x3_max=extent,
                    x1l_boundary_type='reflecting', x1r_boundary_type='reflecting',
                    x2l_boundary_type='reflecting', x2r_boundary_type='reflecting',
                    x3l_boundary_type='reflecting', x3r_boundary_type='reflecting',
                    rho0=1e-8, amplitude=1e8, temperature0=1e-8, gas_cv=1., width=.08,
                    max_blocks=max(64, 16*roots**3), tmax=0, max_steps=0) | changes

    def cloud(self, name, **changes):
        config = self.cloud_config(**changes)
        data, folder, record = self.run(name, **config)
        d=data[0]
        center=np.array([float(config.get('center_'+a,float(config['x1_max'])/2)) for a in ['x','y','z']])
        xyz=np.array([d[a] for a in ['x','y','z']])-center[:,None]
        r=np.sqrt(np.sum(xyz**2,axis=0));sigma=float(config['width'])
        mass=(2*math.pi)**1.5*sigma**3
        erf=np.array([math.erf(v) for v in r/(math.sqrt(2)*sigma)])
        phi=-G*mass*erf/r
        factor=-G*mass*(erf-math.sqrt(2/math.pi)*r/sigma*np.exp(-r*r/(2*sigma*sigma)))/r**3
        exact=xyz*factor
        norm=lambda a:math.sqrt(float(np.average(a*a,weights=d['volume'])))
        record['potential_relative_rms']=norm(d['GPOT']-phi)/norm(phi)
        record['force_relative_rms']=math.sqrt(sum(norm(d['GAC'+a]-exact[i])**2 for i,a in enumerate(['X','Y','Z']))/sum(norm(x)**2 for x in exact))
        require(record['potential_relative_rms']<.08 and record['force_relative_rms']<.12,name+': Gaussian analytic error')
        return data,folder,record

    def diffusion_reference(self, roots=4):
        amplitude,thermal=1e-5,2e-5
        data,folder,record=self.run(f'thermal-linear-{roots}', nblockx1=roots, rho0=1e7, temperature0=3e7,
            amplitude=amplitude, temperature_amplitude=thermal, use_diffusion='true',use_thermal_diff='true',
            alpha_therm=1e15,tmax=.05,cfl=.2,diff_integrator='RKL2')
        d=data[-1];k=2*math.pi/1e8;speed=math.sqrt((2/3)*CV*3e7);omega_g=4*math.pi*G*1e7
        matrix=np.array([[0,-k*speed,0],[k*speed-omega_g/(k*speed),0,k*speed],
                         [0,-(2/3)*k*speed,-1e15*k*k]])
        values,vectors=np.linalg.eig(matrix)
        exact=np.real(vectors @ (np.exp(values*d['time'])*np.linalg.solve(vectors,np.array([amplitude,0,thermal]))))
        cosine=np.cos(k*d['x']);sine=np.sin(k*d['x'])
        measured=np.array([2*np.mean((d['DENS']/1e7-1)*cosine),2*np.mean(d['VELX']/speed*sine),
                           2*np.mean((d['TEMP']/3e7-1)*cosine)])
        error=float(np.linalg.norm(measured-exact)/np.linalg.norm(exact))
        record.update(linear_relative_error=error, independent_amplitudes=exact.tolist(), measured_amplitudes=measured.tolist())
        require(error<.02, record['name']+': independent thermal/Jeans ODE reference')
        require(abs(measured[2]-thermal)>thermal*.05,record['name']+': diffusion inactive')
        require(np.max(abs(d['GACX']))>0,record['name']+': gravity inactive')
        return data,folder,record

    @staticmethod
    def burning_config(**changes):
        return dict(nblockx1=2, rho0=1e7, temperature0=1e9, amplitude=.01,temperature_amplitude=.01,
                    eos_type='helmholtz',eos_table_path=str(ROOT/'EOS_toolkit/tables/helmholtz/helm_table.dat'),
                    use_burn='true',network_name='aprox13',xhe4=1.,xc12=0.,xo16=0.,
                    ode_solver='bd',linear_solver='DenseLU',ode_rtol=1e-9,ode_atol=1e-12,
                    tmax=1e-4,dt_init=2.5e-5,tstep_change_factor=1.,use_nse='false') | changes

    def coupled(self, name, diffusion=False, **changes):
        config=self.burning_config()
        if diffusion:
            config.update(use_diffusion='true',use_thermal_diff='true',use_species_diff='true',diff_integrator='RKL2')
        data,folder,record=self.run(name, **(config|changes))
        require(abs(record['nuclear_heat_over_initial_gas'])>1e-8,name+': no nuclear heat')
        def mean_fraction(d,key):
            mass=d['DENS'].astype(np.longdouble)*d['volume']
            return np.sum(mass*d[key])/np.sum(mass)
        change=max(float(abs(mean_fraction(data[-1],k)-mean_fraction(data[0],k))) for k in NUCLEI)
        record['mean_composition_change']=change
        require(change>1e-8,name+': no composition change')
        require(np.max(abs(data[-1]['GACX']))>0,name+': no gravitational force')
        return data,folder,record

    @staticmethod
    def compact_config(**changes):
        return dict(nblockx1=1,x1_max=1.,temperature0=3e9,xhe4=0.,xc12=.5,xo16=.5,
                    tmax=1e-10,dt_init=2.5e-11,dt_min=1e-20,cfl=.4) | changes

    def temporal_coupling(self):
        trajectories=[]
        # Keep the reference interval smooth and temporally resolved: the
        # separate strong-heating budget test is not an asymptotic order test.
        for count in [4,8,16,64,128]:
            data,_,record=self.coupled(f'coupled-time-{count}',True,
                tmax=1e-6,dt_init=1e-6/count,ode_rtol=1e-11,ode_atol=1e-14,nuclearTempMin=1e8)
            trajectories.append(data[-1])
        reference=trajectories[-2]
        def distance(d):
            return math.sqrt(sum(float(np.mean(((d[k]-reference[k])/max(np.max(abs(reference[k])),1e-100))**2))
                                 for k in ['DENS','TEMP','VELX','he4']))
        errors=[distance(d) for d in trajectories[:3]]
        reference_change=distance(trajectories[-1])
        orders=[math.log2(a/b) for a,b in zip(errors,errors[1:])]
        self.results[-1].update(coupled_errors=errors,coupled_orders=orders,reference_change=reference_change)
        require(reference_change<.1*min(errors),'coupled temporal reference unresolved')
        require(min(orders)>=1.8,'B/2-D/2-H-D/2-B/2 time convergence')

    def run_checks(self, quick=False):
        roots=[1] if quick else [1,2,4]
        clouds=[self.cloud(f'cloud-{r}',roots=r)[2] for r in roots]
        if not quick:
            for key in ['potential_relative_rms','force_relative_rms']:
                orders=[math.log2(a[key]/b[key]) for a,b in zip(clouds,clouds[1:])]
                require(min(orders)>=1.8,key+': Gaussian convergence order')
                clouds[-1][key+'_orders']=orders
            # Same dx and same analytic source; a broader cloud deliberately
            # exposes finite-domain mass truncation before expanding the box.
            inner,_,a=self.cloud('cloud-domain',roots=2,width=.18)
            outer,_,b=self.cloud('cloud-expanded',roots=4,extent=2.,width=.18,center_x=1.,center_y=1.,center_z=1.)
            def core_error(d,center):
                xyz=np.array([d[k]-center for k in ['x','y','z']]);r=np.sqrt(np.sum(xyz*xyz,axis=0))
                mask=np.max(abs(xyz),axis=0)<.375
                exact=-G*(2*math.pi)**1.5*.18**3*np.array([math.erf(v/(math.sqrt(2)*.18)) for v in r])/r
                return float(np.linalg.norm((d['GPOT']-exact)[mask])/np.linalg.norm(exact[mask]))
            a['common_core_error']=core_error(inner[0],.5);b['common_core_error']=core_error(outer[0],1.)
            require(b['common_core_error']<a['common_core_error'],'expanding isolated domain did not improve common core')
            mixed,_,_=self.cloud('cloud-mixed',roots=2,width=.06,center_x=.22,center_y=.22,center_z=.22,
                                 lrefinemax=1,refine_threshold=.1,derefine_threshold=.01)
            require(len(set(mixed[0]['level']))>1,'isolated cloud requires a true coarse/fine interface')
        self.diffusion_reference()
        a,_,_=self.coupled('gravity-burn',False,**self.compact_config())
        b,_,record=self.coupled('gravity-burn-diffusion',True,**self.compact_config())
        difference=max(float(np.max(abs(a[-1][k]-b[-1][k])))/max(float(np.max(abs(a[-1][k]))),1e-100)
                       for k in ['TEMP','ENER','c12'])
        record['transport_relative_effect']=difference
        require(difference>1e-8,'combined case has no resolved transport effect')
        if not quick:self.temporal_coupling()
