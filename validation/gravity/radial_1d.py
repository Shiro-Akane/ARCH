"""Independent 1D spherical/cylindrical field, domain and AMR acceptance.

Uses the real GravityBox executable. All integrals use native radial shell
volumes, not the Cartesian approximation in older box campaigns. CGS units.
"""
import csv
import math
import os
from pathlib import Path
import subprocess

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
G = 6.67430e-8
RADIUS = 1e8
RHO = 1e7


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_plot(path, geometry, blocks):
    with h5py.File(path) as f:
        fields = {name: f['Data/' + name][:].ravel() for name in f['Data']}
        radius = f['Grid/x'][:].ravel()
        levels = f['Grid/level'][:].ravel()
        require(len(radius) == len(fields['DENS']), 'radial plot coordinate extent')
        leaf_level = np.repeat(levels, len(radius) // len(levels))
        width = RADIUS / (16 * blocks * 2. ** leaf_level)
        left, right = radius - width / 2, radius + width / 2
        if geometry == 'spherical':
            volume = width * (right * right + right * left + left * left) / 3
        else:
            volume = width * (right + left) / 2
        data = dict(fields, radius=radius, levels=levels, left=left,
                    right=right, volume=volume.astype(np.longdouble),
                    time=float(f.attrs['time']))
    for key, value in data.items():
        require(np.all(np.isfinite(value)), str(path) + ': nonfinite ' + key)
    require(np.all(data['volume'] > 0), str(path) + ': nonpositive shell')
    return data


def gauss_error(data, geometry, constant=G):
    """Compare native acceleration with an independent enclosed-mass integral."""
    order = np.argsort(data['radius'])
    radius = data['radius'][order]
    left, right = data['left'][order], data['right'][order]
    require(np.all(left[1:] >= right[:-1] - 1e-7), 'overlapping radial leaves')
    require(left[0] == 0 and abs(right[-1] - RADIUS) < 1e-7,
            'radial domain does not include the full origin-to-outer interval')
    mass = np.cumsum((data['DENS'] * data['volume'])[order], dtype=np.longdouble)
    interior = np.concatenate(([np.longdouble(0)], mass[:-1]))
    low = np.zeros(len(radius))
    if geometry == 'spherical':
        low[1:] = -4 * math.pi * constant * np.asarray(interior[1:], float) / left[1:] ** 2
        high = -4 * math.pi * constant * np.asarray(mass, float) / right ** 2
    else:
        low[1:] = -4 * math.pi * constant * np.asarray(interior[1:], float) / left[1:]
        high = -4 * math.pi * constant * np.asarray(mass, float) / right
    expected = (low + high) / 2
    measured = data['GACX'][order]
    return float(np.max(np.abs(measured - expected)) / np.max(np.abs(expected)))


def totals(data):
    volume = data['volume']
    density = data['DENS'].astype(np.longdouble)
    gas = np.sum(volume * data['ENER'], dtype=np.longdouble)
    potential = np.sum(volume * density * data['GPOT'] / 2, dtype=np.longdouble)
    return np.sum(volume * density, dtype=np.longdouble), gas + potential


class RadialCampaign:
    def __init__(self, executable, output):
        self.executable = Path(executable).resolve()
        self.output = Path(output).resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.base = dict(line.split('=', 1) for line in
                         (ROOT / 'simulation/GravityBox/GravityBox.par').read_text().splitlines()
                         if '=' in line and not line.startswith('#'))
        self.results = []

    def config(self, geometry, folder, **changes):
        values = self.base | dict(
            geometry=geometry, gravity_boundary='isolated', compute_backend='cpu',
            x1l_boundary_type='reflecting', x1r_boundary_type='reflecting',
            x1_min=0, x1_max=RADIUS, nblockx2=0, nblockx3=0,
            rho0=RHO, temperature0=1e7, amplitude=0,
            out_dir=str(folder), plt_variables='DENS,ENER,VELX',
        ) | {key: str(value) for key, value in changes.items()}
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / 'input.par'
        path.write_text('\n'.join(f'{key}={value}' for key, value in values.items()) + '\n')
        return values, path

    def run(self, name, geometry, **changes):
        folder = self.output / name
        config, path = self.config(geometry, folder, **changes)
        with (folder / 'run.log').open('w') as log:
            result = subprocess.run([str(self.executable), 'GravityBox', str(path)],
                                    cwd=ROOT, env=os.environ | {'OMP_NUM_THREADS': '1'},
                                    stdout=log, stderr=subprocess.STDOUT, timeout=300)
        require(result.returncode == 0, name + ': simulation failed; see ' + str(folder / 'run.log'))
        plots = [read_plot(p, geometry, int(config['nblockx1']))
                 for p in sorted(folder.glob('*plt*.h5'))]
        require(bool(plots), name + ': missing plot')
        with (folder / 'gravity_solves.tsv').open() as stream:
            solves = list(csv.DictReader(stream, delimiter='\t'))
        require(bool(solves) and all(float(row['residual']) <= float(row['target'])
                                     for row in solves), name + ': unpublished Poisson residual')
        repairs = dict(line.split('=', 1) for line in
                       (folder / 'state_repairs.txt').read_text().splitlines() if '=' in line)
        require(float(repairs['events']) == 0, name + ': floor repair')
        errors = [gauss_error(data, geometry, float(config.get('gravity_G', G)))
                  for data in plots]
        require(max(errors) < 1e-7, name + ': AMR/origin Gauss-law field error')
        record = dict(name=name, geometry=geometry, plots=len(plots),
                      max_gauss_relative_error=max(errors), solves=len(solves))
        self.results.append(record)
        return plots, folder, record

    def static(self, geometry, roots):
        name = f'{geometry}-static-{16 * roots}'
        plots, _, record = self.run(name, geometry, nblockx1=roots,
                                    tmax=0, max_steps=0)
        data = plots[0]
        r = data['radius']
        if geometry == 'spherical':
            phi = -2 * math.pi * G * RHO * (RADIUS ** 2 - r ** 2 / 3)
            force = -4 * math.pi * G * RHO * r / 3
        else:
            phi = math.pi * G * RHO * (r ** 2 - RADIUS ** 2)
            force = -2 * math.pi * G * RHO * r
        potential_error = float(np.max(np.abs(data['GPOT'] - phi)) / np.max(np.abs(phi)))
        force_error = float(np.max(np.abs(data['GACX'] - force)) / np.max(np.abs(force)))
        require(potential_error < 1e-7 and force_error < 1e-7,
                name + ': constant-density analytic sphere/cylinder mismatch')
        record.update(potential_relative_error=potential_error,
                      force_relative_error=force_error)
        return record

    def dynamic_and_restart(self, geometry):
        name = geometry + '-dynamic-amr'
        common = dict(nblockx1=4, amplitude=.2, width=2e7,
                      lrefinemax=1, refine_threshold=.004,
                      derefine_threshold=.001, regrid_interval=2,
                      max_steps=12, tmax=.04, plt_dstep=2, chk_dstep=4)
        plots, folder, record = self.run(name, geometry, **common)
        require(len(plots) >= 3 and len(set(plots[0]['levels'])) == 2,
                name + ': initial AMR did not retain mixed levels')
        with (folder / 'GravityBox_regrid.tsv').open() as stream:
            regrids = list(csv.DictReader(stream, delimiter='\t'))
        require(any(int(row['new_blocks']) > int(row['old_blocks']) for row in regrids)
                and any(row['topology_changed'] == '0' for row in regrids),
                name + ': refine/no-change lifecycle not exercised')
        mass0, energy0 = totals(plots[0])
        mass1, energy1 = totals(plots[-1])
        mass_error = float(abs(mass1 / mass0 - 1))
        energy_error = float(abs(energy1 / energy0 - 1))
        require(mass_error < 1e-12, name + ': closed radial domain mass drift')
        # Short non-equilibrium collapse: require the gravitational and gas
        # energy exchange error to stay below 5e-5 of the initial total.
        require(energy_error < 5e-5, name + ': closed radial domain energy drift')
        record.update(mass_relative_drift=mass_error,
                      energy_relative_drift=energy_error,
                      initial_leaves=int(regrids[0]['old_blocks']),
                      final_leaves=int(regrids[-1]['new_blocks']))
        checkpoint = folder / 'GravityBox_chk_0001.h5'
        require(checkpoint.exists(), name + ': no mid-run checkpoint')
        _, resumed, _ = self.run(geometry + '-restart', geometry,
                                 **(common | dict(restart='true', restart_file=checkpoint)))
        direct = folder / 'GravityBox_chk_0003.h5'
        recovered = resumed / 'GravityBox_chk_0003.h5'
        with h5py.File(direct) as a, h5py.File(recovered) as b:
            names = []
            a.visititems(lambda path, value: names.append(path)
                         if isinstance(value, h5py.Dataset) else None)
            require(a.attrs['time'] == b.attrs['time'] and
                    all(np.array_equal(a[path][:], b[path][:]) for path in names),
                    name + ': checkpoint continuation changed state')
        record['restart_bitwise_equal'] = True

    def regrid_cycle(self, geometry):
        """Exercise actual refine, coarsen and unchanged topology publications."""
        name = geometry + '-refine-coarsen'
        plots, folder, record = self.run(
            name, geometry, nblockx1=4, rho0=RHO, amplitude=.2, width=2e7,
            gravity_G=1e-20, temperature0=1e9, lrefinemax=1,
            refine_threshold=.01, derefine_threshold=.005,
            regrid_interval=2, max_steps=40, tmax=.1, plt_dstep=20)
        with (folder / 'GravityBox_regrid.tsv').open() as stream:
            rows = list(csv.DictReader(stream, delimiter='\t'))
        require(any(int(row['new_blocks']) > int(row['old_blocks']) for row in rows)
                and any(int(row['new_blocks']) < int(row['old_blocks']) for row in rows)
                and any(row['topology_changed'] == '0' for row in rows),
                name + ': refine/coarsen/no-change topology not observed')
        mass0, energy0 = totals(plots[0])
        mass1, energy1 = totals(plots[-1])
        mass_error = float(abs(mass1 / mass0 - 1))
        energy_error = float(abs(energy1 / energy0 - 1))
        require(mass_error < 1e-12 and energy_error < 1e-12,
                name + ': closed-domain conservation across regrid cycle')
        record.update(mass_relative_drift=mass_error,
                      energy_relative_drift=energy_error,
                      refined_and_coarsened=True)

    def hydrostatic(self, geometry, quick):
        """Measure parasitic radial velocity against an independent ideal-gas balance."""
        previous = None
        for roots in ([2, 4] if quick else [1, 2, 4]):
            name = f'{geometry}-hydrostatic-{16 * roots}'
            plots, _, record = self.run(name, geometry, nblockx1=roots,
                                        hydrostatic_radial='true', temperature0=5e8,
                                        amplitude=0, max_steps=40, tmax=.02, plt_dstep=40)
            require(abs(plots[-1]['time'] - .02) < 1e-12,
                    name + ': hydrostatic cases must have the same physical time')
            speed = plots[-1]['VELX']
            volume = plots[-1]['volume']
            rms = float(np.sqrt(np.average(speed * speed, weights=volume)))
            cv, gamma = 1.2471693927e8, 5 / 3
            sound = math.sqrt(gamma * (gamma - 1) * cv * 5e8)
            require(rms / sound < .01, name + ': large parasitic velocity')
            if previous is not None:
                require(rms < previous, name + ': hydrostatic error did not decrease on refinement')
            previous = rms
            record.update(parasitic_velocity_rms=rms,
                          relative_to_center_sound_speed=rms / sound)

    def low_density(self, geometry):
        name = geometry + '-near-vacuum'
        plots, _, record = self.run(name, geometry, nblockx1=4, rho0=1e-12,
                                    amplitude=.2, width=2e7, max_steps=2, tmax=.01)
        require(len(plots) >= 2 and np.min(plots[-1]['DENS']) > 0,
                name + ': density lost positivity')
        record['minimum_density'] = float(np.min(plots[-1]['DENS']))

    def reject(self, geometry, reason, **changes):
        name = geometry + '-reject-' + reason.replace(' ', '-')
        folder = self.output / name
        _, path = self.config(geometry, folder, **changes)
        result = subprocess.run([str(self.executable), 'GravityBox', str(path)],
                                cwd=ROOT, env=os.environ | {'OMP_NUM_THREADS': '1'},
                                capture_output=True, text=True, timeout=60)
        (folder / 'run.log').write_text(result.stdout + result.stderr)
        require(result.returncode != 0 and reason.lower() in
                (result.stdout + result.stderr).lower(),
                name + ': expected invalid configuration was accepted')
        require(not list(folder.glob('*plt*.h5')), name + ': rejected run published data')
        self.results.append(dict(name=name, rejected=True))

    def run_checks(self, quick=False):
        for geometry in ('spherical', 'cylindrical'):
            for roots in ([4] if quick else [1, 2, 4]):
                self.static(geometry, roots)
            self.dynamic_and_restart(geometry)
            self.regrid_cycle(geometry)
            self.hydrostatic(geometry, quick)
            self.low_density(geometry)
            self.reject(geometry, 'requires isolated', gravity_boundary='periodic')
            self.reject(geometry, 'radial inner boundary', x1l_boundary_type='outflow')
            self.reject(geometry, 'nonnegative radius', x1_min=-1)
        self.reject('spherical', 'Cartesian or 1D', nblockx2=1)
