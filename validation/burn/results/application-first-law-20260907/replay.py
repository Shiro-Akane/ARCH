"""Replay the original aprox13/Helm cross-solver inputs on both backends.

The shared runtime validator owns execution, metadata, plans and CPU/CUDA
comparison. This recipe samples the uniform scientific state and applies the
original species/energy budgets against the strict BE_NR application result.
Independent time integration is a separate, existing burn-reference gate.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance


def scientific_state(lane):
    values = runtime.read_parameter_map(Path(lane['parameter_file']))
    with h5py.File(lane['checkpoint']) as data:
        rho = data['Data/rho'][:]
        fractions = data['Data/rhoX'][:] / rho
        energy = data['Data/eng'][:]
        if not np.all(rho == float(values['rho0'])) or not np.all(np.isfinite(fractions)) \
                or not np.all(np.isfinite(energy)) or not np.all(energy > 0):
            raise ArithmeticError('invalid fixed-density burn state')
        if np.max(np.abs(np.sum(fractions, axis=0) - 1)) > 1e-12:
            raise ArithmeticError('burn composition closure failed')
        # Metadata/time/topology have already been checked by the shared runner.
        # Inspect all cells, not only a representative cell from this uniform case.
        return dict(fractions=fractions, energy=energy)


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
    before, recipe = provenance.capture(**identity_args), provenance.file_identity(Path(__file__).resolve())
    cases = []
    for method, filename in (('be_nr', 'be_nr_reference.par'), ('bd', 'bd.par'), ('ros4', 'ros4.par')):
        parameter = ROOT / 'validation/burn/inputs' / filename
        values = runtime.read_parameter_map(parameter)
        cases.append(dict(id=method, problem='BurnOneZone', input=str(parameter.relative_to(ROOT)),
            accepted_steps=[], scientific_time=float(values['tmax']), timeout_seconds=1200,
            plan_policy=dict(network='aprox13', eos='helmholtz', ode=method, linear='denselu'),
            reduction_policy=dict(rtol=2e-8, atol=1e-12),
            qualification=dict(reference='cpu_and_network_conservation',
                               positive=['rho', 'eng'], species_sum_atol=1e-12)))
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    started, records, states = datetime.now(timezone.utc).isoformat(), [], {}
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output)
        records.append(record)
        for backend in ('cpu', 'cuda'):
            states[case['id'], backend] = scientific_state(record['scientific'][backend])
        print('burn application PASS: ' + case['id'], flush=True)
    reference, comparisons = states['be_nr', 'cpu'], []
    for (method, backend), state in states.items():
        if any(state[key].shape != reference[key].shape for key in reference):
            raise ArithmeticError('cross-solver state extents differ')
        species = runtime.compare_numeric_fields(
            {'fractions': reference['fractions'].ravel()}, {'fractions': state['fractions'].ravel()},
            dict(rtol=0., atol=1e-8))
        energy = runtime.compare_numeric_fields(
            {'energy': reference['energy'].ravel()}, {'energy': state['energy'].ravel()},
            dict(rtol=1e-8, atol=0.))
        difference = state['fractions'] - reference['fractions']
        if not species['passed'] or not energy['passed']:
            raise ArithmeticError(f'original cross-solver budget failed: {method}/{backend}')
        comparisons.append(dict(method=method, backend=backend, species=species, energy=energy,
            species_l1=float(np.mean(np.abs(difference))), species_l2=float(np.sqrt(np.mean(difference**2))),
            abundance_sum_residual=float(np.max(np.abs(np.sum(state['fractions'], axis=0) - 1)))))
    if inputs != runtime.runtime_case_inputs(cases, ROOT) \
            or recipe != provenance.file_identity(Path(__file__).resolve()):
        raise RuntimeError('cross-solver inputs or recipe changed')
    evidence = dict(schema=1, scope='actual-ARCH-aprox13-Helm-cross-solver-verification',
        focused_gate_pass=True, release_qualified=False, recipe=recipe, derived_cases=cases,
        runtime_inputs=inputs, cases=records, cross_solver=comparisons,
        species_absolute_budget=1e-8, energy_relative_budget=1e-8, abundance_sum_budget=1e-12,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('burn application cross-solver PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
