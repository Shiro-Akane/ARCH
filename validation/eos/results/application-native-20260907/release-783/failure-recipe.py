"""Actual ARCH normalized-table hydro/AMR and burn application acceptance.

Manufactured tables use the analytic ideal-gas Helmholtz potential already
specified by tests/TabularEOSRegression.cpp. They are test data, not a second
production EOS. Both normalized table ranks and both storage models are covered.
The common runtime validators own execution, policy, checkpoint and parity checks.
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

R_GAS, GAMMA = 1e8, 5. / 3.
CV_GAS = R_GAS / (GAMMA - 1.)
EOS_RELATIVE_BUDGET = 1e-3  # Existing normalized ideal-gas table acceptance.


def table(path, rank, model, domain):
    lr = np.linspace(0., 2., 129) if domain == 'hydro' else np.linspace(6., 8., 129)
    lt = np.linspace(6., 8., 129) if domain == 'hydro' else np.linspace(7.5, 10.5, 193)
    shape = (len(lr), len(lt), 3) if rank == 3 else (len(lr), len(lt), 2, 2)
    rho, temperature = np.meshgrid(10.**lr, 10.**lt, indexing='ij')
    def expand(values):
        return np.broadcast_to(values.reshape(values.shape + (1,) * (rank - 2)), shape)
    with h5py.File(path, 'x') as data:
        for name, value in dict(arch_eos_version=1, table_rank=rank,
                thermodynamic_model=model, n_rho=len(lr), n_T=len(lt),
                log_rho_min=lr[0], log_rho_max=lr[-1], log_T_min=lt[0], log_T_max=lt[-1]).items():
            data[name] = value
        composition = (dict(n_X=3, X_min=.4, X_max=.6, composition_axis='Ye')
            if rank == 3 else dict(n_A=2, A_min=1., A_max=64., n_Z=2, Z_min=.5, Z_max=32.))
        for name, value in composition.items():
            data[name] = value
        if model == 'free_energy':
            data['free_energy'] = expand(R_GAS * temperature * np.log(rho)
                                         - CV_GAS * temperature * np.log(temperature))
        else:
            for name, values in dict(pressure=R_GAS * rho * temperature,
                    energy=CV_GAS * temperature, sound_speed=np.sqrt(GAMMA * R_GAS * temperature),
                    cv=np.full_like(rho, CV_GAS), dp_drho=R_GAS * temperature,
                    dp_dT=R_GAS * rho).items():
                data[name] = expand(values)
    return dict(rank=rank, model=model, domain=domain, shape=shape,
                table=provenance.file_identity(path), gas_constant=R_GAS, gamma=GAMMA)


def inspect_plot(lane, case):
    parameter = Path(lane['parameter_file'])
    plots = sorted(parameter.parent.glob('*_plt_*.h5'))
    if len(plots) != 2:
        raise RuntimeError('expected initial and final thermodynamic plots')
    with h5py.File(plots[-1]) as data:
        fields = data['Data']
        rho, temperature, pressure = (fields[name][:] for name in ('DENS', 'TEMP', 'PRES'))
        if not all(np.all(np.isfinite(values)) and np.all(values > 0)
                   for values in (rho, temperature, pressure)):
            raise ArithmeticError('invalid coupled thermodynamic state')
        domain = 'hydro' if case['problem'] == 'SmoothAdvection' else 'burn'
        limits = ((1., 100., 1e6, 1e8) if domain == 'hydro' else (1e6, 1e8, 10.**7.5, 10.**10.5))
        if not (np.all((rho > limits[0]) & (rho < limits[1]))
                and np.all((temperature > limits[2]) & (temperature < limits[3]))):
            raise ArithmeticError('coupled checkpoint leaves the manufactured table domain')
        pressure_error = float(np.max(np.abs(pressure / (rho * R_GAS * temperature) - 1.)))
        if pressure_error > EOS_RELATIVE_BUDGET:
            raise ArithmeticError('coupled pressure exceeds the original EOS budget')
    result = dict(plot=provenance.file_identity(plots[-1]), pressure_relative_error=pressure_error,
        density_range=[float(np.min(rho)), float(np.max(rho))],
        temperature_range=[float(np.min(temperature)), float(np.max(temperature))])
    if domain == 'hydro':
        with h5py.File(lane['checkpoint']) as data:
            levels, logical = data['Blocks/level'][:], data['Blocks/logical_x1'][:]
            cells = int(data.attrs['cells_per_block'])
            values = runtime.read_parameter_map(parameter)
            length = float(values['x1_max']) - float(values['x1_min'])
            dx = length / (int(values['nblockx1']) * cells * 2.**levels)
            position = float(values['x1_min']) + (logical[:, None] * cells
                + np.arange(cells)[None, :] + .5) * dx[:, None]
            wave = 2 * np.pi * int(values['mode']) / length
            exact = float(values['rho_mean']) + float(values['rho_amplitude']) \
                * np.sinc(wave * dx[:, None] / (2 * np.pi)) \
                * np.sin(wave * (position - float(values['x1_min'])
                    - float(values['velocity0']) * float(data.attrs['time'])))
            density_error = float(np.sum(np.abs(data['Data/rho'][:] - exact) * dx[:, None])
                                  / length / float(values['rho_mean']))
            if density_error > EOS_RELATIVE_BUDGET:
                raise ArithmeticError('manufactured entropy-wave trajectory error is too large')
            result['density_l1_relative_to_mean'] = density_error
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    tables_dir = output / 'tables'
    tables_dir.mkdir(parents=True)
    arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
    identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
    before, recipe = provenance.capture(**identity_args), provenance.file_identity(Path(__file__).resolve())
    definitions, cases = [], []
    for rank in (3, 4):
        for model in ('free_energy', 'direct'):
            for domain in ('hydro', 'burn'):
                path = tables_dir / f'{domain}-{rank}-{model}.h5'
                definitions.append(table(path, rank, model, domain))
                methods = ('be_nr', 'bd', 'ros4') if domain == 'burn' and model == 'free_energy' else (None,)
                for method in methods:
                    overrides = dict(eos_type='tabular', eos_table_path=str(path),
                        max_eint='1e22', plt_variables='DENS,TEMP,PRES,ENER,ENUC,SPECIES')
                    if domain == 'hydro':
                        overrides.update(rho_mean='10', rho_amplitude='2', pressure0='1e16',
                            velocity0='1e7', lrefinemax='1', regrid_interval='1')
                    elif method:
                        overrides['ode_solver'] = method
                    case = dict(id=f'{domain}_{rank}_{model}_{method or "default"}',
                        problem='SmoothAdvection' if domain == 'hydro' else 'BurnOneZone',
                        input='validation/amr/inputs/smooth_amr80_l1.par' if domain == 'hydro'
                              else 'validation/burn/inputs/bd.par',
                        overrides=overrides, accepted_steps=[1, 2, 5],
                        checkpoint_comparison='step-diagnostic',
                        scientific_time=1e-9 if domain == 'hydro' else 1e-10, timeout_seconds=1200,
                        plan_policy=dict(eos=f'tabular{rank}d'),
                        reduction_policy=dict(rtol=2e-8, atol=1e-12),
                        qualification=dict(reference='cpu_and_network_conservation',
                                           positive=['rho', 'eng'], species_sum_atol=1e-12))
                    if domain == 'hydro':
                        # The generic network qualifier is uniform-grid only.
                        # AMR uses the common physical-volume conservation check
                        # and the independent evolved-state oracle above.
                        case.pop('qualification')
                        case['topology_policy'] = dict(require_refined=True, require_mixed=True)
                        case['conservation_policy'] = dict(measure='physical_cell_volume', rtol=2e-12, atol=2e-11,
                            fields=['mass', 'mom_u', 'mom_v', 'mom_w', 'energy', 'rhoX'])
                    cases.append(case)
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    started, records = datetime.now(timezone.utc).isoformat(), []
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output / 'runs')
        record['manufactured_thermodynamics'] = {backend: inspect_plot(record['scientific'][backend], case)
                                               for backend in ('cpu', 'cuda')}
        records.append(record)
        print('tabular application PASS: ' + case['id'], flush=True)
    if recipe != provenance.file_identity(Path(__file__).resolve()) \
            or inputs != runtime.runtime_case_inputs(cases, ROOT):
        raise RuntimeError('application recipe or table/input changed')
    evidence = dict(schema=1, scope='normalized table coupled hydro/AMR and burn applications',
        focused_gate_pass=True, release_qualified=False, recipe=recipe, tables=definitions,
        derived_cases=cases, runtime_inputs=inputs, cases=records,
        thermodynamic_relative_budget=EOS_RELATIVE_BUDGET,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('normalized EOS application profile PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
