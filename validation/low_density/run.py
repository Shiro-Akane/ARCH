"""Actual ARCH low-density evolution, conservation, floor and restart witnesses.

The frozen manifest owns tolerances. References are the analytic entropy wave,
constant acceleration, and symmetric two-rarefaction Euler solution; CPU/CUDA
agreement alone never supplies the scientific oracle. Requires numpy and h5py.
"""
import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import time
import numpy as np
import h5py

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = json.loads(Path(__file__).with_name('manifest.json').read_text())
FIELDS = ('rho', 'mom_u', 'mom_v', 'mom_w', 'eng')


def checkpoint(path, params):
    with h5py.File(path) as data:
        n = int(data.attrs['cells_per_block'])
        level = data['Blocks/level'][:]
        width = 1. / (params['nblockx1'] * n * np.exp2(level))
        x = (data['Blocks/logical_x1'][:][:, None] * n + np.arange(n) + .5) * width[:, None]
        fields = np.stack([data['Data/' + f][:] for f in FIELDS], axis=-1).reshape(-1, 5)
        fractions = data['Data/X'][:].reshape(int(data.attrs['num_species']), -1) if int(data.attrs['num_species']) else np.empty((0, fields.shape[0]))
        x = x.ravel()
        width = np.repeat(width, n)
        order = np.argsort(x)
        return dict(values=fields[order], x=x[order], width=width[order],
                    time=float(data.attrs['time']), repairs=data['state_repairs'][:], fractions=fractions[:,order],
                    path=str(path), levels=level.tolist())


def run(arch, backend, directory, case, overrides, scale=1., restart=None):
    directory.mkdir(parents=True, exist_ok=True)
    params = dict(geometry='cartesian', nblockx1=8, nblockx2=0, nblockx3=0,
                  max_blocks=256, x1_min=0., x1_max=1., solver='HLLC', reconstruct='muscl',
                  limiter='mc', time_integrator='RK3', cfl=.4, lrefinemin=0, lrefinemax=0,
                  regrid_interval=2, refine_var='DENS', refine_threshold=.8, derefine_threshold=.2,
                  tmax=.1, max_steps=-1, plt_dt=-1, plt_dstep=-1, chk_dt=-1, chk_dstep=-1,
                  out_dir=str(directory), base_name='state', eos_type='ideal', gamma=1.4,
                  gravity_type='none', use_burn='false', use_diffusion='false',
                  sml_rho=scale*1e-12, min_eint=1e-10, max_eint=1e21, compute_backend=backend,
                  x1l_boundary_type='periodic', x1r_boundary_type='periodic')
    params.update(overrides)
    if restart:
        params.update(restart='true', restart_file=str(restart))
    inputs=directory/'case.par'
    inputs.write_text(''.join(f'{k}={v}\n' for k,v in params.items()))
    begin=time.monotonic()
    with (directory/'stdout.log').open('w') as out, (directory/'stderr.log').open('w') as err:
        result=subprocess.run([str(arch),case,str(inputs)],cwd=ROOT,stdout=out,stderr=err,
            env={**os.environ,'OMP_NUM_THREADS':'4'},timeout=180)
    if result.returncode:
        raise RuntimeError(f'{directory.name} failed ({result.returncode}): '+(directory/'stderr.log').read_text()[-1500:])
    paths=sorted(directory.glob('state_chk_*.h5'))
    if not paths: raise AssertionError(f'{directory.name} produced no checkpoint')
    final=checkpoint(paths[-1],params)
    if final['time']!=params['tmax']: raise AssertionError('wrong physical end time')
    if not np.all(np.isfinite(final['values'])) or not np.all(final['values'][:,0]>0):
        raise AssertionError('invalid evolved state')
    f=final['values']
    thermal=f[:,4]-.5*np.sum(f[:,1:4]*(f[:,1:4]/f[:,0,None]),axis=1)
    if not np.all(np.isfinite(thermal)) or not np.all(thermal>0):
        raise AssertionError('nonpositive or nonfinite evolved thermal energy')
    final.update(params=params,seconds=time.monotonic()-begin)
    return final


def assert_budget(error, limit, message):
    if not math.isfinite(error) or error>limit: raise AssertionError(f'{message}: {error} > {limit}')


def conservation(run, expected, scale):
    observed=np.sum(run['values'] * run['width'][:,None],axis=0)/scale
    error=float(np.max(np.abs(observed-expected)/np.maximum(np.abs(expected),1.)))
    assert_budget(error,MANIFEST['closed_conservation_error'],'conservation')
    return error


def scale_difference(run, reference, scale):
    if not np.array_equal(run['x'],reference['x']): raise AssertionError('scale-dependent AMR topology')
    difference=float(np.max(np.abs(run['values']/scale-reference['values']) /
                            np.maximum(np.abs(reference['values']),1.)))
    assert_budget(difference,MANIFEST['evolution_scaled_difference'],'scaled evolution')
    return difference


def rarefaction_density(x, end):
    # Equal initial rho=1,p=.4, velocities +/-2, gamma=1.4. Riemann
    # invariants give c*=c0-(gamma-1)*2/2>0, so no exact vacuum is present.
    c0=math.sqrt(1.4*.4)
    cstar=c0-.4
    xi=np.abs((x-.5)/end)
    c=np.where(xi<cstar,cstar,np.where(xi>2+c0,c0,2/2.4*(c0+.2*(xi-2))))
    return (c/c0)**5


def stress_cases(arch, backend, out, record):
    # Stationary material contacts isolate density contrast from a Mach-number
    # cancellation limit. Pressure scales with the dilute side, so both sound
    # speeds remain bounded while rho_left/rho_right reaches 1e18.
    for contrast in (1e6, 1e12, 1e18):
        for scale in (1., 1e-30):
            name=f'contact-{contrast:g}-{scale:g}'
            r=run(arch,backend,out/name,'Sod',dict(rho_left=scale,rho_right=scale/contrast,
                p_left=scale/contrast,p_right=scale/contrast,u_left=0.,u_right=0.,
                sml_rho=scale/contrast*1e-4,min_eint=1e-30,tmax=.02),scale)
            expected=np.where(r['x']<.5,1.,1./contrast)
            error=float(np.max(np.abs(r['values'][:,0]/scale-expected)))
            assert_budget(error,1e-11,'stationary density-contrast contact')
            if np.any(r['repairs'][:9]): raise AssertionError('contact required floor')
            record(name,r,analytic_error=error)
    for solver in ('HLL','HLLC','Roe','SW','VL'):
        reference=None
        for scale in (1.,1e-30):
            name=f'flux-{solver}-{scale:g}'
            r=run(arch,backend,out/name,'SmoothAdvection',dict(solver=solver,
                rho_mean=scale,rho_amplitude=.2*scale,pressure0=scale,velocity0=1.,tmax=.02),scale)
            if reference is None: reference=r
            exact=1+.2*np.sinc(r['width'])*np.sin(2*math.pi*(r['x']-r['time']))
            error=float(np.sum(np.abs(r['values'][:,0]/scale-exact)*r['width']))
            assert_budget(error,.001,'advected entropy wave')
            if np.any(r['repairs'][:9]): raise AssertionError('ordinary flux wave required floor')
            record(name,r,scaled_difference=scale_difference(r,reference,scale),l1=error)
    # Active floors intentionally perturb this uniform state. Quantify the
    # resulting physical error, separately from the implementation error, over
    # the whole domain and a fixed central observation region.
    for floor in MANIFEST['active_density_floors']:
        scale=1e-30
        name=f'active-floor-{floor:g}'
        r=run(arch,backend,out/name,'Sod',dict(rho_left=.5*scale,rho_right=.5*scale,
            p_left=.5*scale,p_right=.5*scale,u_left=2.,u_right=2.,
            sml_rho=floor,tmax=.02),scale)
        conserved_per_rho=np.array([1.,2.,0.,0.,4.5])
        expected=conserved_per_rho*(floor/scale)
        error=float(np.max(np.abs(r['values']/scale-expected)))
        assert_budget(error,MANIFEST['active_floor_accounting_error'],'active floor analytic state')
        added=(floor/scale-.5)*conserved_per_rho
        recorded=np.array([r['repairs'][2],*r['repairs'][4:7],r['repairs'][7]])/scale
        assert_budget(float(np.max(np.abs(recorded-added))),
                      MANIFEST['active_floor_accounting_error'],'active floor accounting')
        difference=np.abs(r['values']/scale-.5*conserved_per_rho)
        global_error=np.sum(difference*r['width'][:,None],axis=0)
        region=(r['x']>=.25)&(r['x']<.75)
        region_error=np.sum(difference[region]*r['width'][region,None],axis=0)/np.sum(r['width'][region])
        record(name,r,analytic_error=error,physical_floor_error=global_error.tolist(),
               central_region_error=region_error.tolist(),added_conserved=recorded.tolist())
    reference=None
    for floor in (1e-35,1e-40,1e-50):
        name=f'floor-sensitivity-{floor:g}'
        r=run(arch,backend,out/name,'SmoothAdvection',dict(sml_rho=floor,
            rho_mean=1e-30,rho_amplitude=2e-31,pressure0=1e-30,velocity0=1.,tmax=.02),1e-30)
        if reference is None: reference=r
        error=float(np.max(np.abs(r['values']-reference['values'])/1e-30))
        assert_budget(error,1e-11,'inactive floor dependence')
        if np.any(r['repairs'][:9]): raise AssertionError('inactive floor triggered')
        record(name,r,floor_difference=error)
    for method in ('RKL1','RKL2'):
        reference=None
        for scale in (1.,1e-30):
            name=f'diffusion-{method}-{scale:g}'
            r=run(arch,backend,out/name,'DiffusionMode',dict(rho0=scale,pressure0=scale,
                use_diffusion='true',use_species_diff='true',use_thermal_diff='false',
                use_viscous_diff='false',D_spec=.1,diff_integrator=method,tmax=.01),scale)
            exact=.5+.25*np.sinc(r['width'])*np.cos(2*math.pi*r['x'])*math.exp(-.1*(2*math.pi)**2*r['time'])
            error=float(np.sum(np.abs(r['fractions'][1]-exact)*r['width']))
            assert_budget(error,2e-4,'analytic species diffusion')
            if reference is None: reference=r
            difference=float(np.max(np.abs(r['fractions']-reference['fractions'])))
            assert_budget(difference,1e-9,'scaled species diffusion')
            if np.any(r['repairs'][:9]): raise AssertionError('diffusion required repair')
            record(name,r,l1=error,scaled_difference=difference)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch',type=Path,required=True)
    parser.add_argument('--backend',choices=['cpu','cuda'],default='cpu')
    parser.add_argument('--output-root',type=Path,required=True)
    args=parser.parse_args()
    arch=args.arch.resolve(); out=args.output_root.resolve()
    if out.exists() and any(out.iterdir()):
        raise RuntimeError('Use a new empty output directory; prior PASS/results must not survive a failed rerun')
    out.mkdir(parents=True,exist_ok=True)
    records=[]; errors={}
    def record(name,r,**metrics):
        item=dict(name=name,backend=args.backend,seconds=r['seconds'],time=r['time'],
                  minimum_density=float(np.min(r['values'][:,0])),repairs=r['repairs'].tolist(),
                  checkpoint=r['path'],parameters=r['params'],**metrics)
        records.append(item)
        (out/'results.json').write_text(json.dumps(dict(manifest=MANIFEST,runs=records),indent=2)+'\n')
        print(name,metrics,flush=True)
    for method in ('pcm','muscl','ppm'):
        for n in (64,128,256):
            reference=None
            for scale in MANIFEST['evolution_scales']:
                name=f'wave-{method}-{n}-{scale:g}'
                r=run(arch,args.backend,out/name,'SmoothAdvection',dict(nblockx1=n//16,
                    reconstruct=method,rho_mean=scale,rho_amplitude=.2*scale,pressure0=scale,velocity0=1.),scale)
                if np.any(r['repairs'][:9]): raise AssertionError('smooth wave required repair')
                exact=1+.2*np.sinc(r['width'])*np.sin(2*math.pi*(r['x']-r['time']))
                l1=float(np.sum(np.abs(r['values'][:,0]/scale-exact)*r['width']))
                if reference is None: reference=r
                difference=scale_difference(r,reference,scale)
                drift=conservation(r,np.array([1,1,0,0,3.]),scale)
                errors[method,n,scale]=l1
                record(name,r,l1=l1,scaled_difference=difference,conservation=drift)
        for scale in MANIFEST['evolution_scales']:
            order=math.log2(errors[method,128,scale]/errors[method,256,scale])
            minimum=MANIFEST['smooth_minimum_orders'][method]
            if order<minimum: raise AssertionError(f'{method} order {order} below {minimum}')
    reference=None
    nodes,weights=np.polynomial.legendre.leggauss(16)
    for scale in MANIFEST['evolution_scales']:
        name=f'rarefaction-{scale:g}'
        r=run(arch,args.backend,out/name,'Sod',dict(nblockx1=16,tmax=.05,
            rho_left=scale,rho_right=scale,p_left=.4*scale,p_right=.4*scale,u_left=-2.,u_right=2.,
            x1l_boundary_type='outflow',x1r_boundary_type='outflow'),scale)
        if np.any(r['repairs'][:9]): raise AssertionError('positive double rarefaction required repair')
        x=r['x'][:,None]+.5*r['width'][:,None]*nodes
        exact=.5*(rarefaction_density(x,r['time'])@weights)
        l1=float(np.sum(np.abs(r['values'][:,0]/scale-exact)*r['width']))
        assert_budget(l1,.025,'independent double-rarefaction density L1')
        if reference is None:reference=r
        difference=scale_difference(r,reference,scale)
        drift=conservation(r,np.array([1-4*r['time'],0,0,0,3-13.6*r['time']]),scale)
        record(name,r,l1=l1,scaled_difference=difference,conservation=drift)
    for scale in (1.,1e-30):
        name=f'gravity-{scale:g}'
        r=run(arch,args.backend,out/name,'ExternalGravity',dict(rho0=scale,pressure0=scale,
            velocity_x0=.2,gravity_type='external',gravity_g_x=2.,time_integrator='RK2'),scale)
        expected=np.array([1.,.4,0.,0.,2.5+.5*.4**2])
        error=float(np.max(np.abs(r['values']/scale-expected)))
        assert_budget(error,1e-11,'constant-acceleration solution')
        record(name,r,analytic_error=error)
    # Deliberately floor a uniform state; the reported added conserved quantities
    # must exactly close the initialized-volume budget and survive restart.
    scale=1e-30
    base=dict(rho_left=.5*scale,rho_right=.5*scale,p_left=.5*scale,p_right=.5*scale,
              u_left=2.,u_right=2.,sml_rho=scale,tmax=.1,chk_dt=.05)
    r=run(arch,args.backend,out/'floor','Sod',base,scale)
    delta=np.array([.5,1.,0.,0.,2.25])
    observed=np.array([r['repairs'][2],*r['repairs'][4:7],r['repairs'][7]])/scale
    assert_budget(float(np.max(np.abs(observed-delta))),1e-11,'initial floor conserved accounting')
    record('floor',r)
    paths=sorted((out/'floor').glob('state_chk_*.h5'))
    midpoint=next(p for p in paths if checkpoint(p,r['params'])['time']==.05)
    restarted=run(arch,args.backend,out/'restart','Sod',base,scale,midpoint)
    assert_budget(float(np.max(np.abs(restarted['values']/scale-r['values']/scale))),1e-11,'restart fields')
    if not np.array_equal(restarted['repairs'],r['repairs']): raise AssertionError('restart lost repair history')
    record('restart',restarted)
    reference=None
    for scale in (1.,1e-30):
        name=f'amr-{scale:g}'
        r=run(arch,args.backend,out/name,'Sod',dict(nblockx1=4,lrefinemax=2,tmax=.1,
            refine_threshold=.15,derefine_threshold=.05,rho_left=scale,rho_right=.125*scale,
            p_left=scale,p_right=.1*scale,x1l_boundary_type='outflow',x1r_boundary_type='outflow'),scale)
        if max(r['levels'])==0: raise AssertionError('AMR witness never refined')
        import csv
        with (out/name/'state_regrid.tsv').open() as stream:
            topology=list(csv.DictReader(stream,delimiter='\t'))
        if not any(int(row['macro_step'])>0 and row['topology_changed']=='1' for row in topology):
            raise AssertionError('AMR witness never changed topology during evolution')
        if np.any(r['repairs'][:9]):raise AssertionError('AMR required unexpected repair')
        if reference is None:reference=r
        record(name,r,scaled_difference=scale_difference(r,reference,scale))
    stress_cases(arch,args.backend,out,record)
    (out/'PASS').write_text(f'{len(records)} physical runs passed\n')

if __name__=='__main__':main()
