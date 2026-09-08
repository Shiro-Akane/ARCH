"""Check conserved mass/species at the coupled-gravity physical endpoints.

The ordinary runtime matrix already checks source-aware density, velocity,
pressure and total energy against constant acceleration. This audit rechecks
that evidence and applies its unchanged mass/rhoX conservation policy to the
saved fixed-time endpoints. Momentum and energy are not treated as conserved
in the presence of gravity. Existing helpers own all mathematics, checkpoint
inspection, process execution and provenance.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
MANIFEST = ROOT / 'validation/gravity/coupled_cases.json'
sys.path.insert(0, str(ROOT / 'tools'))
import qualify_cuda_amr_evidence as qualification
import validate_backend_results as runtime
import validation_provenance as provenance


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True)
    identity_args = dict(arch=build / 'bin/ARCH',
        checkpoint_validator=build / 'arch_cuda_single_level_validation',
        source_root=ROOT, build_dir=build)
    before = provenance.capture(**identity_args)
    report_file = provenance.file_identity(args.report.resolve())
    recipe = provenance.file_identity(Path(__file__).resolve())
    manifest_file = provenance.file_identity(MANIFEST)
    manifest = runtime.load_manifest(MANIFEST)
    inputs = runtime.runtime_case_inputs(manifest['cases'], ROOT)
    report = json.loads(args.report.read_text())
    provenance.require_evidence_identity(report, before)
    qualification.check_matrix(report, MANIFEST, ROOT)
    cases = {case['id']: case for case in manifest['cases']}
    started, records, observed = datetime.now(timezone.utc).isoformat(), [], {}
    for record in report['cases']:
        case = cases[record['id']]
        policy = case['conservation_policy']
        if policy['fields'] != ['mass', 'rhoX']:
            raise RuntimeError('gravity endpoint policy must preserve mass/species without masking its energy source')
        for backend in ('cpu', 'cuda'):
            lane = record['scientific'][backend]
            parameter, endpoint = Path(lane['parameter_file']), Path(lane['checkpoint'])
            parameters = runtime.read_parameter_map(parameter)
            initial = parameter.parent / (parameters['base_name'] + '_chk_0000.h5')
            for path in (parameter, initial, endpoint):
                observed[str(path)] = provenance.file_identity(path)
            if observed[str(parameter)]['sha256'] != lane['parameter_sha256'] \
                    or observed[str(endpoint)]['sha256'] != lane['checkpoint_sha256']:
                raise RuntimeError('saved scientific endpoint or parameters changed')
            initial_metadata = runtime.checkpoint_metadata(
                validator=identity_args['checkpoint_validator'], checkpoint=initial,
                parameters=parameter, expected_steps=0)
            terminal_metadata = runtime.checkpoint_metadata(
                validator=identity_args['checkpoint_validator'], checkpoint=endpoint,
                parameters=parameter, expected_steps=lane['steps'])
            if initial_metadata['time'] != 0.0 \
                    or terminal_metadata['time'] != case['scientific_time']:
                raise RuntimeError('gravity conservation endpoints have the wrong physical times')
            conservation = runtime.validate_conservation(
                identity_args['checkpoint_validator'], initial, endpoint, policy,
                parameter_file=parameter, parameter_sha256=lane['parameter_sha256'])
            source_balance = runtime.qualify_checkpoint(
                identity_args['checkpoint_validator'], endpoint, case,
                scientific=True, parameter_file=parameter)
            if source_balance != record['scientific'][backend + '_qualification']:
                raise RuntimeError('recomputed constant-acceleration evidence differs from the runtime report')
            records.append(dict(case=case['id'], backend=backend, conservation_policy=policy,
                initial_checkpoint=observed[str(initial)], initial_metadata=initial_metadata,
                terminal_checkpoint=observed[str(endpoint)], terminal_metadata=terminal_metadata,
                conservation=conservation, source_balance=source_balance))
            print('gravity physical-endpoint balance PASS: ' + case['id'] + '/' + backend, flush=True)
    if any(identity != provenance.file_identity(Path(path)) for path, identity in observed.items()) \
            or report_file != provenance.file_identity(args.report.resolve()) \
            or recipe != provenance.file_identity(Path(__file__).resolve()) \
            or manifest_file != provenance.file_identity(MANIFEST) \
            or inputs != runtime.runtime_case_inputs(manifest['cases'], ROOT):
        raise RuntimeError('gravity endpoint recipe, report or inputs changed during the audit')
    evidence = dict(schema=1, scope='coupled-gravity fixed-time mass/species conservation and analytic source balance',
        focused_gate_pass=True, release_qualified=False, application_report=report_file,
        manifest=manifest_file, recipe=recipe, runtime_inputs=inputs, inputs=observed,
        command=[sys.executable, '-B', str(Path(__file__).resolve()), *sys.argv[1:]],
        cases=records, started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        note='Conservation uses the manifest measure and original absolute/relative budgets; the independent gravity oracle checks sourced momentum/energy. This supplemental Cartesian coupling audit is not a full release certificate.')
    provenance.write_evidence(output / 'evidence.json', evidence, before, **identity_args)
    print('gravity coupled physical endpoints PASS: ' + str(output / 'evidence.json'))


if __name__ == '__main__':
    main()
