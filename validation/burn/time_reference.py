"""Read-only independent TIME-integration review of the built-in burn models.

The C++ test serves the shared reaction/EOS RHS, not its ODE algorithm or
Jacobian. SciPy DOP853 and Radau, each at two temporal resolutions, must agree
with the immutable endpoint data. This does not independently certify nuclear
rates. Endpoint energy is separately checked with the existing high-precision
Helm monomial-fit oracle, not a copied EOS. Never rewrites test fixtures.

Run under tools/run_memory_guarded.py. Requires numpy/scipy/mpmath.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

import mpmath as mp
import numpy as np
from scipy.integrate import solve_ivp

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'validation/eos'))
import validation_provenance as provenance
from helm_reference import HelmReference, TABLE, IMAX, JMAX


def vector(stream, size):
    tokens = stream.readline().split()
    if len(tokens) != size:
        raise ValueError('burn reference protocol extent mismatch')
    result = np.array([float(token) for token in tokens])
    if not np.all(np.isfinite(result)):
        raise ValueError('burn reference protocol nonfinite result')
    return result


def review(binary, name, free_energy, eta, *, derive_reference=False):
    # This is a synchronous numerical query channel, not a new job supervisor.
    # The common outer memory guard owns this process and its child.
    child = subprocess.Popen([str(binary), '--rhs', name], cwd=ROOT,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    try:
        for line in child.stdout:
            if line.startswith('BURN_RHS_READY '):
                _, size, rho, interval = line.split()
                size, rho, interval = int(size), float(rho), float(interval)
                break
        else:
            raise RuntimeError('burn RHS server did not initialize')
        initial = vector(child.stdout, size)
        expected = vector(child.stdout, size)
        aion = vector(child.stdout, size - 1)
        zion = vector(child.stdout, size - 1)

        def query(state):
            child.stdin.write(' '.join(format(float(x), '.17g') for x in state) + '\n')
            child.stdin.flush()
            return vector(child.stdout, size + 2)

        energy_initial = float(query(initial)[-2])
        if not energy_initial > 0:
            raise ArithmeticError('independent burn review needs positive initial energy')

        def rhs(_time, state):
            values = query(state[:size])
            # Independent passive quadrature of the nuclear/signed energy rate.
            # It never feeds back into the served composition/temperature RHS.
            return np.r_[values[:size], values[-1] / energy_initial] * interval

        records = []
        endpoints = []
        for method in ('DOP853', 'Radau'):
            for steps in (1, 8):
                solution = solve_ivp(rhs, (0.0, 1.0), np.r_[initial, 0.0], method=method,
                    rtol=3.e-14, atol=1.e-15, max_step=1.0 / steps)
                if not solution.success or solution.t[-1] != 1.0:
                    raise ArithmeticError('independent time integrator did not complete')
                endpoint = solution.y[:size, -1]
                # Review precision is much tighter than the production 1e-8
                # accuracy gate. It is not a CPU/GPU measured-parity budget.
                species_error = float(np.max(abs(endpoint[:-1] - expected[:-1])))
                temperature_error = float(abs(endpoint[-1] / expected[-1] - 1.0))
                if not derive_reference and (species_error > 1.e-12 or temperature_error > 1.e-12):
                    raise ArithmeticError('immutable time reference failed independent review')
                endpoints.append(endpoint)
                records.append({'method': method, 'max_step_fraction': 1.0 / steps,
                    'rhs_calls': solution.nfev, 'species_linf': species_error,
                    'relative_temperature': temperature_error,
                    'endpoint_hex': [float(x).hex() for x in endpoint],
                    'integrated_energy': float(solution.y[-1, -1] * energy_initial)})

        authority = endpoints[0] if derive_reference else expected
        for endpoint in endpoints:
            if np.max(abs(endpoint[:-1] - authority[:-1])) > 1.e-12 or abs(endpoint[-1]/authority[-1] - 1.0) > 1.e-12:
                raise ArithmeticError('independent DOP853/Radau time refinements disagree')

        # The already-independent EOS oracle accepts composition through its
        # thermodynamic moments. Reuse it at both precisions; no new EOS body.
        energies = []
        for digits in (60, 80):
            with mp.workdps(digits):
                model = HelmReference(free_energy, eta)
                def independent_energy(state):
                    model.ytot = mp.fsum(mp.mpf(float(x)) / mp.mpf(float(a))
                                        for x, a in zip(state[:-1], aion))
                    model.ye = mp.fsum(mp.mpf(float(x)) * mp.mpf(float(z)) / mp.mpf(float(a))
                                      for x, a, z in zip(state[:-1], aion, zion))
                    return model.state(mp.mpf(rho), mp.mpf(float(state[-1])))['E']
                start_energy = independent_energy(initial)
                end_energy = independent_energy(authority)
                energies.append((float(end_energy), float(end_energy - start_energy)))
        if tuple(value.hex() for value in energies[0]) != tuple(value.hex() for value in energies[1]):
            raise ArithmeticError('independent endpoint EOS differs with precision')
        host_energy = float(query(authority)[-2])
        energy_error = abs(host_energy / energies[1][0] - 1.0)
        if energy_error > 4096 * np.finfo(float).eps:
            raise ArithmeticError('endpoint EOS failed independent monomial-fit control')
        for record in records:
            energy_error_abs = abs(record['integrated_energy'] - energies[1][1])
            energy_budget = (1.e-9 * abs(energies[1][1])
                             + 4096 * np.finfo(float).eps * max(energy_initial, energies[1][0]))
            if energy_error_abs > energy_budget:
                raise ArithmeticError(f'independent first-law balance failed: {energy_error_abs} > {energy_budget}')
            record['first_law_absolute_error'] = energy_error_abs
            record['first_law_absolute_budget'] = energy_budget
        return {'network': name, 'rho': rho, 'interval': interval,
                'time_integrations': records,
                'reference_endpoint_hex': [float(value).hex() for value in authority],
                'independent_endpoint_energy_hex': energies[1][0].hex(),
                'independent_energy_change': energies[1][1],
                'relative_endpoint_eos_error': energy_error}
    finally:
        child.stdin.close()
        # A normal EOF finishes the server; a protocol/ODE failure still has a
        # bounded cleanup. Long-running jobs belong to the outer common guard.
        try:
            result = child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
            raise RuntimeError('burn RHS server failed to close')
        finally:
            child.stdout.close()
        if result != 0:
            raise RuntimeError(f'burn RHS server failed: {result}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--derive-reference', action='store_true',
        help='print independently corroborated endpoints for review; never rewrite fixtures or declare a validation pass')
    args = parser.parse_args()
    binary, build = args.binary.resolve(), args.build_dir.resolve()
    def identity():
        return provenance.capture_focused(artifacts={'burn_reference': binary},
            source_root=ROOT, build_dir=build)
    before = identity()
    values = np.fromfile(TABLE, sep=' ')
    points = IMAX * JMAX
    if values.size != 21 * points:
        raise ValueError('incomplete Helmholtz reference table')
    free_energy = values[:9 * points].reshape(JMAX, IMAX, 9)
    eta = values[13 * points:17 * points].reshape(JMAX, IMAX, 4)
    records = [review(binary, name, free_energy, eta, derive_reference=args.derive_reference)
               for name in ('aprox13', 'aprox19', 'aprox21', 'iso7')]
    provenance.require_unchanged(before, identity())
    print(json.dumps({'scope': 'builtin independent time integration and endpoint EOS',
        'release_qualified': False, 'focused_gate_pass': not args.derive_reference,
        'reference_derivation': args.derive_reference,
        'independent_nuclear_rates': False, 'identity': before,
        'identity_verified_after_run': True, 'table': provenance.file_identity(TABLE),
        'records': records}, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
