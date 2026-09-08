"""Observe runtime 3D refine/coarsen with the canonical Cartesian Sedov case.

The fixed checkpoints are 1, 5, 10, 20, 40 and 80 accepted steps. Only the
case name, observation steps and topology-coverage requirements are derived;
the original physical input, overrides, thresholds, capacity and numerical
budgets remain unchanged. This is an observed trajectory, not a promised pass.

Release-872 reaches t=0.0010510504773499122 and 0.002063996860800448 at
steps 1 and 2, with 34 leaves and no runtime topology change. Its [0,1]^3
domain has a central radius-0.1 deposit and max_blocks=128. That pool is not
a guarantee for full refinement (27 roots would produce 216 leaves, with
additional staging storage). Neither capacity nor boundary safety at step 80
is inferred from the first two steps; the unchanged runner and budgets apply.

The initially proposed 3D SmoothAdvection embedding is not used: its Setup
explicitly requires one-dimensional Cartesian geometry. No problem guard is
changed. Existing runtime, topology, provenance and sanitizer helpers own the
execution and checks; this result recipe introduces no transfer mathematics.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
MANIFEST = ROOT / 'validation/amr/gpu_cases.json'
OBSERVATION_STEPS = (1, 5, 10, 20, 40, 80)
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance
from validation_sanitizer import CudaSanitizer


def derive_case():
    original, = runtime.select_cases(runtime.load_manifest(MANIFEST), ['hydro_amr_rk3_3d'])
    case = deepcopy(original)
    case['id'] = 'hydro_amr_regrid_cycle_3d'
    case['accepted_steps'] = list(OBSERVATION_STEPS)
    case['topology_policy'].update(require_count_change=True,
        require_refine_transition=True, require_derefine_transition=True)
    parameters = runtime.read_parameter_map(ROOT / case['input'])
    parameters.update(case['overrides'])
    if parameters['geometry'] != 'cartesian' or any(
            int(parameters['nblockx' + str(axis)]) <= 0 for axis in (1, 2, 3)):
        raise RuntimeError('the canonical Sedov input is not Cartesian 3D')
    return original, case, parameters


def check_coverage(record, case, parameters, sanitizer):
    checkpoints = record['checkpoints']
    if record['id'] != case['id'] or any(
            [entry[backend]['steps'] for entry in checkpoints] != case['accepted_steps']
            for backend in ('cpu', 'cuda')):
        raise RuntimeError('dynamic 3D checkpoint coverage differs from the fixed recipe')
    observed = [entry['parity'] for entry in checkpoints]
    if any(item['dimension'] != 3 or item['passed'] is not True for item in observed):
        raise RuntimeError('dynamic 3D needs successful three-dimensional checkpoint comparisons')
    transitions = runtime.validate_topology_policy(observed, case['topology_policy'], case['id'])
    if transitions != record['topology_transitions']:
        raise RuntimeError('runtime topology transition evidence differs from the shared checker')
    # Only step-1 and later snapshots participate: initialization cannot satisfy
    # this check. Full parent/child leaf keys remain in each parity.topology.
    pairs = [dict(from_step=left['step'], to_step=right['step'], dimension=left['dimension'],
        children_per_parent=1 << left['dimension'],
        transitions=runtime.validate_topology_transitions([left, right],
            require_refine=False, require_derefine=False))
        for left, right in zip(observed, observed[1:])]
    measured = []
    capacity = int(parameters['max_blocks'])
    for entry in checkpoints:
        for backend in ('cpu', 'cuda'):
            lane = entry[backend]
            metrics = lane['regrid']
            if runtime.read_regrid_metrics(Path(metrics['file']['path']), backend, lane['steps']) != metrics:
                raise RuntimeError('whole-regrid rows, summary or file identity changed')
            initial, *running = metrics['records']
            if initial['macro_step'] != 0 or initial['physical_time'] != 0.0 or \
                    [row['macro_step'] for row in running] != list(range(lane['steps'])):
                raise RuntimeError('missing initialization or runtime regrid measurement')
            if any(max(row['old_blocks'], row['new_blocks']) > capacity for row in metrics['records']):
                raise RuntimeError('observed regrid exceeds the unchanged configured block capacity')
            expected_sanitizer = sanitizer.evidence(Path(lane['parameter_file']).parent) \
                if sanitizer is not None and backend == 'cuda' else None
            if lane['sanitizer'] != expected_sanitizer:
                raise RuntimeError('instrumentation coverage or report identity differs')
            measured.append(dict(backend=backend, steps=lane['steps'],
                initial_records=1, runtime_records=len(running),
                runtime_topology_changes=sum(row['topology_changed'] for row in running),
                max_observed_blocks=max(max(row['old_blocks'], row['new_blocks'])
                    for row in metrics['records']), summary=metrics['summary']))
    return dict(dimension=3, runtime_topology_transitions=transitions,
        transition_checker='tools/validate_backend_results.py::validate_topology_transitions',
        adjacent_snapshot_checks=pairs, regrid_coverage=measured,
        configured_max_blocks=capacity,
        instrumented_cuda_lanes=len(checkpoints) if sanitizer is not None else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--sanitizer', type=Path)
    parser.add_argument('--tool', choices=('memcheck', 'racecheck'))
    args = parser.parse_args()
    if args.tool and not args.sanitizer:
        parser.error('--tool requires --sanitizer; instrumentation cannot be silently skipped')
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    original, case, parameters = derive_case()
    identity_args = dict(arch=build / 'bin/ARCH',
        checkpoint_validator=build / 'arch_cuda_single_level_validation',
        source_root=ROOT, build_dir=build)
    before = provenance.capture(**identity_args)
    manifest_file = provenance.file_identity(MANIFEST)
    recipe = provenance.file_identity(Path(__file__).resolve())
    inputs = runtime.runtime_case_inputs([case], ROOT)
    sanitizer = CudaSanitizer(args.sanitizer, args.tool or 'memcheck') if args.sanitizer else None
    started = datetime.now(timezone.utc).isoformat()
    attempt = dict(schema=1, scope='Cartesian 3D application runtime refine/coarsen lifecycle',
        release_qualified=False, status='running', recipe=recipe, manifest=manifest_file,
        canonical_case=original, derived_case=case, runtime_inputs=inputs,
        command=[sys.executable, '-B', str(Path(__file__).resolve()), *sys.argv[1:]],
        instrumentation=dict(tool=sanitizer.tool, executable=sanitizer.identity) if sanitizer else None,
        provenance=before, binary_sha256=before['artifacts']['arch_sha256'],
        started_utc=started, note='Fixed observation window; original Sedov input, capacity, thresholds and numerical budgets. Initialization alone cannot satisfy runtime bidirectional topology coverage.')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'attempt.json').write_text(json.dumps(attempt, indent=2, default=str) + '\n')
    record = None
    try:
        record = runtime.run_case(identity_args['arch'], identity_args['checkpoint_validator'],
            ROOT, case, output / 'runs', sanitizer)
        coverage = check_coverage(record, case, parameters, sanitizer)
        if manifest_file != provenance.file_identity(MANIFEST) or \
                recipe != provenance.file_identity(Path(__file__).resolve()) or \
                inputs != runtime.runtime_case_inputs([case], ROOT):
            raise RuntimeError('dynamic 3D recipe, manifest or runtime inputs changed')
        evidence = dict(attempt, status='pass', focused_gate_pass=True,
            cases=[record], coverage=coverage, finished_utc=datetime.now(timezone.utc).isoformat())
        provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    except Exception as error:
        failure = dict(attempt, status='failed', focused_gate_pass=False,
            identity_verified_after_run=False, error=f'{type(error).__name__}: {error}',
            cases=[record] if record is not None else [],
            finished_utc=datetime.now(timezone.utc).isoformat())
        (output / 'failure.json').write_text(json.dumps(failure, indent=2, default=str) + '\n')
        raise
    print('dynamic 3D application lifecycle PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
