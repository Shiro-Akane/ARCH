"""Actual-program Euler/SSPRK2/SSPRK3 temporal convergence.

For the periodic constant-pressure contact and PCM/HLLC, the density operator is
upwind advection: rho'_j = -u (rho_j-rho_{j-1})/dx for u>0 (reversed for u<0).
Its Fourier eigenvalue is -abs(u)*(1-cos(k*dx))/dx-i*u*sin(k*dx)/dx.
The exact exponential of that independently derived semi-discrete operator
isolates time error from spatial truncation. No production integrator supplies
the expected solution. Requires h5py/numpy and the ordinary runtime validators.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as runtime
import validation_provenance as provenance


def fourier_evolution(velocity, cell_width, wavenumber, time):
    theta = wavenumber * cell_width
    return (math.exp(-abs(velocity) * 2 * math.sin(theta / 2)**2 * time / cell_width),
            -velocity * math.sin(theta) * time / cell_width)


def errors(lane):
    import h5py
    import numpy as np
    values = runtime.read_parameter_map(Path(lane['parameter_file']))
    with h5py.File(lane['checkpoint']) as data:
        if np.any(data['Blocks/level'][:] != 0):
            raise ValueError('temporal oracle requires a uniform mesh')
        n = int(data.attrs['cells_per_block'])
        length = float(values['x1_max']) - float(values['x1_min'])
        dx = length / (int(values['nblockx1']) * n)
        wavenumber = 2 * math.pi * int(values['mode']) / length
        time = float(data.attrs['time'])
        if time != lane['terminal_time']:
            raise ArithmeticError('temporal oracle time drifted')
        rho = data['Data/rho'][:]
        locations = (data['Blocks/logical_x1'][:][:, None] * n + np.arange(n) + .5) * dx
        velocity, pressure = float(values['velocity0']), float(values['pressure0'])
        mean, amplitude = float(values['rho_mean']), float(values['rho_amplitude'])
        damping, phase = fourier_evolution(velocity, dx, wavenumber, time)
        sinc = math.sin(wavenumber * dx / 2) / (wavenumber * dx / 2)
        expected = mean + amplitude * sinc * damping * np.sin(wavenumber * locations + phase)
        if rho.shape != expected.shape or not np.all(np.isfinite(rho)) or not np.all(rho > 0):
            raise ArithmeticError('invalid temporal density field')
        difference = np.abs(rho - expected)
        mass_drift = abs(float(np.mean(rho)) - mean)
        pressure_observed = (float(values['gamma']) - 1) * (
            data['Data/eng'][:] - .5 * sum(data['Data/mom_' + component][:]**2
                                         for component in ('u', 'v', 'w')) / rho)
        pressure_error = float(np.max(np.abs(pressure_observed - pressure)))
        velocity_error = max(float(np.max(np.abs(data['Data/mom_' + component][:] / rho - expected_velocity)))
                             for component, expected_velocity in (('u', velocity), ('v', 0.), ('w', 0.)))
        if mass_drift > 1e-12 * mean or pressure_error > 1e-12 * pressure \
                or velocity_error > 1e-12 * max(1., abs(velocity)):
            raise ArithmeticError('periodic contact invariants failed')
        return dict(l1=float(np.mean(difference)), l2=float(np.sqrt(np.mean(difference**2))),
                    linf=float(np.max(difference)), mass_drift=mass_drift,
                    pressure_error=pressure_error, velocity_error=velocity_error,
                    accepted_steps=lane['steps'], physical_time=time)


def temporal_orders(samples, cfls, minimum):
    if not math.isfinite(minimum) or minimum <= 0:
        raise ValueError('temporal order threshold is invalid')
    if len(samples) != len(cfls) or len(samples) < 3 or not all(
            math.isfinite(error) and error > 0 for error in samples):
        raise ArithmeticError('incomplete or nonfinite temporal errors')
    if not all(math.isfinite(cfl) and cfl > 0 for cfl in cfls) or not all(
            coarse > fine for coarse, fine in zip(cfls, cfls[1:])):
        raise ValueError('temporal CFL refinement is invalid')
    orders = [math.log(a / b) / math.log(coarse / fine)
              for a, b, coarse, fine in zip(samples, samples[1:], cfls, cfls[1:])]
    if min(orders) < minimum:
        raise ArithmeticError(f'temporal convergence lost order {minimum}: {orders}')
    return orders


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
    identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
    identity = provenance.capture(**identity_args)
    cfls = (.4, .2, .1)
    cases = [dict(id=f'{method.lower()}_cfl_{index}', problem='SmoothAdvection',
        input='validation/hydro/inputs/pcm_n64.par', accepted_steps=[], scientific_time=.1,
        overrides=dict(time_integrator=method, cfl=str(cfl)),
        plan_policy=dict(eos='ideal', reconstruction='pcm', time=method.lower()),
        reduction_policy=dict(rtol=2e-10, atol=2e-12))
        for method in ('Euler', 'RK2', 'RK3') for index, cfl in enumerate(cfls)]
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    started, records = datetime.now(timezone.utc).isoformat(), []
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output)
        record['temporal_errors'] = {backend: errors(record['scientific'][backend])
                                     for backend in ('cpu', 'cuda')}
        records.append(record)
    orders = {}
    for index, (method, minimum) in enumerate((('Euler', .9), ('RK2', 1.8), ('RK3', 2.7))):
        for backend in ('cpu', 'cuda'):
            samples = [item['temporal_errors'][backend]['l1'] for item in records[index*3:index*3+3]]
            orders[method + '_' + backend] = temporal_orders(samples, cfls, minimum)
    if inputs != runtime.runtime_case_inputs(cases, ROOT):
        raise RuntimeError('temporal reference inputs changed during execution')
    evidence = dict(schema=1, scope='actual-ARCH-semidiscrete-hydro-time-order',
        focused_gate_pass=True, release_qualified=False, cases=records, derived_cases=cases,
        runtime_inputs=inputs, cfls=cfls, orders=orders, started_utc=started,
        finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, identity, **identity_args)
    print(json.dumps(dict(focused_gate_pass=True, orders=orders, evidence=str(output / 'evidence.json'))))


if __name__ == '__main__':
    main()
