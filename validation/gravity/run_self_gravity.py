#!/usr/bin/env python3
"""Independent Jeans-wave, energy, temporal, AMR and restart acceptance checks.

Requires numpy and h5py; runs the real ARCH executable, never a mock solver.
CGS, error definitions and acceptance budgets are frozen in the P3/P4 plan.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess

import h5py
import numpy as np
from gravity_box import BoxCampaign

ROOT = Path(__file__).resolve().parents[2]
RHO, PRESSURE, AMPLITUDE, G = 1e7, 6e6, 1e-4, 6.67430e-8
K = 2 * math.pi
OMEGA = math.sqrt(K * K - 4 * math.pi * G * RHO)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_plot(path, roots):
    with h5py.File(path) as f:
        data = {k: f['Data/' + k][:].ravel() for k in f['Data']}
        data['x'] = f['Grid/x'][:]
        data['time'] = float(f.attrs['time'])
        data['level'] = f['Grid/level'][:]
        cells_per_block = data['DENS'].size // data['level'].size
        dimension = int(f.attrs['dim'])
        widths = 1 / (16 * roots * 2. ** data['level'])
        # The multidimensional smoke domains use equal native spacing, with
        # all active lengths normalized in the run metadata below.
        data['volume'] = np.repeat(widths ** dimension, cells_per_block)
        data['width'] = np.repeat(widths, cells_per_block)
    return data


def metrics(initial, final, phase=0.):
    def totals(d):
        v = d['volume'].astype(np.longdouble)
        rho = d['DENS'].astype(np.longdouble)
        potential = np.sum(v * rho * d['GPOT'] / 2)
        energy = np.sum(v * d['ENER']) + potential
        force = v * rho * d['GACX']
        return np.sum(v * rho), energy, potential, float(abs(np.sum(force)) / np.sum(abs(force)))
    m0, e0, w0, f0 = totals(initial)
    m1, e1, _, f1 = totals(final)
    average = np.sinc(final['width'])
    exact = RHO * (1 + AMPLITUDE * average * np.cos(K * final['x'] + phase - OMEGA * final['time']))
    error = math.sqrt(np.average((final['DENS'] - exact) ** 2, weights=final['volume'])) / (RHO * AMPLITUDE / math.sqrt(2))
    return dict(density_relative_rms=error, mass_relative_drift=float(abs(m1 - m0) / m0),
                energy_over_initial_potential=float(abs(e1 - e0) / abs(w0)),
                initial_net_force=f0, final_net_force=f1)


class Campaign:
    def __init__(self, executable, output):
        self.executable, self.output = executable, output
        output.mkdir(parents=True, exist_ok=True)
        self.base = {}
        for line in (ROOT / 'simulation/JeansWave/JeansWave.par').read_text().splitlines():
            if '=' in line and not line.startswith('#'):
                key, value = line.split('=', 1)
                self.base[key] = value
        self.results = []

    def run(self, name, *, energy_budget=.01, **changes):
        folder = self.output / name
        folder.mkdir(exist_ok=True)
        config = self.base | {'out_dir': str(folder)} | {k: str(v) for k, v in changes.items()}
        path = folder / 'input.par'
        path.write_text('\n'.join(f'{k}={v}' for k, v in config.items()) + '\n')
        with (folder / 'run.log').open('w') as log:
            subprocess.run([str(self.executable), 'JeansWave', str(path)], cwd=ROOT,
                           env=os.environ | {'OMP_NUM_THREADS': '1'}, stdout=log,
                           stderr=subprocess.STDOUT, check=True, timeout=600)
        plots = sorted(folder.glob('*plt*.h5'))
        require(bool(plots), name + ': missing output')
        data = [load_plot(p, int(config['nblockx1'])) for p in plots]
        repairs = dict(line.split('=', 1) for line in (folder / 'state_repairs.txt').read_text().splitlines() if '=' in line)
        require(float(repairs['events']) == 0, name + ': floor repair in smooth solution')
        with (folder / 'gravity_solves.tsv').open() as stream:
            solves = list(csv.DictReader(stream, delimiter='\t'))
        require(bool(solves), name + ': missing solve trace')
        require(all(float(s['residual']) <= float(s['target']) for s in solves), name + ': unaccepted residual')
        require(len({s['generation'] for s in solves}) == len(solves), name + ': recycled field lease')
        record = dict(name=name, config=config, solves=len(solves), last_time=data[-1]['time'])
        if len(data) > 1:
            record.update(metrics(data[0], data[-1], float(config.get('phase', 0))))
            require(record['mass_relative_drift'] <= 1e-12, name + ': mass drift')
            if energy_budget is not None:
                require(record['energy_over_initial_potential'] <= energy_budget, name + ': energy drift')
        self.results.append(record)
        return data, folder, record

    def reject(self, name, expected, **changes):
        folder = self.output / name
        folder.mkdir(exist_ok=True)
        config = self.base | {'out_dir': str(folder)} | {k: str(v) for k, v in changes.items()}
        path = folder / 'input.par'
        path.write_text('\n'.join(f'{k}={v}' for k, v in config.items()) + '\n')
        result = subprocess.run([str(self.executable), 'JeansWave', str(path)], cwd=ROOT,
                                env=os.environ | {'OMP_NUM_THREADS': '1'}, capture_output=True, text=True, timeout=60)
        (folder / 'run.log').write_text(result.stdout + result.stderr)
        require(result.returncode != 0 and expected.lower() in (result.stdout + result.stderr).lower(), name + ': expected rejection missing')
        require(not list(folder.glob('*plt*.h5')), name + ': published output on rejected run')
        self.results.append(dict(name=name, rejected=True, reason=expected))

    def restart(self):
        common = dict(phase=math.pi/4, lrefinemax=1, nblockx1=8,
                      refine_threshold=5e-6, derefine_threshold=1e-6, cfl=.4,
                      chk_dstep=8, tmax=.1)
        _, continuous, _ = self.run('restart-continuous', **common)
        checkpoint = sorted(continuous.glob('*chk*.h5'))[1]
        _, resumed, _ = self.run('restart-resumed', **common, restart='true', restart_file=checkpoint)
        a, b = sorted(continuous.glob('*chk*.h5'))[-1], sorted(resumed.glob('*chk*.h5'))[-1]
        with h5py.File(a) as left, h5py.File(b) as right:
            names = []
            left.visititems(lambda name, obj: names.append(name) if isinstance(obj, h5py.Dataset) else None)
            for name in names:
                require(np.array_equal(left[name][:], right[name][:]), 'restart differs in ' + name)
            require(left.attrs['time'] == right.attrs['time'], 'restart time mismatch')
        self.results.append(dict(name='restart-identity', datasets=len(names), bitwise_equal=True))
        for key, value in [('gravity_G', G*1.01), ('gravity_rtol', 2e-10), ('gravity_atol', 1e-14), ('gravity_max_cycles', 201)]:
            self.reject('restart-reject-' + key, 'gravity policy/boundary/controls', restart='true', restart_file=checkpoint,
                        **(common | {key: value}))

    def full(self):
        for cells in [32, 64, 128]:
            _, _, record = self.run(f'spatial-{cells}', nblockx1=cells//16)
            require(record['initial_net_force'] <= 1e-11 and record['final_net_force'] <= 1e-11, 'uniform net force')
            if cells == 64:
                require(record['density_relative_rms'] <= .02, '64-cell Jeans analytic budget')
        reference, _, _ = self.run('temporal-reference', nblockx1=2, reconstruct='pcm', cfl=.00625, tmax=.3)
        finer, _, _ = self.run('temporal-reference-finer', nblockx1=2, reconstruct='pcm', cfl=.003125, tmax=.3)
        reference_error = np.linalg.norm(reference[-1]['DENS']-finer[-1]['DENS'])
        for method, target in [('Euler', .9), ('RK2', 1.8), ('RK3', 2.7)]:
            errors = []
            for cfl in [.8, .4, .2]:
                data, _, _ = self.run(f'temporal-{method}-{cfl}', energy_budget=None, nblockx1=2, reconstruct='pcm', cfl=cfl,
                                      time_integrator=method, tmax=.3)
                errors.append(float(np.linalg.norm(data[-1]['DENS']-reference[-1]['DENS'])))
            orders = np.log2(np.array(errors[:-1])/errors[1:])
            require(min(orders) >= target, method + ': temporal order')
            require(reference_error < .01*min(errors), method + ': unresolved reference')
            self.results.append(dict(name='temporal-order-'+method, errors=errors, orders=orders.tolist(), reference_change=float(reference_error)))
        for method in ['Euler', 'RK2']:
            self.run('energy-64-'+method, time_integrator=method)
        force_errors = []
        for cells in [64, 128]:
            data, folder, record = self.run(f'dynamic-amr-{cells}', nblockx1=cells//16, lrefinemax=1,
                phase=math.pi/4, refine_threshold=2e-5*(64/cells)**2, derefine_threshold=5e-6*(64/cells)**2,
                tmax=.4, plt_dstep=20)
            with (folder/'JeansWave_regrid.tsv').open() as stream:
                regrids = list(csv.DictReader(stream, delimiter='\t'))
            refined = any(int(r['new_blocks'])>int(r['old_blocks']) for r in regrids)
            coarsened = any(int(r['new_blocks'])<int(r['old_blocks']) for r in regrids)
            unchanged = any(r['topology_changed']=='0' for r in regrids)
            require(refined and coarsened and unchanged, 'dynamic refine/coarsen/no-change witnesses')
            require(any(len(set(d['level']))>1 for d in data), 'AMR run never sampled mixed levels')
            maximum = max(metrics(data[0], d, math.pi/4)['final_net_force'] for d in data)
            require(maximum <= 2e-3, 'AMR net-force budget')
            force_errors.append(maximum)
            record.update(maximum_net_force=maximum, refined=refined, coarsened=coarsened, unchanged=unchanged)
        # A roundoff-level symmetric force is an absolute witness, not a usable order estimate.
        require(force_errors[1] < force_errors[0] or max(force_errors) < 1e-11, 'AMR force does not improve')
        for dimension in [2, 3]:
            self.run(f'native-{dimension}d', nblockx1=2, nblockx2=1, nblockx3=int(dimension==3),
                     x2_max=.5, x3_max=.5, max_blocks=16, max_steps=2, tmax=.02)
            self.run(f'native-mixed-{dimension}d', nblockx1=4, nblockx2=1, nblockx3=int(dimension==3),
                     x2_max=.25, x3_max=.25, max_blocks=32, max_steps=2, tmax=.02,
                     phase=math.pi/4, lrefinemax=1, refine_threshold=2e-5, derefine_threshold=5e-6)
        self.restart()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arch', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    campaign = Campaign(args.arch.resolve(), args.output.resolve())
    box = BoxCampaign(campaign.executable, campaign.output/'box')
    try:
        if args.quick:
            _, _, record = campaign.run('jeans-64')
            require(record['density_relative_rms'] <= .02, 'Jeans analytic budget')
        else:
            campaign.full()
        box.run_checks(quick=args.quick)
        for changes, message in [({'gravity_rtol': 0}, 'gravity_rtol'), ({'gravity_atol': -1}, 'gravity_atol'),
                ({'gravity_max_cycles': 0}, 'gravity_max_cycles'), ({'gravity_boundary':'isolated'}, 'gravity_boundary'),
                ({'x1l_boundary_type':'outflow'}, 'periodic'), ({'gravity_boundary':'isolated', 'nblockx2':1, 'nblockx3':1}, 'isolated gravity requires')]:
            campaign.reject('reject-'+next(iter(changes)), message, **changes)
        status = 'passed'
    except Exception:
        status = 'failed'
        raise
    finally:
        report = dict(status=status, executable=str(campaign.executable),
                      executable_sha256=hashlib.sha256(campaign.executable.read_bytes()).hexdigest(),
                      results=campaign.results + box.results)
        (campaign.output/'summary.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'Self-gravity {status}: {len(report["results"])} acceptance records')


if __name__ == '__main__':
    main()
