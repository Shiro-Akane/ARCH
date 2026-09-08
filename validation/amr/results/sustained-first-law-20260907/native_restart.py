"""Qualify sustained AMR and restart with native checkpoint composition.

Every restored native state and same-backend long continuation retain the strict
restart policy. Independent and repeatedly switched burn histories additionally
end at one prescribed physical time. Forward step/controller comparisons are
recorded as diagnostics, not physical invariants or replacements for fixed-time
acceptance. Failed earlier step-aligned experiments are preserved separately.
"""
import argparse
import json
from copy import deepcopy
from datetime import datetime, timezone
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance
import replay as earlier


def burn_chains(arch, validator, output, cycles, terminal_time, native_probe):
    canonical = ROOT / 'validation/amr/inputs/burn_enuc_amr.par'
    total = 2 * (cycles + 1)

    def records(lane):
        parameter = Path(lane['parameter'])
        return [(path, runtime.checkpoint_metadata(validator=validator,
            checkpoint=path, parameters=parameter, expected_steps=None))
            for path in parameter.parent.glob('*_chk_*.h5')]

    def run(name, backend, steps, source=None, until=None):
        restore = {} if source is None else dict(restart_file=source[0],
            restart_parameters=source[1], restart_step=source[2], restart_phase=True)
        return restart.run_lane(arch=arch, source_root=ROOT, canonical_input=canonical,
            output_root=output, name=name, problem='BurnGradient', backend=backend,
            max_steps=steps, checkpoint_validator=validator, terminal_time=until, **restore)

    def compare(left, right, *, physical=False):
        result = runtime.compare_hdf5_checkpoints(Path(left), Path(right),
            restart.comparison_policy(), validator,
            comparison_mode='physical-time' if physical else 'reproducibility',
            target_time=terminal_time if physical else None)
        if not result['passed'] or not result.get('native_composition_compared'):
            raise RuntimeError('native restart comparison failed: ' + str(result))
        if not physical and result['output_index_offsets'] != restart.output_index_offsets(None):
            raise RuntimeError('restart changed output history')
        return result

    continuous = {backend: run('continuous_' + backend, backend, total)
                  for backend in ('cpu', 'cuda')}
    fixed = {backend: run('fixed_' + backend, backend, -1, until=terminal_time)
             for backend in ('cpu', 'cuda')}
    fixed_comparisons = [compare(fixed['cpu']['checkpoint'], fixed['cuda']['checkpoint'], physical=True)]
    chains = []
    for pattern in ('cpu', 'cuda', 'alternating'):
        source = run(pattern + '_segment_0', 'cuda' if pattern == 'cuda' else 'cpu', 3)
        segments, comparisons, local_controls, selected, native_restores = [source], [], [], [], []
        for index in range(1, cycles + 1):
            step, final = 2 * index, index == cycles
            checkpoint, metadata = restart.select_checkpoint(records(source), step, True)
            restored = (checkpoint, Path(source['parameter']), step)
            selected.append(dict(checkpoint=str(checkpoint), **metadata))
            # Check the actual shared read_chk path and device upload/download
            # BEFORE evolution. The probe contains no integration algorithm.
            lane = output / (pattern + '_native_' + str(index))
            lane.mkdir()
            process = runtime.run_arch_with_logs([str(native_probe), str(checkpoint),
                str(restored[1]), str(lane)], source_root=ROOT, lane_root=lane, timeout=120)
            if process.returncode:
                raise RuntimeError('native checkpoint replay failed')
            restore_checks = []
            for destination in ('host', 'cuda'):
                files = list(lane.glob(destination + '_chk_*.h5'))
                if len(files) != 1:
                    raise RuntimeError('missing native restored snapshot')
                result = compare(checkpoint, files[0])
                if result['max_abs'] != 0.0 or any(result[key] != result['candidate_' + key]
                        for key in ('step', 'time', 'dt_old', 'dt_burn')):
                    raise RuntimeError('native restore changed state or controller')
                restore_checks.append(result)
            native_restores.append(dict(source=str(checkpoint), comparisons=restore_checks,
                stdout=provenance.file_identity(lane / 'arch.stdout')))
            backend = ('cuda' if index % 2 else 'cpu') if pattern == 'alternating' else pattern
            stop = total if final else step + 3
            if pattern == 'alternating':
                control = run(pattern + '_control_' + str(index), source['backend'], stop, restored)
                local_controls.append(control)
            source = run(pattern + '_segment_' + str(index), backend, stop, restored)
            segments.append(source)
            observed, _ = restart.select_checkpoint(records(source), step + 2, not final)
            if pattern == 'alternating':
                expected, _ = restart.select_checkpoint(records(control), step + 2, not final)
            else:
                expected, _ = restart.select_checkpoint(records(continuous[pattern]), step + 2, not final)
            if pattern == 'alternating':
                result = runtime.compare_hdf5_checkpoints(expected, observed,
                    restart.comparison_policy(), validator, comparison_mode='step-diagnostic')
                if not result['passed']:
                    raise RuntimeError('forward diagnostic exceeded its original field budget')
                result['acceptance_role'] = 'forward-step diagnostic; fixed-time physics is mandatory'
                comparisons.append(result)
            else:
                comparisons.append(compare(expected, observed))
            if pattern != 'alternating' and comparisons[-1]['max_abs'] != 0.0:
                raise RuntimeError('same-backend native restore changed an evolved field')
        terminal_lanes = []
        # Resume the last genuine post-regrid source, not a rewritten endpoint.
        for backend in ('cpu', 'cuda'):
            lane = run(pattern + '_fixed_' + backend, backend, -1, restored, terminal_time)
            terminal_lanes.append(lane)
            fixed_comparisons.append(compare(fixed[backend]['checkpoint'], lane['checkpoint'], physical=True))
        initial, = [path for path, metadata in records(continuous['cpu']) if metadata['step'] == 0]
        runtime.validate_topology_policy([compare(initial, initial), *comparisons],
            dict(require_refined=True, require_mixed=True, require_count_change=True), pattern)
        chains.append(dict(pattern=pattern, cycles=cycles, segments=segments,
            selected_checkpoints=selected, forward_comparisons=comparisons,
            exact_native_restores=native_restores,
            local_controls=local_controls, fixed_time_continuations=terminal_lanes))
        print('native burn chain PASS: ' + pattern, flush=True)
    return dict(continuous=continuous, fixed_time_references=fixed,
        fixed_time_comparisons=fixed_comparisons, chains=chains, terminal_time=terminal_time)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--restart-cycles', type=int, default=12)
    parser.add_argument('--burn-terminal-time', type=float, default=1e-10)
    parser.add_argument('--native-state-probe', type=Path, required=True)
    args = parser.parse_args()
    if args.restart_cycles < 4:
        parser.error('at least four chained restores are required')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    arch, validator = build / 'bin/ARCH', build / 'arch_cuda_single_level_validation'
    identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
    before = provenance.capture(**identity_args)
    probe = provenance.file_identity(args.native_state_probe.resolve())
    build_record_path = args.native_state_probe.resolve().with_name('build.json')
    build_record = json.loads(build_record_path.read_text())
    if build_record['binary'] != probe \
            or build_record['identity']['source'] != before['source'] \
            or build_record['identity']['artifacts']['arch'] != provenance.file_identity(arch) \
            or build_record['identity']['artifacts']['validator'] != provenance.file_identity(validator):
        raise RuntimeError('native-state probe does not bind the current candidate')
    def recipes():
        return [provenance.file_identity(Path(path).resolve()) for path in (__file__, earlier.__file__)]
    recipe = recipes()
    manifest = ROOT / 'validation/amr/gpu_cases.json'
    inventory = {case['id']: case for case in runtime.load_manifest(manifest)['cases']}
    cases = []
    for name, counts in (('hydro_amr_regrid_cycle_1d', (100, 500)),
                         ('diffusion_amr_rkl2_5stage', (25, 100))):
        case = deepcopy(inventory[name])
        case['accepted_steps'] = sorted(set(case['accepted_steps']) | set(counts))
        cases.append(case)
    input_identity = runtime.runtime_case_inputs(cases, ROOT)
    def restart_inputs():
        return {name: provenance.runtime_inputs(parameter_file=ROOT / path,
            working_directory=ROOT, parameter_reader=runtime.read_parameter_map)
            for name, path in (('smooth', 'validation/amr/inputs/smooth_amr80_l1.par'),
                               ('burn', 'validation/amr/inputs/burn_enuc_amr.par'))}
    restart_identity = restart_inputs()
    started, matrices = datetime.now(timezone.utc).isoformat(), []
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output / 'matrices')
        if record['checkpoints'][-1]['cuda']['regrid']['summary']['records'] < max(case['accepted_steps']):
            raise RuntimeError('sustained regrid count is incomplete')
        matrices.append(record)
        print('sustained matrix PASS: ' + case['id'], flush=True)
    smooth = earlier.restart_chain(arch, validator, output / 'smooth', 'SmoothAdvection',
        ROOT / 'validation/amr/inputs/smooth_amr80_l1.par', args.restart_cycles)
    burn = burn_chains(arch, validator, output / 'burn', args.restart_cycles,
        args.burn_terminal_time, args.native_state_probe.resolve())
    if recipes() != recipe or input_identity != runtime.runtime_case_inputs(cases, ROOT) \
            or restart_identity != restart_inputs():
        raise RuntimeError('sustained recipe or input changed')
    if probe != provenance.file_identity(args.native_state_probe.resolve()):
        raise RuntimeError('native-state probe changed')
    evidence = dict(schema=1, scope='native-state sustained AMR and strict restore with fixed-time physics',
        focused_gate_pass=True, release_qualified=False, recipe=recipe,
        native_state_probe=probe, probe_build=provenance.file_identity(build_record_path),
        derived_cases=cases, runtime_inputs=input_identity, restart_inputs=restart_identity, matrices=matrices,
        smooth_chain=smooth, burn_chains=burn, started_utc=started,
        finished_utc=datetime.now(timezone.utc).isoformat())
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('native sustained restart PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
