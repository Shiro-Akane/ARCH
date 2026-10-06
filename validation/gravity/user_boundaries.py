#!/usr/bin/env python3
"""Real user-boundary runs, surface budgets, AMR and portable restart checks.

Extends the repository's gravity acceptance campaign. Raw checkpoints/plotfiles
stay in the chosen local output directory; only derived summaries are portable.
The internal mathematical tests independently verify the Poisson conditions.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
import validation_sanitizer


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def read_table(path):
    with path.open() as stream:
        return list(csv.DictReader((line for line in stream if not line.startswith('#')), delimiter='\t'))


def checkpoint(path, parameters):
    """Integrate conserved arrays with independent exact cell volume formulas."""
    with h5py.File(path) as file:
        dim = int(file.attrs['dim'])
        levels = file['Blocks/level'][:]
        logical = [file[f'Blocks/logical_x{axis}'][:] for axis in (1, 2, 3)]
        state = {key: file[f'Data/{key}'][:] for key in ('rho', 'mom_u', 'mom_v', 'mom_w', 'eng', 'rhoX')}
        result = dict(time=float(file.attrs['time']), step=int(file.attrs['step']),
                      identity=str(file.attrs['boundary_identity']),
                      repair=np.array(file['state_repairs']), levels=levels, state=state)
    volume = []
    for block, level in enumerate(levels):
        widths = [(float(parameters[f'x{a}_max'])-float(parameters[f'x{a}_min'])) /
                  (16 * int(parameters[f'nblockx{a}']) * 2**int(level)) if a <= dim else 1. for a in (1, 2, 3)]
        axes = [float(parameters[f'x{a}_min']) +
                (logical[a-1][block]*16+np.arange(16))*widths[a-1] if a <= dim else np.array([0.])
                for a in (1, 2, 3)]
        z, y, x = np.meshgrid(axes[2], axes[1], axes[0], indexing='ij')
        geom = parameters['geometry']
        if geom == 'cartesian':
            measure = np.full(x.shape, math.prod(widths))
        elif geom == 'cylindrical' or (geom == 'spherical' and dim == 2):
            measure = .5*((x+widths[0])**2-x**2) * widths[1] * widths[2]
        elif dim == 1:
            measure = ((x+widths[0])**3-x**3)/3.
        else:
            measure = ((x+widths[0])**3-x**3)/3. * (np.cos(y)-np.cos(y+widths[1])) * widths[2]
        volume.append(measure.ravel())
    volume = np.asarray(volume, dtype=np.longdouble)
    result['totals'] = np.array([np.sum(state[key].astype(np.longdouble)*volume)
                               for key in ('rho', 'mom_u', 'mom_v', 'mom_w', 'eng')], dtype=np.longdouble)
    result['species'] = np.sum(state['rhoX'].astype(np.longdouble)*volume, axis=(1, 2))
    for key, values in state.items():
        require(np.all(np.isfinite(values)), str(path)+': nonfinite '+key)
    require(np.all(state['rho'] > 0), str(path)+': nonpositive density')
    require(np.all(result['repair'] == 0), str(path)+': unexpected floor repair')
    return result


class UserBoundaryCampaign:
    def __init__(self, executable, output, backend='cpu', threads=1,
                 sanitizer=None, measure_resources=False):
        self.executable = Path(executable).resolve()
        self.output = Path(output).resolve()
        self.backend, self.threads = backend, threads
        self.sanitizer = sanitizer if backend == 'cuda' else None
        self.measure_resources = measure_resources
        self.output.mkdir(parents=True, exist_ok=True)
        text = (ROOT/'simulation/UserBoundary/UserBoundaryCart.par').read_text()
        self.base = {key.strip(): value.strip() for line in text.splitlines()
                     if '=' in line and not line.lstrip().startswith('#')
                     for key, value in [line.split('=', 1)]}
        self.results = []

    def run(self, name, geometry='cartesian', dimension=1, case='UserBoundary', **changes):
        folder = self.output/name
        folder.mkdir(parents=True, exist_ok=True)
        p = self.base | dict(geometry=geometry, nblockx1='1', nblockx2='1' if dimension >= 2 else '0',
             nblockx3='1' if dimension == 3 else '0', x1_min='0' if geometry == 'cartesian' else '.25',
             x1_max='1' if geometry == 'cartesian' else '1.25', x2_min='0.4', x2_max='1.4',
             x3_min='0.2', x3_max='1.2', dt_max='1e-4', tmax='3e-4', max_steps='3',
             out_dir=str(folder), base_name=name, compute_backend=self.backend,
             chk_dstep='1', user_boundary_heat_flux='.01')
        for axis in (1, 2, 3):
            for side in ('l', 'r'):
                p[f'x{axis}{side}_boundary_type'] = 'user' if axis <= dimension else 'outflow'
        p.update({key: str(value).lower() if isinstance(value, bool) else str(value) for key, value in changes.items()})
        config = folder/'input.par'
        config.write_text(''.join(f'{key} = {value}\n' for key, value in p.items()))
        start = time.perf_counter()
        command = [str(self.executable), case, str(config)]
        if self.sanitizer:
            command = self.sanitizer.command(command, folder)
        if self.measure_resources:
            command = ['/usr/bin/time', '-f', '%M', '-o', str(folder/'peak_rss_kib.txt')] + command
        process = subprocess.run(command, cwd=folder,
            env=os.environ | {'OMP_NUM_THREADS': str(self.threads), 'OMP_DYNAMIC': 'FALSE'},
            capture_output=True, text=True, timeout=1200 if self.measure_resources else 240)
        seconds = time.perf_counter()-start
        (folder/'run.log').write_text(process.stdout+process.stderr)
        require(process.returncode == 0, name+': run failed\n'+(process.stdout+process.stderr)[-4000:])
        paths = sorted(folder.glob('*chk_*.h5'))
        require(len(paths) >= 2, name+': missing accepted checkpoints')
        initial_path = Path(p['restart_file']) if p.get('restart') == 'true' else paths[0]
        initial, final = checkpoint(initial_path, p), checkpoint(paths[-1], p)
        if int(p['max_steps']) >= 0:
            require(final['step'] == int(p['max_steps']), name+': did not reach target accepted step')
        require(abs(final['time']-float(p['tmax'])) <= 8*np.finfo(float).eps*float(p['tmax']),
                name+': physical final time differs')
        rows = read_table(folder/'boundary_fluxes.tsv')
        budgets = {row['operator']: row for row in rows}
        mass_out = float(budgets['total']['mass'])
        mass_error = float(final['totals'][0]-initial['totals'][0]+mass_out)
        bound = 128*np.finfo(float).eps*(1+final['step'])*max(
            abs(initial['totals'][0]), abs(final['totals'][0]), abs(mass_out))
        require(abs(mass_error) <= bound, name+': discrete mass/actual surface flux budget')
        energy_error = None
        if p['gravity_type'] == 'none':
            energy_error = float(final['totals'][4]-initial['totals'][4]+float(budgets['total']['energy']))
            ebound = 128*np.finfo(float).eps*(1+final['step'])*max(
                abs(initial['totals'][4]), abs(final['totals'][4]))
            require(abs(energy_error) <= ebound, name+': discrete energy/actual surface flux budget')
        require(abs(float(final['species'].sum()-initial['species'].sum())+mass_out) <= bound,
                name+': species mass flux differs from total mass flux')
        require(initial['identity'] == final['identity'] and 'physical:name=' in final['identity'],
                name+': boundary restart identity missing or changed')
        if p['gravity_type'] == 'self':
            solves = read_table(folder/'gravity_solves.tsv')
            require(bool(solves), name+': no potential solve evidence')
            require(all(float(s['residual']) <= float(s['target']) for s in solves),
                    name+': unaccepted potential residual')
            if self.backend == 'cuda':
                require(all(s.get('device') == '1' for s in solves), name+': host gravity fallback')
                require(sum(int(s['kernels']) for s in solves) > 0, name+': no device gravity kernels')
            exchange = read_table(folder/'gravity_boundary_exchange.tsv')
            require(bool(exchange) and all(math.isfinite(float(s['boundary_exchange'])) for s in exchange),
                    name+': nonfinite or absent Green boundary exchange')
        record = dict(name=name, geometry=geometry, dimension=dimension, backend=self.backend,
            wall_seconds=seconds, steps=final['step'], final_time=final['time'],
            mass_budget_error=mass_error, energy_budget_error=energy_error,
            final_blocks=len(final['levels']), minimum_density=float(final['state']['rho'].min()))
        if self.sanitizer:
            record['sanitizer'] = self.sanitizer.evidence(folder)
        if self.measure_resources:
            record.update(elapsed_seconds=seconds, threads=self.threads, config=p,
                          cells=final['state']['rho'].size,
                          peak_rss_kib=int((folder/'peak_rss_kib.txt').read_text().strip()))
            with (folder/'run_timings.tsv').open() as stream:
                record.update({k: float(v) for k, v in next(csv.DictReader(stream, delimiter='\t')).items()})
            if p['gravity_type'] == 'self':
                for key in ('setup_seconds', 'solve_seconds', 'source_boundary_seconds',
                            'poisson_seconds', 'force_seconds'):
                    record[key] = sum(float(s[key]) for s in solves)
                record['stage_poisson_seconds'] = sum(float(s['poisson_seconds'])
                                                     for s in solves if int(s['stage']) > 0)
        self.results.append(record)
        return p, initial, final, paths[-1], record

    def reject(self, name, parameters, expected, case='UserBoundary', **changes):
        folder = self.output/name
        folder.mkdir(parents=True, exist_ok=True)
        p = parameters | dict(out_dir=str(folder), base_name=name) | {k: str(v) for k, v in changes.items()}
        config = folder/'input.par'
        config.write_text(''.join(f'{key} = {value}\n' for key, value in p.items()))
        process = subprocess.run([str(self.executable), case, str(config)], cwd=folder,
            env=os.environ | {'OMP_NUM_THREADS': str(self.threads)}, capture_output=True, text=True, timeout=60)
        text = process.stdout+process.stderr
        (folder/'run.log').write_text(text)
        require(process.returncode != 0 and expected.lower() in text.lower(), name+': missing expected rejection\n'+text[-3000:])
        require(not list(folder.glob('*.h5')), name+': rejected input published scientific output')
        self.results.append(dict(name=name, rejected=True, reason=expected))

    def restart_checks(self, source_backend=None):
        # Source runs are already accepted before the target consumes their file.
        # Changing only compute_backend is a portable restart, not a model change.
        source = source_backend or self
        continued = source.run('restart-continuous', tmax='6e-4', max_steps=6)
        # Rejection checks execute on the target backend too. A CUDA source
        # checkpoint must not make a CPU-only target request CUDA before the
        # intended boundary-identity check can run.
        p = continued[0] | dict(compute_backend=self.backend)
        checkpoint_path = next(path for path in sorted((source.output/'restart-continuous').glob('*chk*.h5'))
            if checkpoint(path, p)['step'] == 3)
        resumed = self.run('restart-resumed', restart='true', restart_file=checkpoint_path,
                           tmax='6e-4', max_steps=6)
        a, b = continued[2], resumed[2]
        require(a['step'] == b['step'] and a['time'] == b['time'], 'restart accepted identity/time differs')
        # Same backend is deterministic; different backends use the existing
        # FP64 parity scale, preserving independent conservation checks above.
        for key in a['state']:
            if source.backend == self.backend:
                require(np.array_equal(a['state'][key], b['state'][key]), 'restart differs in '+key)
            else:
                require(np.allclose(a['state'][key], b['state'][key], rtol=2e-11, atol=2e-13),
                        'cross-backend restart differs in '+key)
        self.results.append(dict(name='restart-identity', source_backend=source.backend,
                                 target_backend=self.backend, bitwise_equal=source.backend==self.backend))
        self.reject('restart-reject-callback-input', p, 'physical/gravity boundary identity',
                    restart='true', restart_file=checkpoint_path, user_boundary_heat_flux='.02')
        # Strict case input validation must pass before these checks can reach
        # the independently missing boundary callback on the Sod registration.
        sod = p | dict(x_pos='.5', rho_left=1, p_left=1, u_left=0,
                       rho_right='.125', p_right='.1', u_right=0)
        sod.pop('user_boundary_heat_flux')
        self.reject('missing-physical-callback', sod, 'physical_boundary.cpp', case='Sod')
        self.reject('missing-gravity-callback', sod, 'gravity_boundary.cpp', case='Sod',
                    use_diffusion='false', x1l_boundary_type='outflow', x1r_boundary_type='outflow')

    def run_checks(self, quick=False):
        # Always retain all three geometries and a 3D Cartesian coupled run.
        shapes = [(g, d) for d in (1, 2, 3) for g in ('cartesian', 'cylindrical', 'spherical')
                  if not quick or d < 3 or g == 'cartesian']
        for geometry, dimension in shapes:
            self.run(f'{geometry}-{dimension}d', geometry, dimension)
        self.run('cylindrical-axis-join', 'cylindrical', 2, x1_min=0, x1_max=4,
                 x2_min=0, x2_max=2*math.pi, x1l_boundary_type='reflect',
                 x2l_boundary_type='periodic', x2r_boundary_type='periodic')
        if not quick:
            self.run('spherical-axis-pole-join', 'spherical', 3, x1_min=0, x1_max=4,
                x2_min=0, x2_max=math.pi, x3_min=0, x3_max=2*math.pi,
                x1l_boundary_type='reflect', x2l_boundary_type='reflect', x2r_boundary_type='reflect',
                x3l_boundary_type='periodic', x3r_boundary_type='periodic')
        self.run('function-registration', 'cylindrical', case='UserGravity')
        self.run('heat-ledger', gravity_type='none')
        if not quick:
            for method in ('RKL1', 'RKL2'):
                self.run('heat-ledger-multistage-'+method.lower(), gravity_type='none',
                         alpha_therm=400, diff_integrator=method)
            _, _, final, _, _ = self.run('heat-ledger-time-varying', case='UserGravity',
                gravity_type='none', alpha_therm=400, diff_integrator='RKL2')
            # Independent integral over the two unit-area slab faces:
            # integral 2*q0*(1+t/tau) dt, q0=.01 CGS, tau=1 s. Second-order
            # RKL quadrature must reproduce this linear time forcing.
            expected_heat = .02*(final['time'] + .5*final['time']**2)
            actual_heat = float(read_table(self.output/'heat-ledger-time-varying'/'boundary_fluxes.tsv')[-1]['heat'])
            require(abs(actual_heat-expected_heat) <= 128*np.finfo(float).eps*expected_heat,
                    'time-varying heat: RKL2 stage-time integral differs')
            self.results[-1]['heat_time_integral_error'] = actual_heat-expected_heat
        for gravity in ('none', 'self'):
            _, _, final, _, _ = self.run('amr-heat-ledger' if gravity=='none' else 'amr-coupled',
                gravity_type=gravity, lrefinemax=1, max_blocks=64, regrid_interval=1,
                refine_var='ENER', refine_threshold='1e-8', derefine_threshold='1e-9')
            require(len(final['levels']) > 1, 'user-boundary AMR never refined')
        self.restart_checks()
        return self.results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--backend', choices=['cpu', 'cuda'], default='cpu')
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--reference-arch', type=Path,
                        help='optional other-backend executable for physical parity and bidirectional restart')
    validation_sanitizer.add_arguments(parser)
    args = parser.parse_args()
    require(not args.output.exists() or not any(args.output.iterdir()),
            'choose a new or empty local output directory')
    sanitizer = validation_sanitizer.from_arguments(args)
    campaign = UserBoundaryCampaign(args.arch, args.output, args.backend, args.threads, sanitizer=sanitizer)
    campaign.run_checks(args.quick)
    if args.reference_arch:
        other_backend = 'cpu' if args.backend == 'cuda' else 'cuda'
        reference = UserBoundaryCampaign(args.reference_arch, args.output/'parity-reference',
                                         other_backend, args.threads, sanitizer=sanitizer)
        for record in list(campaign.results):
            if record.get('name') not in {f'{g}-{d}d' for g in ('cartesian','cylindrical','spherical') for d in (1,2,3)}:
                continue
            p, _, actual, _, _ = reference.run(record['name'], record['geometry'], record['dimension'])
            candidate_path = sorted((campaign.output/record['name']).glob('*chk*.h5'))[-1]
            candidate = checkpoint(candidate_path, p)
            for key in actual['state']:
                require(np.allclose(actual['state'][key], candidate['state'][key], rtol=2e-11, atol=2e-13),
                        record['name']+': backend parity differs in '+key)
            campaign.results.append(dict(name='backend-parity-'+record['name'], passed=True))
        for source_backend, target_backend, source_exe, target_exe in (
            (other_backend, args.backend, args.reference_arch, args.arch),
            (args.backend, other_backend, args.arch, args.reference_arch)):
            folder = args.output/f'restart-{source_backend}-to-{target_backend}'
            source = UserBoundaryCampaign(source_exe, folder/'source', source_backend,
                                          args.threads, sanitizer=sanitizer)
            target = UserBoundaryCampaign(target_exe, folder/'target', target_backend,
                                          args.threads, sanitizer=sanitizer)
            target.restart_checks(source)
            campaign.results.extend(source.results+target.results)
    (campaign.output/'summary.json').write_text(json.dumps(dict(raw_data='local-only', cases=campaign.results), indent=2)+'\n')
    print(json.dumps(campaign.results, indent=2))


if __name__ == '__main__':
    main()
