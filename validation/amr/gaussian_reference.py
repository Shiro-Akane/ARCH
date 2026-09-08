"""Check registered Gaussian initialization and actual thermal transport.

The initial-state oracle uses native-coordinate formulas independently of
Grid::GetPhysicalCoords. The activity control runs one canonical coupled case
with/without thermal diffusion at the same physical time and topology. This
is focused evidence; it does not replace the complete AMR runtime matrix.
Requires h5py/numpy and an already built ARCH. Run under run_memory_guarded.py.
"""
from datetime import datetime, timezone
from pathlib import Path
import argparse
import json
import math
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runner
import validation_provenance as provenance


def check_activity(relative_difference):
    # The operator must change the state by more than rounding, not merely
    # advertise an enabled flag. This is not a backend parity tolerance.
    threshold = 256 * sys.float_info.epsilon
    if not math.isfinite(relative_difference) or relative_difference <= threshold:
        raise ArithmeticError('thermal diffusion did not produce a resolved energy change')
    return threshold


def initialization_errors(parameter, checkpoint):
    import h5py
    import numpy as np
    values = runner.read_parameter_map(parameter)
    dim = 3 if int(values['nblockx3']) else (2 if int(values['nblockx2']) else 1)
    with h5py.File(checkpoint) as data:
        if int(data.attrs['step']) != 0 or not np.all(data['Blocks/level'][:] == 0):
            raise ValueError('initial oracle requires an unrefined step-zero checkpoint')
        cells = int(data.attrs['cells_per_block'])
        n = round(cells**(1/dim))
        if n**dim != cells:
            raise ValueError('unexpected checkpoint cell layout')
        local = np.indices((n,)*dim).reshape(dim, -1)[::-1]
        coordinates = []
        for axis in range(dim):
            lower = float(values[f'x{axis+1}_min'])
            step = (float(values[f'x{axis+1}_max'])-lower)/(int(values[f'nblockx{axis+1}'])*n)
            logical = data[f'Blocks/logical_x{axis+1}'][:]
            coordinates.append(lower + (logical[:, None]*n + local[axis] + .5)*step)
        r = coordinates[0]
        xc, yc, zc = (float(values.get(key, '0')) for key in ('xc', 'yc', 'zc'))
        if dim == 1:
            distance2 = (r-xc)**2 + yc**2 + zc**2
        elif dim == 2:
            angle = coordinates[1]
            distance2 = (r*np.cos(angle)-xc)**2 + (r*np.sin(angle)-yc)**2 + zc**2
        elif values['geometry'] == 'cylindrical':
            z, phi = coordinates[1:]
            distance2 = (r*np.cos(phi)-xc)**2 + (r*np.sin(phi)-yc)**2 + (z-zc)**2
        else:
            theta, phi = coordinates[1:]
            distance2 = ((r*np.sin(theta)*np.cos(phi)-xc)**2
                         + (r*np.sin(theta)*np.sin(phi)-yc)**2 + (r*np.cos(theta)-zc)**2)
        pulse = np.exp(-distance2/float(values['width'])**2)
        rho = data['Data/rho'][:]
        fractions = data['Data/rhoX'][:]/rho
        errors = {'species': float(np.max(np.abs(fractions[1]-float(values['amp'])*pulse)))}
        kinetic = np.zeros_like(rho)
        for name in ('u', 'v', 'w'):
            momentum = data['Data/mom_'+name][:]
            expected = float(values.get(name+'_amplitude', '0'))*pulse
            errors[name] = float(np.max(np.abs(momentum/rho-expected)))
            kinetic += .5*momentum**2/rho
        cv = data['Species/Cv'][:][:, None, None]
        if not np.all(cv > 0):
            raise ValueError('passive Gaussian needs positive heat capacities')
        capacity = np.sum(fractions*cv, axis=0)
        gamma_minus_one = np.sum(fractions*cv*(data['Species/gamma'][:][:, None, None]-1), axis=0)/capacity
        pressure = (data['Data/eng'][:]-kinetic)*gamma_minus_one
        temperature = (data['Data/eng'][:]-kinetic)/rho/capacity
        if not np.all(np.isfinite(temperature)) or np.ptp(temperature) <= 0:
            raise ValueError('thermal pulse must have a real temperature gradient')
        errors['pressure'] = float(np.max(np.abs(pressure-float(values['p0'])*(
            1+float(values['pressure_amplitude'])*pulse))))
        if any(not math.isfinite(error) or error > 2e-12 for error in errors.values()):
            raise ArithmeticError(f'Gaussian initialization mismatch: {errors}')
    return errors


def thermal_difference(enabled, disabled):
    import h5py
    import numpy as np
    with h5py.File(enabled) as on, h5py.File(disabled) as off:
        if on.attrs['time'] != off.attrs['time'] or on.attrs['step'] != off.attrs['step']:
            raise ValueError('thermal activity controls must reach identical time and step')
        for name in ('level', 'logical_x1', 'logical_x2', 'logical_x3'):
            if not np.array_equal(on['Blocks/'+name][:], off['Blocks/'+name][:]):
                raise ValueError('thermal activity controls need identical ordered topology')
        a, b = on['Data/eng'][:], off['Data/eng'][:]
        if not np.all(np.isfinite(a)) or not np.all(np.isfinite(b)):
            raise ValueError('nonfinite thermal activity energy')
        absolute = float(np.max(np.abs(a-b)))
        scale = max(float(np.max(np.abs(a))), float(np.max(np.abs(b))))
        relative = absolute/scale if scale > 0 else 0.0
        threshold = check_activity(relative)
        return dict(absolute_energy_change=absolute, relative_energy_change=relative,
                    minimum_relative_change=threshold, time=float(on.attrs['time']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    arch = build/'bin/ARCH'
    runner.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    identity_arguments = dict(arch=arch, checkpoint_validator=build/'arch_cuda_single_level_validation',
                              source_root=ROOT, build_dir=build)
    before, started = provenance.capture(**identity_arguments), datetime.now(timezone.utc).isoformat()
    manifest = runner.load_manifest(ROOT/'validation/amr/gpu_curvilinear_cases.json')
    cases, commands = [], []
    for case in manifest['cases']:
        if '_coupled_' not in case['id'] or '_rkl1_' not in case['id']:
            continue
        dim = 3 if int(case['overrides']['nblockx3']) else (2 if int(case['overrides']['nblockx2']) else 1)
        overrides = {**case['overrides'], 'lrefinemax': '0', 'max_blocks': '8',
            'nblockx1': '2', 'nblockx2': '1' if dim >= 2 else '0',
            'nblockx3': '1' if dim == 3 else '0', 'x1_max': '2.0'}
        lane = output/case['id']
        parameter = lane/'input.par'
        runner.render_parameter_file(ROOT/case['input'], parameter, backend='cpu', output_dir=lane,
            base_name='initial', accepted_steps=1, scientific_overrides=overrides)
        command = [str(arch), case['problem'], str(parameter)]
        completed = runner.run_arch_with_logs(command, source_root=ROOT, lane_root=lane, timeout=120)
        if completed.returncode != 0:
            raise RuntimeError(f"{case['id']} failed; see {lane}")
        checkpoint = lane/'initial_chk_0000.h5'
        cases.append(dict(case=case['id'], errors=initialization_errors(parameter, checkpoint),
                          checkpoint=provenance.file_identity(checkpoint), parameter=provenance.file_identity(parameter)))
        commands.append(command)
    if len(cases) != 6:
        raise ValueError('initialization must cover both curved geometries in all three dimensions')
    activity_case = next(c for c in manifest['cases'] if c['id'] == 'cylindrical_coupled_amr_rkl2_wedge_2d')
    activity = {}
    for backend in ('cpu', 'cuda'):
        lanes = []
        for enabled in ('true', 'false'):
            case = {**activity_case, 'id': 'thermal_'+enabled,
                    'overrides': {**activity_case['overrides'], 'use_thermal_diff': enabled}}
            lanes.append(runner.run_arch_lane(arch, ROOT, case, backend, 2, output/'thermal_activity'))
            commands.append([str(arch), case['problem'], str(lanes[-1]['parameter_file'])])
        activity[backend] = dict(metrics=thermal_difference(lanes[0]['checkpoint'], lanes[1]['checkpoint']), lanes=lanes)
    evidence = dict(schema=1, scope='gaussian-initialization-and-thermal-activity',
        focused_gate_pass=True, release_qualified=False,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        commands=commands, initialization=cases, thermal_activity=activity)
    provenance.write_evidence(output/'evidence.json', evidence, before, **identity_arguments)
    print(json.dumps(dict(focused_gate_pass=True, release_qualified=False, evidence=str(output/'evidence.json'))))


if __name__ == '__main__':
    main()
