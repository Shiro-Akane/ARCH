"""Production built-in NSE activation and source-aware energy closure.

All built-in networks and ODEs use the ordinary ARCH application. Each network
also has an NSE-disabled control at the same physical time. Nuclear data and
conversion constants come from the existing independent NSE data reader;
the energy check is an extended-precision binding-energy balance, not a call
to ARCH's integrator or NSE solver. This complements the independent equilibrium
and self-consistent projection tests in the configured regression suite.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'validation/network'))
import validate_backend_results as runtime
import validation_provenance as provenance
import nse_reference


def state(path):
    with h5py.File(path) as data:
        rho = data['Data/rho'][:]
        if not np.all(rho == 1e7):
            raise ArithmeticError('NSE one-zone density changed')
        velocity_squared = sum((data['Data/' + name][:] / rho)**2 for name in ('mom_u', 'mom_v', 'mom_w'))
        if not np.all(velocity_squared == 0.):
            raise ArithmeticError('NSE one-zone acquired kinetic energy')
        composition = data['Data/X'][:].astype(np.longdouble)
        energy = data['Data/eng'][:].astype(np.longdouble) / rho.astype(np.longdouble)
        if not np.all(np.isfinite(composition)) or not np.all(np.isfinite(energy)):
            raise ArithmeticError('NSE one-zone contains nonfinite state')
        return composition, energy


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
    data = {name: nse_reference.nuclear_data(name) for name in nse_reference.NETWORKS}
    cases = []
    for network in data:
        for method, active in (('be_nr', False), ('be_nr', True), ('bd', True), ('ros4', True)):
            cases.append(dict(id=f'{network}_{method}_nse_{active}', problem='BurnOneZone',
                input='validation/burn/inputs/bd.par', accepted_steps=[], scientific_time=1e-16,
                timeout_seconds=1200, overrides=dict(network_name=network, ode_solver=method,
                    use_nse=str(active).lower(), temperature0='1e10', nseTempThreshold='5e9',
                    nseDensThreshold='1e6', plt_variables='DENS,TEMP,PRES,ENER,ENUC,SPECIES'),
                plan_policy=dict(network=network, eos='helmholtz', ode=method, linear='denselu'),
                reduction_policy=dict(rtol=2e-8, atol=1e-12),
                qualification=dict(reference='cpu_and_network_conservation',
                                   positive=['rho', 'eng'], species_sum_atol=1e-12)))
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    started, records, controls = datetime.now(timezone.utc).isoformat(), [], {}
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output / 'runs')
        network, active = case['overrides']['network_name'], case['overrides']['use_nse'] == 'true'
        nuclear = data[network]
        binding = (np.asarray(nuclear['arrays']['BION'], dtype=np.longdouble)
                   / np.asarray(nuclear['arrays']['AION'], dtype=np.longdouble))
        charge = (np.asarray(nuclear['arrays']['ZION'], dtype=np.longdouble)
                  / np.asarray(nuclear['arrays']['AION'], dtype=np.longdouble))
        balances = []
        for backend in ('cpu', 'cuda'):
            lane = record['scientific'][backend]
            parameter = Path(lane['parameter_file'])
            initial, = parameter.parent.glob('*_chk_0000.h5')
            x0, e0 = state(initial)
            x1, e1 = state(lane['checkpoint'])
            dx = x1 - x0
            q = np.sum(dx * binding[:, None, None], axis=0) * np.longdouble(nuclear['energy_conversion'])
            scale = np.maximum(np.maximum(np.abs(e0), np.abs(e1)), np.abs(q))
            balance = float(np.max(np.abs(e1 - e0 - q) / scale))
            ye = float(np.max(np.abs(np.sum(dx * charge[:, None, None], axis=0))))
            if not np.isfinite(balance) or not np.isfinite(ye) or balance > 1e-12 or ye > 1e-12:
                raise ArithmeticError('NSE application energy/charge closure failed')
            if not active:
                controls[network, backend] = x1
                contrast = None
            else:
                contrast = float(np.max(np.abs(x1 - controls[network, backend])))
                if not np.isfinite(contrast) or contrast <= 1e-3:
                    raise ArithmeticError('NSE-enabled run did not differ from its short-time disabled control')
            balances.append(dict(backend=backend, energy_closure_relative=balance,
                charge_absolute_drift=ye, nse_composition_contrast=contrast))
        record['source_aware_balance'] = balances
        records.append(record)
        print('NSE application PASS: ' + case['id'], flush=True)
    if inputs != runtime.runtime_case_inputs(cases, ROOT) \
            or recipe != provenance.file_identity(Path(__file__).resolve()):
        raise RuntimeError('NSE application input or recipe changed')
    evidence = dict(schema=1, scope='built-in NSE activation and coupled application energy closure',
        focused_gate_pass=True, release_qualified=False, recipe=recipe, nuclear_data=data,
        derived_cases=cases, runtime_inputs=inputs, cases=records,
        energy_relative_budget=1e-12, charge_absolute_budget=1e-12,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('NSE application profile PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
