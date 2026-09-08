"""Independent planar Sedov similarity and actual CPU/CUDA shock verification.

Kamm, LA-UR-00-6055 (2000), equations 13--16, 29--48:
https://cococubed.com/papers/kamm_2000.pdf
The standard uniform-density solution is evaluated in log(x2), avoiding a
subtraction of nearly equal chemical-like similarity coordinates at the origin.
The one-sided energy integral in Kamm is doubled for ARCH's two-sided planar
blast. No production hydro, EOS, or time integrator supplies expected fields.

ARCH deposits energy in a finite region. In the convergence sequence that
region occupies two cells and shrinks with dx; its measured initial energy
must equal the prescribed energy. This checks the point-explosion limit, not
an assertion that a finite-radius initialization is an exact similarity state.
Requires NumPy, SciPy and h5py. Run --oracle-only before the application profile.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys

import h5py
import numpy as np
from scipy.integrate import quad
from scipy.optimize import brentq

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance


class PlanarSimilarity:
    """Uniform-density, non-vacuum planar solution with a polytropic EOS."""
    def __init__(self, gamma=1.4, quadrature_tolerance=1e-12):
        if not math.isfinite(gamma) or not 1 < gamma < 2:
            raise ValueError('this reference covers 1 < gamma < 2')
        self.gamma = g = gamma
        self.a, self.b, self.c = 3 * (g + 1) / 4, (g + 1) / (g - 1), 3 * g / 2
        self.e = (g + 1) / 2
        self.d = 3 * (g + 1) / (3 * (g + 1) - 2 * (g + 1))
        self.a0 = 2 / 3
        self.a2 = -(g - 1) / (2 * (g - 1) + 1)
        self.a1 = 3 * g / (g + 1) * (2 * (2 - g) / (9 * g) - self.a2)
        self.a3 = 1 / (2 * (g - 1) + 1)
        self.a4 = 3 / (2 - g) * self.a1
        self.a5 = -2 / (2 - g)

        def integrand(z):
            radius, density, velocity, pressure, derivative = self.parametric(z)
            return (.5 * density * velocity**2 + pressure / (g - 1)) * radius * derivative
        integral, error = quad(integrand, -np.inf, 0., epsabs=quadrature_tolerance,
                               epsrel=quadrature_tolerance, limit=200)
        self.alpha_half = self.a0**2 * integral
        self.energy_quadrature_error = self.a0**2 * error
        if not math.isfinite(self.alpha_half) or self.alpha_half <= 0 \
                or error > 10 * quadrature_tolerance * max(1., abs(integral)):
            raise ArithmeticError('Sedov energy normalization did not converge')

    def parametric(self, z):
        v = (1 + math.exp(z) / self.b) / self.c
        dv = math.exp(z) / (self.b * self.c)
        l1 = math.log(self.a * v)
        l3 = math.log(self.d * (1 - self.e * v))
        l4 = math.log(self.b * (1 - self.c * v / self.gamma))
        lr = -self.a0 * l1 - self.a2 * z - self.a1 * l3
        radius = math.exp(lr)
        derivative = -self.a0 * dv / v - self.a2 + self.a1 * self.e * dv / (1 - self.e * v)
        density = self.b * math.exp(self.a3 * z + self.a4 * l3 + self.a5 * l4)
        velocity = 2 / (self.gamma + 1) * math.exp(l1 + lr)
        pressure = 2 / (self.gamma + 1) * math.exp(
            self.a0 * l1 + (self.a4 - 2 * self.a1) * l3 + (1 + self.a5) * l4)
        return radius, density, velocity, pressure, derivative

    def interior(self, radius):
        if not 0 < radius <= 1:
            raise ValueError('similarity radius must lie in (0, 1]')
        lower = -1.
        while self.parametric(lower)[0] > radius:
            lower *= 2
        z = brentq(lambda value: self.parametric(value)[0] - radius,
                   lower, 0., xtol=5e-14, rtol=1e-14)
        return self.parametric(z)[1:4]

    def shock(self, time, energy, density):
        radius = (energy * time**2 / (2 * self.alpha_half * density))**(1 / 3)
        return radius, self.a0 * radius / time

    def cell_averages(self, edges, time, energy, density, ambient_pressure, order):
        radius, speed = self.shock(time, energy, density)
        nodes, weights = np.polynomial.legendre.leggauss(order)
        averages = []
        for left, right in zip(edges[:-1], edges[1:]):
            cuts = [left, *(point for point in (-radius, 0., radius) if left < point < right), right]
            total = np.zeros(3)
            for low, high in zip(cuts[:-1], cuts[1:]):
                locations = (high + low) / 2 + (high - low) / 2 * nodes
                values = []
                for x in locations:
                    if abs(x) >= radius:
                        values.append((density, 0., ambient_pressure / (self.gamma - 1)))
                    else:
                        r, u, p = self.interior(abs(x) / radius)
                        rho, velocity, pressure = density * r, math.copysign(speed * u, x), density * speed**2 * p
                        values.append((rho, rho * velocity, pressure / (self.gamma - 1) + .5 * rho * velocity**2))
                total += (high - low) / 2 * np.dot(weights, values)
            averages.append(total / (right - left))
        return np.asarray(averages)


def verify_oracle():
    reference, refined = PlanarSimilarity(), PlanarSimilarity(quadrature_tolerance=1e-13)
    # Independent printed data, Kamm Table 1 (Exact columns), not ARCH samples.
    published = ((.9797, .9699, .8620, .9159), (.7419, .6677, .2201, .4905),
                 (.4912, .4244, .0641, .4037), (.1040, .0891, .0013, .3900))
    max_error = 0.
    for radius, f, g, h in published:
        rho, u, p = reference.interior(radius)
        actual = (u * (reference.gamma + 1) / 2, rho / reference.b, p * (reference.gamma + 1) / 2)
        max_error = max(max_error, *(abs(a - b) for a, b in zip(actual, (f, g, h))))
    if max_error > 5e-5 or abs(reference.alpha_half - .538548) > 5e-7 \
            or abs(reference.alpha_half - refined.alpha_half) > 1e-11:
        raise ArithmeticError('independent published Sedov reference or quadrature check failed')
    mass, error = quad(lambda z: reference.parametric(z)[1] * reference.parametric(z)[0]
                      * reference.parametric(z)[4], -np.inf, 0., epsabs=1e-12, epsrel=1e-12)
    if abs(mass - 1) > 1e-11 or error > 1e-11:
        raise ArithmeticError('similarity swept-mass identity failed')
    shock = reference.parametric(0.)
    if max(abs(a - b) for a, b in zip(shock[:4], (1., 6., 5/6, 5/6))) > 1e-12:
        raise ArithmeticError('strong-shock jump conditions failed')
    return dict(source='https://cococubed.com/papers/kamm_2000.pdf',
                alpha_half=reference.alpha_half, printed_table_max_absolute_error=max_error,
                normalization_refinement_error=abs(reference.alpha_half - refined.alpha_half),
                swept_mass_error=abs(mass - 1))


def load_state(path):
    with h5py.File(path) as data:
        if np.any(data['Blocks/level'][:] != 0):
            raise ValueError('Sedov profile oracle requires a uniform mesh')
        order = np.argsort(data['Blocks/logical_x1'][:])
        fields = np.stack([data['Data/' + name][:][order].ravel()
                           for name in ('rho', 'mom_u', 'eng')], axis=1)
        if not np.all(np.isfinite(fields)) or not np.all(fields[:, 0] > 0):
            raise ArithmeticError('nonfinite or nonpositive Sedov state')
        if any(np.any(data['Data/mom_' + axis][:] != 0) for axis in ('v', 'w')):
            raise ArithmeticError('planar blast developed transverse momentum')
        return fields, float(data.attrs['time'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--oracle-only', action='store_true')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    checks = verify_oracle()
    if args.oracle_only:
        print(json.dumps(checks, indent=2))
        return
    if args.build_dir is None or args.output_dir is None:
        parser.error('application verification requires build and output directories')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
    identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
    before, started = provenance.capture(**identity_args), datetime.now(timezone.utc).isoformat()
    reference, time, energy, ambient, background = PlanarSimilarity(), .1, 1., 1., 1e-5
    cases = [dict(id=f'sedov_planar_n{n}', problem='Sedov',
        input='validation/hydro/inputs/sod_ppm_n64.par', accepted_steps=[], scientific_time=time,
        timeout_seconds=1200, overrides=dict(nblockx1=str(n // 16), max_blocks=str(n // 16 + 8),
            center_x='.5', deposit_radius=str(1 / n), ambient_density=str(ambient),
            ambient_pressure=str(background), explosion_energy=str(energy)),
        plan_policy=dict(eos='ideal', reconstruction='ppm', time='rk3'),
        reduction_policy=dict(rtol=2e-8, atol=1e-12)) for n in (128, 256, 512)]
    budgets = dict(normalized_l1_max=.04, normalized_l2_max=.10, finest_shock_cells_max=3.,
                   final_pair_l1_order_min=.5, mass_energy_relative_max=1e-10,
                   symmetry_scaled_max=1e-10, reference_quadrature_scaled_max=1e-10)
    inputs, records = runtime.runtime_case_inputs(cases, ROOT), []
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output / 'runs')
        n = int(case['overrides']['nblockx1']) * 16
        edges = np.linspace(-.5, .5, n + 1)
        expected = reference.cell_averages(edges, time, energy, ambient, background, 24)
        refined = reference.cell_averages(edges, time, energy, ambient, background, 48)
        radius, speed = reference.shock(time, energy, ambient)
        scale = np.array([reference.b * ambient, speed, ambient * speed**2, energy / (2 * radius)])
        def primitives(state):
            rho, momentum, eng = state.T
            return np.stack((rho, momentum / rho,
                (reference.gamma - 1) * (eng - .5 * momentum**2 / rho), eng), axis=1)
        expected_primitive = primitives(expected)
        quadrature_error = float(np.max(np.abs(primitives(refined) - expected_primitive) / scale))
        if quadrature_error > budgets['reference_quadrature_scaled_max']:
            raise ArithmeticError('reference finite-volume quadrature did not converge')
        record['similarity'] = dict(shock_radius=radius, shock_speed=speed,
            quadrature_scaled_error=quadrature_error, fields=['density', 'velocity', 'pressure', 'energy'])
        for backend in ('cpu', 'cuda'):
            lane = record['scientific'][backend]
            initial_path, = Path(lane['parameter_file']).parent.glob('*_chk_0000.h5')
            initial, initial_time = load_state(initial_path)
            actual, actual_time = load_state(lane['checkpoint'])
            if initial_time != 0 or actual_time != time or actual.shape != expected.shape:
                raise ArithmeticError('Sedov shape or physical time mismatch')
            initial_energy = float(np.mean(initial[:, 2]) - background / (reference.gamma - 1))
            energy_error = abs(float(np.mean(actual[:, 2]) - np.mean(initial[:, 2]))) / energy
            mass_error = abs(float(np.mean(actual[:, 0]) - ambient)) / ambient
            deposition_error = abs(initial_energy - energy) / energy
            physical = primitives(actual)
            if np.any(physical[:, 2] <= 0):
                raise ArithmeticError('Sedov pressure is nonpositive')
            error = np.abs(physical - expected_primitive) / scale
            l1, l2 = np.mean(error, axis=0), np.sqrt(np.mean(error**2, axis=0))
            reflection = physical[::-1].copy()
            reflection[:, 1] *= -1
            symmetry = float(np.max(np.abs(physical - reflection) / scale))
            # Outer pressure half-jump locates the shock independently of a
            # possibly broad/nonmonotone density peak. Interpolate its crossing.
            center = .5 * (edges[:-1] + edges[1:])
            half_jump = ambient * speed**2 / (reference.gamma + 1)
            crossings = np.where((center[:-1] > 0) & (physical[:-1, 2] >= half_jump)
                                 & (physical[1:, 2] < half_jump))[0]
            if len(crossings) != 1:
                raise ArithmeticError('Sedov shock crossing is not unique')
            j = crossings[0]
            observed_radius = center[j] + (half_jump - physical[j, 2]) / (physical[j+1, 2] - physical[j, 2]) / n
            shock_cells = abs(observed_radius - radius) * n
            metrics = dict(l1=l1.tolist(), l2=l2.tolist(), symmetry_scaled=symmetry,
                mass_relative=mass_error, energy_relative=energy_error,
                deposition_relative=deposition_error, shock_radius=observed_radius,
                shock_error_cells=shock_cells, cells=n, physical_time=actual_time)
            record['similarity'][backend] = metrics
            if max(mass_error, energy_error, deposition_error) > budgets['mass_energy_relative_max'] \
                    or symmetry > budgets['symmetry_scaled_max'] \
                    or np.max(l1) > budgets['normalized_l1_max'] \
                    or np.max(l2) > budgets['normalized_l2_max'] \
                    or shock_cells > budgets['finest_shock_cells_max']:
                print(json.dumps(metrics), flush=True)
                raise ArithmeticError(f'{case["id"]} {backend} failed independent Sedov budgets')
        records.append(record)
        print('Sedov similarity PASS: ' + case['id'], flush=True)
    orders = {backend: (np.log(np.array(records[-2]['similarity'][backend]['l1']) /
                               np.array(records[-1]['similarity'][backend]['l1'])) / np.log(2)).tolist()
              for backend in ('cpu', 'cuda')}
    if any(min(value) < budgets['final_pair_l1_order_min'] for value in orders.values()):
        raise ArithmeticError('Sedov point-deposition sequence failed convergence: ' + str(orders))
    if inputs != runtime.runtime_case_inputs(cases, ROOT):
        raise RuntimeError('Sedov inputs changed during execution')
    evidence = dict(schema=1, scope='planar strong-shock independent similarity', focused_gate_pass=True,
        release_qualified=False, oracle=checks, derived_cases=cases, runtime_inputs=inputs,
        budgets=budgets, cases=records, final_pair_orders=orders, started_utc=started,
        finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('Sedov application profile PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
