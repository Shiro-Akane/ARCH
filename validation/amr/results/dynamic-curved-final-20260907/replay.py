"""Observe bidirectional 2D curved AMR with two canonical Gaussian cases.

Only case IDs, accepted-step observations and topology requirements are derived.
The fixed observations are 2, 5, 20, 80 and 160 steps. All original input,
overrides, physical budgets, timeouts and RKL policy (including four stages)
remain unchanged. A legitimate adaptive stage-count change can therefore fail
the original stage witness; retain that failure rather than weakening a check.

This successor observes the bidirectional lifecycle through step 160, where
attempt 920 already showed a complete cylindrical parent/four-child round trip.
The archived 320-step attempt remains failed: its final coarsening temporarily
selected three RKL stages. Separate read-only checkpoint checks do not promote
that attempt or claim that the as-yet-unrun spherical route passes. See
attempt-920/README.md for the original recipe, diagnosis and retained evidence.

The original closed, reflecting domains contain 16 root blocks with Lmax=1:
at most 64 active leaves and 80 old/new staging blocks fit max_blocks=128.
Diffusion broadening motivates the observation window, not an analytic oracle
or a guaranteed pass. Runtime run_case, topology, regrid, provenance and
sanitizer helpers own execution and acceptance; this recipe adds no mathematics.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
MANIFEST = ROOT / 'validation/amr/gpu_curvilinear_cases.json'
OBSERVATION_STEPS = (2, 5, 20, 80, 160)
CANONICAL_CASES = {
    'cylindrical_amr_rkl1_wedge_2d': 'cylindrical',
    'spherical_amr_rkl1_wedge_2d': 'spherical',
}
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance
from validation_sanitizer import CudaSanitizer


def derive_cases():
    originals = runtime.select_cases(runtime.load_manifest(MANIFEST), list(CANONICAL_CASES))
    derived, parameters = [], {}
    for original in originals:
        case = deepcopy(original)
        geometry = CANONICAL_CASES[original['id']]
        case['id'] = geometry + '_amr_regrid_cycle_2d'
        case['accepted_steps'] = list(OBSERVATION_STEPS)
        case['topology_policy'].update(require_count_change=True,
            require_refine_transition=True, require_derefine_transition=True)
        effective = runtime.read_parameter_map(ROOT / case['input'])
        effective.update(case['overrides'])
        if effective['geometry'] != geometry or int(effective['nblockx1']) <= 0 \
                or int(effective['nblockx2']) <= 0 or int(effective['nblockx3']) != 0:
            raise RuntimeError('canonical curved case is not the requested two-dimensional geometry')
        derived.append(case)
        parameters[case['id']] = effective
    return originals, derived, parameters


def check_coverage(record, case, parameters, sanitizer):
    checkpoints = record['checkpoints']
    geometry = parameters['geometry']
    if record['id'] != case['id'] or geometry not in CANONICAL_CASES.values() or any(
            [entry[backend]['steps'] for entry in checkpoints] != case['accepted_steps']
            for backend in ('cpu', 'cuda')):
        raise RuntimeError('curved lifecycle observations differ from the fixed recipe')
    observed = [entry['parity'] for entry in checkpoints]
    if [item['step'] for item in observed] != case['accepted_steps'] or any(
            item['dimension'] != 2 or item['passed'] is not True for item in observed):
        raise RuntimeError('curved lifecycle needs successful two-dimensional checkpoint comparisons')
    transitions = runtime.validate_topology_policy(observed, case['topology_policy'], case['id'])
    if transitions != record['topology_transitions']:
        raise RuntimeError('recorded topology transitions differ from the shared checker')
    # Only step-2 and later snapshots participate. The shared checker requires
    # complete parent/four-child relations, not initialization or net counts.
    pairs = [dict(from_step=left['step'], to_step=right['step'], dimension=2,
        children_per_parent=1 << left['dimension'],
        transitions=runtime.validate_topology_transitions([left, right],
            require_refine=False, require_derefine=False))
        for left, right in zip(observed, observed[1:])]
    measured, runtime_changes = [], dict(cpu=0, cuda=0)
    capacity = int(parameters['max_blocks'])
    for entry in checkpoints:
        for backend in ('cpu', 'cuda'):
            lane = entry[backend]
            parameter = Path(lane['parameter_file'])
            actual = runtime.read_parameter_map(parameter)
            if provenance.sha256(parameter) != lane['parameter_sha256'] \
                    or actual['geometry'] != geometry or actual['compute_backend'] != backend \
                    or int(actual['nblockx2']) <= 0 or int(actual['nblockx3']) != 0:
                raise RuntimeError('actual runtime parameter identity or curved geometry changed')
            if any(entry[backend + '_conservation'][end]['geometry'] != geometry
                   for end in ('before', 'after')):
                raise RuntimeError('conservation measurements use a different physical geometry')
            metrics = lane['regrid']
            if runtime.read_regrid_metrics(Path(metrics['file']['path']), backend, lane['steps']) != metrics:
                raise RuntimeError('whole-regrid rows, summary or file identity changed')
            initial, *running = metrics['records']
            if initial['macro_step'] != 0 or initial['physical_time'] != 0.0 \
                    or [row['macro_step'] for row in running] != list(range(lane['steps'])):
                raise RuntimeError('missing initialization or runtime regrid measurement')
            if any(max(row['old_blocks'], row['new_blocks']) > capacity for row in metrics['records']):
                raise RuntimeError('observed regrid exceeds the original block capacity')
            expected_sanitizer = sanitizer.evidence(parameter.parent) \
                if sanitizer is not None and backend == 'cuda' else None
            if lane['sanitizer'] != expected_sanitizer:
                raise RuntimeError('instrumentation coverage or report identity differs')
            changes = sum(row['topology_changed'] for row in running)
            runtime_changes[backend] += changes
            measured.append(dict(backend=backend, steps=lane['steps'], file=metrics['file'],
                initial_records=1, runtime_records=len(running), runtime_topology_changes=changes,
                max_observed_blocks=max(max(row['old_blocks'], row['new_blocks'])
                    for row in metrics['records']), summary=metrics['summary']))
    if not all(runtime_changes.values()):
        raise RuntimeError('each backend must record non-initial runtime topology changes')
    return dict(id=case['id'], geometry=geometry, dimension=2,
        runtime_topology_transitions=transitions,
        transition_checker='tools/validate_backend_results.py::validate_topology_transitions',
        adjacent_snapshot_checks=pairs, regrid_coverage=measured,
        configured_max_blocks=capacity,
        instrumented_cuda_lanes=len(checkpoints) if sanitizer is not None else 0)


def retained_logs(output):
    """Identify diagnostics from completed or interrupted lanes without interpreting them."""
    return [provenance.file_identity(path) for path in sorted((output / 'runs').rglob('*'))
        if path.is_file() and (path.suffix in ('.par', '.tsv', '.txt', '.log')
                              or path.name in ('arch.stdout', 'arch.stderr'))]


def self_test():
    """Read-only derivation checks and in-memory topology negative controls."""
    import unittest

    class RecipeControls(unittest.TestCase):
        def test_only_observation_and_topology_requirements_are_derived(self):
            originals, cases, parameters = derive_cases()
            self.assertEqual(len(cases), len(CANONICAL_CASES))
            for original, case in zip(originals, cases):
                restored = deepcopy(case)
                for key in ('id', 'accepted_steps', 'topology_policy'):
                    restored[key] = original[key]
                self.assertEqual(restored, original)
                self.assertEqual(case['rkl_policy'], dict(order=1, stages=4, lanes_per_step=2))
                self.assertEqual(case['accepted_steps'], list(OBSERVATION_STEPS))
                self.assertEqual(parameters[case['id']]['geometry'], CANONICAL_CASES[original['id']])

        def test_successor_changes_only_the_observation_horizon(self):
            archived = json.loads((Path(__file__).resolve().parent /
                'release-920/attempt.json').read_text())
            originals, cases, _ = derive_cases()
            self.assertEqual(originals, archived['canonical_cases'])
            self.assertEqual(OBSERVATION_STEPS, (2, 5, 20, 80, 160))
            for previous, current in zip(archived['derived_cases'], cases):
                self.assertEqual(previous['accepted_steps'], [2, 5, 20, 80, 160, 320])
                restored = deepcopy(current)
                restored['accepted_steps'] = previous['accepted_steps']
                self.assertEqual(restored, previous)

        def test_runtime_parent_four_children_round_trip(self):
            parent, unchanged = [0, 0, 0, 0], [0, 2, 2, 0]
            children = [[1, 0, 0, 0], [1, 0, 1, 0], [1, 1, 0, 0], [1, 1, 1, 0]]

            def snapshot(step, leaves):
                return dict(step=step, dimension=2, topology=[unchanged, *leaves],
                    blocks=len(leaves) + 1, min_level=0,
                    max_level=max(item[0] for item in leaves))

            before, refined, coarsened = snapshot(2, [parent]), snapshot(5, children), snapshot(20, [parent])
            _, cases, _ = derive_cases()
            policy = cases[0]['topology_policy']
            self.assertEqual(runtime.validate_topology_policy(
                [before, refined, coarsened], policy, 'fixture'), dict(refined=True, derefined=True))
            for snapshots in ([before, refined], [refined, coarsened], [refined, refined]):
                with self.assertRaises(RuntimeError):
                    runtime.validate_topology_policy(snapshots, policy, 'fixture')
            with self.assertRaisesRegex(RuntimeError, 'no explicit derefine transition'):
                runtime.validate_topology_transitions(
                    [snapshot(5, children[:-1]), coarsened], require_refine=False, require_derefine=True)

    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(RecipeControls))
    return 0 if result.wasSuccessful() else 1


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
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
    originals, cases, parameters = derive_cases()
    identity_args = dict(arch=build / 'bin/ARCH',
        checkpoint_validator=build / 'arch_cuda_single_level_validation',
        source_root=ROOT, build_dir=build)
    before = provenance.capture(**identity_args)
    manifest_file = provenance.file_identity(MANIFEST)
    recipe = provenance.file_identity(Path(__file__).resolve())
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    sanitizer = CudaSanitizer(args.sanitizer, args.tool or 'memcheck') if args.sanitizer else None
    attempt = dict(schema=1, scope='Curved 2D application runtime refine/coarsen lifecycles',
        release_qualified=False, status='running', recipe=recipe, manifest=manifest_file,
        canonical_cases=originals, derived_cases=cases, runtime_inputs=inputs,
        command=[sys.executable, '-B', str(Path(__file__).resolve()), *sys.argv[1:]],
        instrumentation=dict(tool=sanitizer.tool, executable=sanitizer.identity) if sanitizer else None,
        provenance=before, binary_sha256=before['artifacts']['arch_sha256'],
        started_utc=datetime.now(timezone.utc).isoformat(),
        note='Fixed observation window; all original physics, capacity, thresholds, budgets and RKL four-stage policy retained. Initialization cannot satisfy bidirectional runtime topology coverage.')
    output.mkdir(parents=True, exist_ok=True)
    (output / 'attempt.json').write_text(json.dumps(attempt, indent=2, default=str) + '\n')
    records, coverage, active_case = [], [], None
    try:
        for case in cases:
            active_case = case['id']
            record = runtime.run_case(identity_args['arch'], identity_args['checkpoint_validator'],
                ROOT, case, output / 'runs', sanitizer)
            records.append(record)
            coverage.append(check_coverage(record, case, parameters[case['id']], sanitizer))
        if {item['geometry'] for item in coverage} != set(CANONICAL_CASES.values()):
            raise RuntimeError('both curved geometry routes must complete the lifecycle')
        if manifest_file != provenance.file_identity(MANIFEST) \
                or recipe != provenance.file_identity(Path(__file__).resolve()) \
                or inputs != runtime.runtime_case_inputs(cases, ROOT):
            raise RuntimeError('curved lifecycle recipe, manifest or runtime inputs changed')
        evidence = dict(attempt, status='pass', focused_gate_pass=True,
            cases=records, coverage=coverage, finished_utc=datetime.now(timezone.utc).isoformat())
        provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    except Exception as error:
        failure = dict(attempt, status='failed', focused_gate_pass=False,
            identity_verified_after_run=False, error=f'{type(error).__name__}: {error}',
            active_case=active_case, cases=records, coverage=coverage,
            retained_logs=retained_logs(output), finished_utc=datetime.now(timezone.utc).isoformat())
        (output / 'failure.json').write_text(json.dumps(failure, indent=2, default=str) + '\n')
        raise
    print('curved 2D application lifecycles PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    raise SystemExit(main())
