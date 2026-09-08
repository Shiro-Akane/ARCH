"""Index this candidate's existing evidence; never run a simulation or certify a release.

One fixed delivery inventory, not a scientific verifier. Checks labelled
``recheck`` reuse existing integrity/coverage checkers; ``runner-record`` means
the original successful producer owns numerical acceptance. Missing reports
remain open; manual delivery requires an explicit --delivery-review record.
Run --self-test for CPU-only controls.

For this candidate use the sparse producer's Python 3.11 environment: its
serialized diagnostic time sums are compared exactly, including rounding.
Python 3.12 produces a different last bit for two of those timing sums; this
index does not widen either the diagnostic comparison or any science budget.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
import validate_backend_results as runtime
import qualify_cuda_amr_evidence as qualifier
import validation_sanitizer as sanitizer

SOURCE = '73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171'
RELEASE = ROOT / 'build/release-core-throughput-cmake'
ARCHIVE = HERE / 'release-73a9cf50'
ENV_KEYS = tuple(provenance.execution_environment_identity())
MANUAL_GATES = ('manual.documentation', 'manual.delivery_assets')


def report(module, family, run, name='evidence.json'):
    return f'validation/{module}/results/{family}-20260907/{run}/{name}'


# Counts are the established execution inventory, never numerical budgets.
# A tuple is (gate id, review kind, expected report, kind-specific contract).
GATES = [
    ('assets.recovery', 'assets', 'validation/network/results/asset-recovery-20260907/evidence.json', {}),
    ('regression.release', 'ctest', report('backend', 'final-first-law', 'release-regression-895'), {'tests': 98}),
    ('regression.cpu', 'ctest', report('backend', 'final-first-law', 'cpu-regression-897'), {'tests': 31}),
    ('regression.debug', 'ctest', report('backend', 'final-first-law', 'debug-regression-910'), {'tests': 98}),
    ('runtime.cartesian', 'matrix', report('amr', 'cartesian-native', 'release-872', 'backend-validation-evidence.json'), {'manifest': 'validation/amr/gpu_cases.json'}),
    ('runtime.curved', 'matrix', report('amr', 'curved-native', 'release-873', 'backend-validation-evidence.json'), {'manifest': 'validation/amr/gpu_curvilinear_cases.json'}),
    ('runtime.uniform', 'matrix', report('backend', 'uniform-native', 'release-874', 'backend-validation-evidence.json'), {'manifest': 'validation/backend/cases.json'}),
    ('runtime.generated', 'matrix', report('network', 'runtime-native', 'release-875', 'backend-validation-evidence.json'), {'manifest': 'validation/network/runtime_cases.json'}),
    ('restart.smooth', 'restart', report('amr', 'restart-smooth-native', 'release-876', 'restart-validation-evidence.json'), {'problem': 'SmoothAdvection', 'input': 'validation/amr/inputs/smooth_amr80_l1.par'}),
    ('restart.burn', 'restart', report('amr', 'restart-burn-native', 'release-877', 'restart-validation-evidence.json'), {'problem': 'BurnGradient', 'input': 'validation/amr/inputs/burn_enuc_amr.par'}),
    ('science.burn_independent', 'science', report('burn', 'independent-time-final', 'release-888'), {'scope': 'final-candidate independent built-in time/first-law/EOS review', 'records': 4}),
    ('science.burn_application', 'science', report('burn', 'application-first-law', 'release-878'), {'scope': 'actual-ARCH-aprox13-Helm-cross-solver-verification', 'cases': 3, 'cross_solver': 6}),
    ('science.sedov', 'science', report('hydro', 'sedov-first-law', 'release-889'), {'scope': 'planar strong-shock independent similarity', 'cases': 3}),
    ('science.nse', 'science', report('burn', 'nse-application-native', 'release-890'), {'scope': 'built-in NSE activation and coupled application energy closure', 'cases': 16}),
    ('science.eos_application', 'science', report('eos', 'application-native', 'release-891'), {'scope': 'normalized table coupled hydro/AMR and burn applications', 'cases': 12}),
    ('science.eos_balance', 'science', report('eos', 'application-native', 'endpoints-892'), {'scope': 'same-physical-time normalized-EOS conservation and burn source balance', 'cases': 24}),
    ('science.gravity_application', 'matrix', report('gravity', 'coupled-final', 'runtime-893', 'backend-validation-evidence.json'), {'manifest': 'validation/gravity/coupled_cases.json'}),
    ('science.gravity_balance', 'science', report('gravity', 'coupled-final', 'endpoints-894'), {'scope': 'coupled-gravity fixed-time mass/species conservation and analytic source balance', 'cases': 6}),
    ('science.gaussian', 'science', report('amr', 'gaussian-final', 'release-871'), {'scope': 'gaussian-initialization-and-thermal-activity', 'commands': 10, 'initialization': 6}),
    ('science.hydro_time', 'science', report('hydro', 'time-native', 'release-879'), {'scope': 'actual-ARCH-semidiscrete-hydro-time-order', 'cases': 9}),
    ('science.geometry', 'science', report('amr', 'geometry-native', 'release-880'), {'scope': 'independent-metrics-and-diffusion-spatial-operators', 'schema': 2}),
    ('science.weak_cv', 'science', report('network', 'weak-cv-native', 'release-898'), {'scope': 'controlled-Urca-weak-factory-and-trajectories'}),
    ('science.weak_helm', 'science', report('network', 'weak-helm-native', 'release-899'), {'scope': 'controlled-Urca-weak-factory-and-trajectories'}),
    ('science.sparse', 'sparse', report('network', 'sparse-native', 'release-900'), {}),
    ('reliability.sustained', 'sustained', report('amr', 'sustained-first-law', 'release-901'), {}),
    ('runtime.dynamic_3d', 'dynamic_3d', report('amr', 'dynamic-3d-final', 'release-919'), {'tool': None}),
    ('runtime.dynamic_curved', 'dynamic_curved', report('amr', 'dynamic-curved-final', 'release-923'), {'tool': None}),
    ('sanitizer.dynamic_3d_memcheck', 'dynamic_3d', report('amr', 'dynamic-3d-final', 'memcheck-914'), {'tool': 'memcheck'}),
    ('sanitizer.dynamic_3d_racecheck', 'dynamic_3d', report('amr', 'dynamic-3d-final', 'racecheck-915'), {'tool': 'racecheck'}),
    ('sanitizer.focused_memcheck', 'sanitizer', report('backend', 'final-first-law', 'memcheck-916'), {'tool': 'memcheck', 'timeout_seconds': 2400}),
    ('sanitizer.focused_racecheck', 'sanitizer', report('backend', 'final-first-law', 'racecheck-903'), {'tool': 'racecheck', 'timeout_seconds': 86400, 'sparse_race_interval': 1e-12}),
    ('sanitizer.curved_memcheck', 'instrumented_matrix', report('amr', 'curved-native', 'memcheck-904', 'backend-validation-evidence.json'), {'tool': 'memcheck'}),
    ('sanitizer.curved_racecheck', 'instrumented_matrix', report('amr', 'curved-native', 'racecheck-906', 'backend-validation-evidence.json'), {'tool': 'racecheck'}),
    ('sanitizer.restart_memcheck', 'instrumented_restart', report('amr', 'restart-burn-native', 'memcheck-905', 'restart-validation-evidence.json'), {'tool': 'memcheck', 'problem': 'BurnGradient', 'input': 'validation/amr/inputs/burn_enuc_amr.par'}),
    ('sanitizer.restart_racecheck', 'instrumented_restart', report('amr', 'restart-burn-native', 'racecheck-907', 'restart-validation-evidence.json'), {'tool': 'racecheck', 'problem': 'BurnGradient', 'input': 'validation/amr/inputs/burn_enuc_amr.par'}),
    ('resources.capacity', 'capacity', report('backend', 'device-memory-first-law', 'release-926'), {}),
    ('resources.compilation', 'cold', report('backend', 'cold-core-first-law', 'release-909'), {}),
    ('runtime.aggregate', 'aggregate', str(ARCHIVE.relative_to(ROOT) / 'runtime-qualification/arch.stdout'), {}),
]


class Rejected(RuntimeError):
    def __init__(self, kind, message):
        super().__init__(message)
        self.kind = kind


def require(condition, message, kind='coverage'):
    if not condition:
        raise Rejected(kind, message)


def objects(value):
    """Walk report dictionaries, omitting identity trees handled separately."""
    if isinstance(value, dict):
        yield value
        for key, item in value.items():
            if key not in ('identity', 'provenance', 'reference_identity'):
                yield from objects(item)
    elif isinstance(value, list):
        for item in value:
            if isinstance(item, (dict, list)):
                yield from objects(item)


def load_recipe(relative, name):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)  # Definitions only; never call its main().
    return module


@contextmanager
def recorded_environment(values):
    require(set(values) == set(ENV_KEYS), 'incomplete recorded execution controls', 'identity')
    require(all(value is None or isinstance(value, str) for value in values.values()),
            'invalid recorded execution control', 'identity')
    before = {key: os.environ.get(key) for key in ENV_KEYS}
    try:
        for key, value in values.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
        yield  # Only read-only provenance capture, never execution of a workload.
    finally:
        for key, value in before.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def check_ctest(data, inventory, junit, count):
    names = [test['name'] for test in inventory['tests']]
    cases = junit.findall('.//testcase')
    require(data['test_count'] == len(names) == len(set(names)) == count,
            'configured CTest inventory differs')
    require(sorted(names) == sorted(case.attrib['name'] for case in cases),
            'CTest names differ from inventory')
    require(not any(case.find(tag) is not None for case in cases
                    for tag in ('failure', 'error', 'skipped')), 'CTest contains failure/error/skip')


class Review:
    def __init__(self):
        self.source = provenance.source_identity(ROOT)
        require(self.source['worktree_sha256'] == SOURCE, 'current candidate source differs', 'source')
        self.identities, self.files, self.captures = {}, {}, []
        self.recipe = self.file(Path(__file__).resolve())
        self.interpreter = dict(invocation=str(Path(sys.executable).absolute()),
            version=sys.version, binary=self.file(Path(sys.executable).resolve()))

    def file(self, path, expected=None):
        path = Path(path).resolve()
        require(path.is_file(), f'missing evidence dependency: {path}', 'integrity')
        if path not in self.files:
            self.files[path] = (provenance.file_identity(path), provenance._artifact_observations({'file': path}))
        identity, observed = self.files[path]
        require(provenance._artifact_observations({'file': path}) == observed,
                f'file changed while indexing: {path}', 'integrity')
        if expected is not None:
            require(identity == expected, f'file hash/path mismatch: {path}', 'integrity')
        return identity

    def identity(self, old, *, historical_artifact=False):
        require(old['source'] == self.source, 'full source identity differs', 'source')
        build = Path(old['build']['cmake_options']['ARCH_BINARY_DIR']).resolve()
        if historical_artifact:
            # Cold measurement phases record earlier links in a separate tree.
            require(provenance.build_identity(build, old['build']['configuration']) == old['build'],
                    'measurement build inputs changed', 'identity')
        else:
            # Preserve each producer's actual controls. This capture does not
            # claim that the old execution environment was replayed.
            with recorded_environment(old['execution_environment']):
                if 'arch_sha256' in old['artifacts']:
                    paths = provenance._configured_artifact_paths(build, old['build'])
                    args = dict(arch=paths['arch'], checkpoint_validator=paths['checkpoint_validator'],
                                source_root=ROOT, build_dir=build, configuration=old['build']['configuration'])
                    capture = provenance.capture
                else:
                    args = dict(artifacts={key: Path(item['path']) for key, item in old['artifacts'].items()},
                                source_root=ROOT, build_dir=build, configuration=old['build']['configuration'])
                    capture = provenance.capture_focused
                key = (capture.__name__, repr(args), repr(old['execution_environment']))
                found = next((row for row in self.captures if row[0] == key), None)
                if found is None:
                    current = capture(**args)
                    self.captures.append((key, current, capture, args))
                else:
                    current = found[1]
            provenance.require_unchanged(old, current)
        for name, value in self.identities.items():
            if value == old:
                return name
        name = f'identity-{len(self.identities) + 1}'
        self.identities[name] = old
        return name

    def attachments(self, data):
        for item in objects(data):
            if isinstance(item.get('path'), str) and isinstance(item.get('sha256'), str):
                self.file(item['path'], {'path': item['path'], 'sha256': item['sha256']})

    def finish(self):
        require(provenance.source_identity(ROOT) == self.source, 'source changed during indexing', 'source')
        for _, before, capture, args in self.captures:
            with recorded_environment(before['execution_environment']):
                provenance.require_unchanged(before, capture(**args))
        for path, (_, stat) in self.files.items():
            require(provenance._artifact_observations({'file': path}) == stat,
                    f'evidence changed during indexing: {path}', 'integrity')


def success_record(data, contract):
    require(data.get('schema') == contract.get('schema', 1), 'unexpected producer schema')
    require(data.get('scope') == contract['scope'], 'unexpected producer scope')
    require(data.get('focused_gate_pass') is True and data.get('release_qualified') is False,
            'missing successful scoped producer record', 'recorded_execution')
    for field, count in contract.items():
        if field not in ('scope', 'schema'):
            require(isinstance(data.get(field), list) and len(data[field]) == count,
                    f'incomplete {field} coverage')


def sanitizer_records(data, tool):
    records = [item['sanitizer'] for item in objects(data) if isinstance(item.get('sanitizer'), dict)]
    require(bool(records), 'missing instrumentation records')
    for record in records:
        require(record['tool'] == tool, 'wrong sanitizer tool')
        summaries = sanitizer.check_report(Path(record['report']['path']).read_text(), tool)
        require(summaries == record['summaries'], 'sanitizer summaries changed')
    return len(records)


def check_application_sanitizer_lanes(data, tool, expected_count):
    lanes = [item for item in objects(data)
             if item.get('backend') == 'cuda' and 'resolved_plan' in item]
    require(len(lanes) == expected_count, 'instrumented application lane inventory differs')
    require(all(isinstance(lane.get('sanitizer'), dict) and lane['sanitizer'].get('tool') == tool
                for lane in lanes), 'a CUDA application lane lacks its sanitizer record')
    paths = [Path(lane['sanitizer']['report']['path']).resolve() for lane in lanes]
    require(len(set(paths)) == len(lanes), 'CUDA application lanes reuse a sanitizer report')
    require(all(path == Path(lane.get('parameter_file', lane.get('parameter', ''))).resolve().with_name('sanitizer.log')
                for lane, path in zip(lanes, paths)), 'application sanitizer report is outside its lane')


def check_cold(data, review):
    success_record(data, {'scope': 'isolated optimized core compilation'})
    require(data['compiler_cache_disabled'] is True and data['cold_objects']['existing_compile_outputs'] == 0
            and data['cold_objects']['configured_compile_outputs'] > 0,
            'not a cold uncached build')
    require([p['name'] for p in data['phases']] ==
            ['cold', 'noop', 'incremental', 'pair_serial', 'pair_parallel'] and data['comparison_source'],
            'cold/incremental/paired concurrency coverage incomplete')
    measurement = {p['identity']['build']['cmake_options']['ARCH_BINARY_DIR'] for p in data['phases']}
    require(len(measurement) == 1 and data['reference_identity']['build']['cmake_options']['ARCH_BINARY_DIR'] not in measurement,
            'measurement must use one separate build tree')
    require(data['jobs'] > 1 and [p['jobs'] for p in data['phases']] ==
            [data['jobs'], data['jobs'], data['jobs'], 1, data['jobs']], 'paired compile scheduling differs')
    recipe = load_recipe('validation/backend/results/cold-core-first-law-20260907/replay.py', 'index_cold_recipe')
    refs = []
    for phase in data['phases']:
        refs.append(review.identity(phase['identity'], historical_artifact=True))
        require(phase['identity']['build']['registered_networks'] == data['reference_identity']['build']['registered_networks'],
                'measurement generated packages differ')
        for contract in (phase['equivalence_before'], phase['equivalence_after']):
            recipe.require_unchanged_contract(data['preflight'], contract)
            left, right = contract['measurement'], contract['reference']
            require(left['normalized_commands']['sha256'] == right['normalized_commands']['sha256']
                    and left['heavy_pool'] == right['heavy_pool'], 'compiler command equivalence differs')
        require('guard_stopped=False' in phase['resource_summary'], 'build guard did not finish')
    return refs


def check_sanitizer_inventory(data, contract, configured, recipe):
    """Separate the declared race window from full scientific/memcheck input."""
    require(data.get('tool') == contract['tool'], 'sanitizer tool differs')
    profile = recipe.sparse_profile(contract['tool'], contract.get('sparse_race_interval'))
    require(data.get('sparse_profile') == profile, 'sparse sanitizer purpose/controls differ')
    build = Path(data['identity']['build']['cmake_options']['ARCH_BINARY_DIR']).resolve()
    commands = recipe.sanitizer_commands(build, configured, profile)
    records = data['cases']
    require(len(records) == len(commands) and {c['name'] for c in records} == set(commands),
            'sanitizer route inventory differs')
    require(all(c['returncode'] == 0 and c['command'] == commands[c['name']] for c in records),
            'sanitizer executable or physical arguments differ')
    require(all(isinstance(c.get('sanitizer'), dict) and c['sanitizer'].get('tool') == contract['tool']
                for c in records), 'a focused route lacks its own sanitizer record')
    paths = [Path(c['sanitizer']['report']['path']).resolve() for c in records]
    require(len(set(paths)) == len(records), 'focused routes reuse a sanitizer report')
    require(all(path == Path(c['stdout']['path']).resolve().with_name('sanitizer.log')
                and path.parent == Path(c['stderr']['path']).resolve().parent
                for c, path in zip(records, paths)), 'sanitizer report is outside its execution lane')
    return next(c for c in records if c['name'] == 'audit31_sparse_cells'), profile


def check_special(gate_id, kind, data, contract, review):
    """Review coverage, delegating existing numerical/log acceptance where available."""
    if kind == 'ctest':
        success_record(data, {'scope': 'complete-configured-cpu-cuda-regression'})
        check_ctest(data, json.loads(Path(data['inventory']['path']).read_text()),
                    ET.parse(data['junit']['path']).getroot(), contract['tests'])
    elif kind == 'matrix':
        qualifier.check_matrix(data, ROOT / contract['manifest'], ROOT)
    elif kind in ('restart', 'instrumented_restart'):
        qualifier.check_restart(data, problem=contract['problem'], input_path=ROOT / contract['input'],
                                source_root=ROOT, checkpoint_validator=None)
    elif kind == 'instrumented_matrix':
        manifest = runtime.load_manifest(ROOT / 'validation/amr/gpu_curvilinear_cases.json')
        selected = next(c for c in manifest['cases'] if c['id'] == 'spherical_coupled_amr_rkl2_wedge_3d')
        require(len(data['cases']) == 1 and data['cases'][0]['id'] == selected['id'], 'wrong instrumented case')
        require(data['manifest_sha256'] == provenance.sha256(ROOT / 'validation/amr/gpu_curvilinear_cases.json'),
                'instrumented manifest changed')
        checkpoints = data['cases'][0]['checkpoints']
        require(len(checkpoints) == len(selected['accepted_steps']), 'instrumented steps missing')
        for record, step in zip(checkpoints, selected['accepted_steps']):
            qualifier.check_comparison(record['parity'], selected['reduction_policy'], steps=step)
            for backend in ('cpu', 'cuda'):
                qualifier.check_lane(record[backend], backend=backend, steps=step, initial=True,
                                     trace=backend == 'cuda', regrid=True)
                conserved = record[backend + '_conservation']
                require(runtime.validate_conservation_metrics(conserved['before'], conserved['after'],
                        selected['conservation_policy']) == conserved, 'instrumented conservation summary differs')
    elif kind == 'sanitizer':
        success_record(data, {'scope': 'final-focused-backend-sanitizer', 'cases': 23})
        require(data.get('timeout_seconds') == contract['timeout_seconds'],
                'recorded complete-route wall-time allowance differs')
        recipe = load_recipe('validation/backend/results/final-first-law-20260907/run_sanitizers.py', 'index_sanitizer_recipe')
        regression_path = ROOT / report('backend', 'final-first-law', 'release-regression-895')
        review.file(regression_path)
        regression = json.loads(regression_path.read_text())
        require(regression['identity']['build'] == data['identity']['build'],
                'sanitizer and reference CTest build identities differ', 'identity')
        inventory_file = regression['inventory']
        review.file(inventory_file['path'], inventory_file)
        inventory = json.loads(Path(inventory_file['path']).read_text())
        configured = {test['name']: test['command'] for test in inventory['tests']}
        record, profile = check_sanitizer_inventory(data, contract, configured, recipe)
        registered = [entry['manifest'] for entry in data['identity']['build']['registered_networks']
                      if entry['manifest']['network_id'] == 'audit31']
        require(len(registered) == 1, 'sparse sanitizer network is not uniquely registered')
        summary = recipe.sparse_validation.parse_transcript(
            Path(record['stdout']['path']).read_text(), registered[0], profile['controls'], profile['steps'])
        # JSON stringifies integer method keys and turns the storage tuple into
        # a list; preserve every numerical diagnostic exactly after that round trip.
        require(json.loads(json.dumps(summary)) == record.get('sparse_summary'),
                'sparse sanitizer transcript/coverage summary differs')
    elif kind in ('dynamic_3d', 'dynamic_curved'):
        family, scope, count = (
            ('dynamic-3d-final', 'Cartesian 3D application runtime refine/coarsen lifecycle', 1)
            if kind == 'dynamic_3d' else
            ('dynamic-curved-final', 'Curved 2D application runtime refine/coarsen lifecycles', 2))
        success_record(data, {'scope': scope, 'cases': count})
        require(data.get('status') == 'pass', 'dynamic AMR execution did not pass')
        path = f'validation/amr/results/{family}-20260907/replay.py'
        recipe = load_recipe(path, 'index_' + kind + '_recipe')
        if kind == 'dynamic_3d':
            original, case, effective = recipe.derive_case()
            require(data['canonical_case'] == original and data['derived_case'] == case,
                    'dynamic 3D case or observation window changed')
            cases, parameters, recorded_coverage = [case], {case['id']: effective}, [data['coverage']]
        else:
            originals, cases, parameters = recipe.derive_cases()
            require(data['canonical_cases'] == originals and data['derived_cases'] == cases,
                    'dynamic curved cases or observation windows changed')
            recorded_coverage = data['coverage']
        require(len(cases) == count, 'dynamic AMR recipe case inventory differs')
        require(data['runtime_inputs'] == runtime.runtime_case_inputs(cases, ROOT),
                'dynamic AMR runtime inputs changed')
        review.file(ROOT / path, data['recipe'])
        review.file(recipe.MANIFEST, data['manifest'])
        instrumentation = data['instrumentation']
        instrument = None
        if contract['tool'] is None:
            require(instrumentation is None, 'ordinary dynamic AMR record is instrumented')
        else:
            require(isinstance(instrumentation, dict) and instrumentation['tool'] == contract['tool'],
                    'dynamic AMR instrumentation differs')
            instrument = sanitizer.CudaSanitizer(Path(instrumentation['executable']['path']), contract['tool'])
            require(instrument.identity == instrumentation['executable'], 'dynamic AMR sanitizer changed')
        coverage = []
        for record, case in zip(data['cases'], cases):
            coverage.append(recipe.check_coverage(record, case, parameters[case['id']], instrument))
            for checkpoint, step in zip(record['checkpoints'], case['accepted_steps']):
                qualifier.check_comparison(checkpoint['parity'], case['reduction_policy'], steps=step,
                    comparison_mode=runtime.checkpoint_comparison_mode(case))
                for backend in ('cpu', 'cuda'):
                    qualifier.check_lane(checkpoint[backend], backend=backend, steps=step,
                        initial=True, trace=backend == 'cuda', regrid=True,
                        rkl_policy=case.get('rkl_policy') if backend == 'cuda' else None,
                        plan_policy=case.get('plan_policy'))
                    qualifier.check_qualification(checkpoint.get(backend + '_qualification'), case)
                    conserved = checkpoint[backend + '_conservation']
                    require(runtime.validate_conservation_metrics(conserved['before'], conserved['after'],
                            case['conservation_policy']) == conserved, 'dynamic AMR conservation summary differs')
                    if case['conservation_policy'].get('measure') == 'physical_cell_volume':
                        require(checkpoint[backend]['parameter_sha256'] == conserved['before']['parameter_sha256'],
                                'dynamic AMR physical conservation parameter identity differs')
        require(coverage == recorded_coverage, 'dynamic AMR topology or instrumentation coverage differs')
        if instrument is not None:
            require(sanitizer_records(data, contract['tool']) == sum(len(c['accepted_steps']) for c in cases),
                    'dynamic AMR sanitizer lane coverage differs')
    elif kind == 'science':
        success_record(data, contract)
        if gate_id == 'science.burn_independent':
            require({c['network'] for c in data['records']} == {'iso7', 'aprox13', 'aprox19', 'aprox21'}, 'burn networks missing')
            expected = {(m, cap) for m in ('DOP853', 'Radau') for cap in (1., .125)}
            require(all(len(c['time_integrations']) == 4 and {(x['method'], x['max_step_fraction'])
                    for x in c['time_integrations']} == expected for c in data['records']), 'independent burn trajectories missing')
        if gate_id == 'science.nse':
            require(all(len(c['source_aware_balance']) == 2 for c in data['cases']), 'NSE endpoint balances missing')
        if gate_id.startswith('science.weak_'):
            expected_eos = 'helmholtz' if gate_id.endswith('helm') else 'constant_cv'
            require(data['independent']['controls'].get('eos', 'constant_cv') == expected_eos,
                    'wrong independent weak EOS profile')
            require({r['method'] for r in data['independent']['trajectories']} == {'DOP853', 'Radau'}
                    and data['tight_scientific_control']['pass'] is True, 'weak tight/independent coverage missing')
            # A coarse BE_NR control can intentionally fail; retain its record.
        if gate_id == 'science.geometry':
            require(all(data.get(key) for key in ('metric_references', 'viscous', 'radial_origin', 'scalars', 'transcripts')),
                    'independent geometry/operator records missing')
    elif kind == 'sparse':
        success_record(data, {'scope': 'real-generated-sparse-typed-factory-trajectories'})
        sys.path.insert(0, str(ROOT / 'validation/network'))
        from run_sparse_validation import parse_transcript
        package = next(r['manifest'] for r in data['identity']['build']['registered_networks'] if r['package'] == 'audit31')
        require(data['summary']['steps'] == 4, 'representative sparse trajectory length differs')
        # The producer serializes with sort_keys=True. Its parser accepts the
        # original command-field order, and returns integer method keys and a
        # tuple. Restore that declared input order and compare JSON values at
        # this archive boundary; do not alter the parser's numerical checks.
        controls = {name: data['controls'][name]
                    for name in ('rho', 'temperature', 'interval', 'cv', 'rtol')}
        parsed = parse_transcript(Path(data['transcript']['path']).read_text(), package, controls, 4)
        require(json.loads(json.dumps(parsed)) == data['summary'], 'sparse transcript differs')
    elif kind == 'sustained':
        success_record(data, {'scope': 'native-state sustained AMR and strict restore with fixed-time physics'})
        burn = data['burn_chains']
        require(len(burn['fixed_time_comparisons']) == 7 and len(burn['chains']) == 3
                and {c['pattern'] for c in burn['chains']} == {'cpu', 'cuda', 'alternating'}, 'sustained fixed-time coverage missing')
        require(data['smooth_chain']['cycles'] == 12 and all(c['cycles'] == 12 and len(c['exact_native_restores']) == 12
                and all(len(r['comparisons']) == 2 for r in c['exact_native_restores']) for c in burn['chains']),
                'sustained 12-cycle/72-native-restore coverage missing')
        require(len(data['matrices']) == 2 and len(data['derived_cases']) == 2, 'sustained matrices missing')
    elif kind == 'capacity':
        success_record(data, {'scope': 'bounded CUDA allocator capacity and storage overlap', 'records': 4})
        require({r['name'] for r in data['records']} == {'regrid_transaction', 'sparse_capacity', 'audit31_trajectory', 'amr_application'},
                'allocator workload inventory differs')
        path = 'validation/backend/results/device-memory-first-law-20260907/replay.py'
        recipe = load_recipe(path, 'index_capacity_recipe')
        review.file(ROOT / path, data['recipe'])
        instrumentation = data.get('instrumentation')
        require(isinstance(instrumentation, dict) and instrumentation.get('verified_unchanged') is True
                and isinstance(instrumentation.get('before'), dict)
                and instrumentation['before'] == instrumentation.get('after'),
                'missing or changed profiling tool identities')
        tools = instrumentation['before']
        require(set(tools) in ({'profiler'}, {'profiler', 'launcher'})
                and tools['profiler']['artifact'] == data['profiler'], 'profiling tool inventory differs')
        for tool in tools.values():
            review.file(tool['artifact']['path'], tool['artifact'])
        for record in data['records']:
            s = record['allocation_summary']
            require(recipe.check_allocation_lifetimes(s) == record.get('allocation_lifetimes'),
                    'missing or changed allocation lifetime classification')
            key = recipe.WORKLOAD_ARTIFACTS[record['name']]
            process_capture = record.get('process_identity_evidence')
            require(isinstance(process_capture, dict), 'missing observed CUDA executable identity')
            require(recipe.check_process_identities(s, process_capture,
                    {key: data['identity']['artifacts'][key]},
                    {key: data['identity']['artifact_observation'][key]},
                    launcher=tools.get('launcher')) == process_capture,
                    'changed CUDA PID to frozen workload executable association')
            require(s['physical_vram_measurement'] is False, 'allocator requests are not physical VRAM')
            require(any(p['peak_device_requested_bytes'] > 0 for p in s['processes']), 'missing allocation measurement')
            if record['name'] in ('audit31_trajectory', 'amr_application'):
                require(isinstance(record['ordinary_evidence'], dict), 'missing capacity scientific companion')
    if kind in ('sanitizer', 'instrumented_matrix', 'instrumented_restart'):
        expected = {'sanitizer': 23, 'instrumented_matrix': 2, 'instrumented_restart': 6}[kind]
        if kind != 'sanitizer':
            check_application_sanitizer_lanes(data, contract['tool'], expected)
        count = sanitizer_records(data, contract['tool'])
        require(count == expected, 'incomplete or duplicate original sanitizer reports')
        return f'{count} original sanitizer reports rechecked'
    return 'existing coverage/log checkers applied' if kind in ('ctest', 'matrix', 'restart', 'sparse', 'dynamic_3d', 'dynamic_curved') else 'original successful runner record and declared coverage reviewed; no scientific replay'


def review_gate(spec, review):
    gate_id, kind, relative, contract = spec
    path = ROOT / relative
    gate = dict(id=gate_id, required=True, status='pending', reason='Awaiting complete expected report.',
                expected_coverage=contract, reports=[dict(path=relative, exists=path.is_file(), sha256=None)], checks=[], attempts=[])
    if not path.is_file():
        return gate
    try:
        gate['reports'][0].update(review.file(path))
        data = json.loads(path.read_text())
        gate['reports'][0].update(reported_scope=data.get('scope', data.get('qualification_scope')),
                                  reported_release_qualified=data.get('release_qualified'))
        if kind == 'assets':
            require(data['scope'] == 'WSL restart generated-package reconstruction' and data['release_qualified'] is False,
                    'wrong reconstruction record')
            current = provenance.build_identity(RELEASE)['registered_networks']
            expected = {r['package']: {f['name']: f['sha256'] for f in r['files']} for r in current}
            require({r['package'] for r in data['records']} == {'audit31', 'weak_urca'}, 'recovered packages missing')
            for record in data['records']:
                installed = {f['name']: f['installed']['sha256'] for f in record['files']}
                require(installed == expected[record['package']], 'restored inventory differs from actual registered package')
                require(all(f['installed']['sha256'] == f['durable']['sha256'] for f in record['files']), 'durable package differs')
            gate['asset_history'] = data  # Includes prior hash and truthful audit31 metadata change.
            gate['checks'].append(dict(kind='recheck', description='Recovered and durable files match the actual CMake-registered package inventory; metadata changes retained.'))
        else:
            old = data.get('provenance', data.get('identity', data.get('reference_identity')))
            require(isinstance(old, dict), 'missing producer identity', 'identity')
            if kind not in ('cold', 'aggregate'):
                require(data.get('identity_verified_after_run') is True, 'producer did not recheck identity', 'identity')
            gate['reports'][0]['identity_ref'] = review.identity(old)
            gate['checks'].append(dict(kind='recheck', description='Full current source/build/artifact identity; original execution controls retained, not replayed.'))
            if kind == 'cold':
                gate['measurement_identity_refs'] = check_cold(data, review)
                gate['checks'].append(dict(kind='recheck', description='Original cold/no-op/incremental/concurrency phase records and existing command-equivalence checker; earlier phase binaries are historical artifacts, not the reference binary.'))
            elif kind == 'aggregate':
                require(data.get('status') == 'pass' and data.get('qualification_scope') == 'full-runtime'
                        and data.get('release_qualified') is False and data.get('matrix_cases') == 62
                        and data.get('restart_suites') == 2, 'incomplete runtime-only qualification')
                gate['checks'].append(dict(kind='runner-record', description='Existing runtime-only qualifier success and declared inventory reviewed; qualifier is not replayed.'))
            else:
                message = check_special(gate_id, kind, data, contract, review)
                gate['checks'].append(dict(kind='runner-record' if kind in ('science', 'sustained', 'capacity') else 'recheck', description=message))
        review.attachments(data)
        gate['checks'].append(dict(kind='recheck', description='Referenced path/SHA-256 attachments checked; no numerical reference regeneration.'))
        gate.update(status='pass', reason='Required scoped evidence and identity checks passed.')
    except Exception as error:
        gate.update(status='failed', reason=str(error), failure_kind=getattr(error, 'kind', 'report_review'))
    return gate


def historical_attempts(review):
    def attachments(names):
        return [review.file(ROOT / name) for name in names]
    return [
        dict(id='release-869', status='failed', disposition='superseded',
             reason='Stale cuda_policy_resolution numeric fingerprint; reconciled by focused886 and complete895.',
             files=attachments(['build/regression-final-release-869.log', 'build/policy-reference-device-886.log',
                 'validation/backend/results/final-first-law-20260907/release-regression-869/ctest.xml',
                 'validation/backend/results/final-first-law-20260907/release-regression-869/arch.stdout']),
             closure_gate='regression.release'),
        dict(id='debug-build-855', status='failed', disposition='interrupted',
             reason='WSL interruption; partial build is not a completed build. Registered temporary assets were subsequently reconstructed.',
             files=attachments(['build/final-debug-build-855.log']),
             recovery_gate='assets.recovery', successor_gate='regression.debug'),
        dict(id='memcheck-902', status='failed', failure_kind='timeout', disposition='retained',
             reason='Twenty-two routes completed; the unchanged four-step audit31 trajectory exceeded its 1200-second wall-time allowance without a complete sanitizer summary. No memory-guard stop occurred.',
             timeout_seconds=1200, completed_routes=22, expected_routes=23,
             files=attachments(['build/memcheck-final-902.log',
                 'validation/backend/results/final-first-law-20260907/attempt-902/run_sanitizers.py',
                 'validation/backend/results/final-first-law-20260907/memcheck-902/audit31_sparse_cells/arch.stdout',
                 'validation/backend/results/final-first-law-20260907/memcheck-902/audit31_sparse_cells/arch.stderr',
                 'validation/backend/results/final-first-law-20260907/memcheck-902/audit31_sparse_cells/sanitizer.log']),
             successor_attempt='memcheck-916', successor_gate='sanitizer.focused_memcheck'),
        dict(id='dynamic-3d-913', status='failed', failure_kind='coverage', disposition='retained',
             reason='The six snapshots completed, but they missed intermediate coarsening visible in both runtime regrid logs. The unchanged physical window is resampled at step 41 by the successor.',
             files=attachments(['build/dynamic-3d-final-913.log',
                 'validation/amr/results/dynamic-3d-final-20260907/attempt-913/replay.py',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/attempt.json',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/failure.json',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cpu/arch.stdout',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cpu/arch.stderr',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cpu/hydro_amr_regrid_cycle_3d_cpu_s80_regrid.tsv',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cuda/arch.stdout',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cuda/arch.stderr',
                 'validation/amr/results/dynamic-3d-final-20260907/release-913/runs/hydro_amr_regrid_cycle_3d/step-80/cuda/hydro_amr_regrid_cycle_3d_cuda_s80_regrid.tsv']),
             successor_attempt='release-919', successor_gate='runtime.dynamic_3d'),
        dict(id='capacity-908', status='failed', failure_kind='capacity-policy', disposition='retained',
             reason='The original all-allocations-closed policy rejected observed static-symbol residuals after explicit allocations closed. The reader and its all_allocations_released=false summary remain unchanged; the successor distinguishes allocation lifetimes and must complete all four workloads.',
             files=attachments(['build/capacity-final-908.log',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-908/replay.py',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-908/README.md',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-908/diagnosis.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-908/regrid_transaction/arch.stdout',
                 'validation/backend/results/device-memory-first-law-20260907/release-908/regrid_transaction/arch.stderr']),
             successor_attempt='release-921', successor_gate='resources.capacity'),
        dict(id='capacity-921', status='failed', failure_kind='process-identity', disposition='retained',
             reason='The first two allocation workloads and the 64-subdivision sparse scientific run completed. Nsight retained only a truncated child process name; no exact PID-to-executable association could be recovered. The final AMR capacity workload did not run. The successor must collect owned-process executable observations and complete all four workloads.',
             files=attachments(['build/capacity-final-921.log',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-921/replay.py',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-921/README.md',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-921/diagnosis.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-921/sparse-validation/evidence.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-921/audit31_trajectory/arch.stdout',
                 'validation/backend/results/device-memory-first-law-20260907/release-921/audit31_trajectory/arch.stderr']),
             successor_attempt='release-922', successor_gate='resources.capacity'),
        dict(id='dynamic-curved-920', status='failed', failure_kind='stage-witness', disposition='retained',
             reason='The cylindrical 320-step simulation completed, but six of 640 RKL records legitimately selected three stages after coarsening instead of the fixed-four-stage witness. Separate read-only field/conservation diagnosis does not certify this failed campaign; spherical execution did not start. The successor must observe both complete runtime lifecycles through step 160 with the original fixed-stage and scientific checks unchanged.',
             files=attachments(['build/dynamic-curved-final-920.log',
                 'validation/amr/results/dynamic-curved-final-20260907/attempt-920/replay.py',
                 'validation/amr/results/dynamic-curved-final-20260907/attempt-920/README.md',
                 'validation/amr/results/dynamic-curved-final-20260907/attempt-920/diagnosis.json',
                 'validation/amr/results/dynamic-curved-final-20260907/release-920/attempt.json',
                 'validation/amr/results/dynamic-curved-final-20260907/release-920/failure.json']),
             successor_attempt='release-923', successor_gate='runtime.dynamic_curved'),
        dict(id='capacity-922', status='failed', failure_kind='process-identity', disposition='retained',
             reason='The first regrid workload completed, but the exact-image join rejected a missing or multiple-image capture. That run did not archive its capture, so the alternatives cannot be distinguished retrospectively. No complete capacity evidence exists.',
             files=attachments(['build/capacity-final-922.log',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-922/replay.py',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-922/README.md',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-922/diagnosis.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-922/regrid_transaction/arch.stdout',
                 'validation/backend/results/device-memory-first-law-20260907/release-922/regrid_transaction/arch.stderr']),
             successor_attempt='release-926', successor_gate='resources.capacity'),
        dict(id='capacity-925', status='failed', failure_kind='process-identity', disposition='retained',
             reason='The retained selected-PID capture shows the installed Nsight launcher followed by the exact frozen regrid executable at one process start time. The strict single-image policy rejected that normal exec handoff. The successor explicitly pins the launcher and accepts only its nonoverlapping prefix; it must still complete all four original workloads.',
             files=attachments(['build/capacity-final-925.log',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-925/replay.py',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-925/README.md',
                 'validation/backend/results/device-memory-first-law-20260907/attempt-925/diagnosis.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-925/regrid_transaction/process-identity-audit.json',
                 'validation/backend/results/device-memory-first-law-20260907/release-925/regrid_transaction/arch.stdout',
                 'validation/backend/results/device-memory-first-law-20260907/release-925/regrid_transaction/arch.stderr']),
             successor_attempt='release-926', successor_gate='resources.capacity'),
    ]


def check_delivery_record(data, review):
    """Check an explicit review declaration, never assess document contents."""
    require(data.get('schema') == 1 and data.get('scope') == 'final-delivery-manual-review',
            'unexpected delivery review schema/scope', 'manual_record')
    require(data.get('source') == review.source, 'delivery review full source differs', 'source')
    require(isinstance(data.get('reviewer'), str) and data['reviewer'].strip(),
            'delivery reviewer is missing', 'manual_record')
    stamp = datetime.fromisoformat(data['reviewed_utc'].replace('Z', '+00:00'))
    require(stamp.utcoffset() is not None and stamp.utcoffset().total_seconds() == 0,
            'delivery review timestamp must explicitly use UTC', 'manual_record')
    entries = data.get('gates', [])
    require(len(entries) == len(MANUAL_GATES) and {entry['id'] for entry in entries} == set(MANUAL_GATES),
            'delivery review must explicitly cover both manual gates', 'manual_record')
    for entry in entries:
        require(entry.get('status') == 'pass' and isinstance(entry.get('explanation'), str)
                and entry['explanation'].strip(), 'delivery gate lacks an explicit pass and explanation', 'manual_record')
        require(isinstance(entry.get('files'), list) and entry['files'], 'delivery gate has no reviewed files', 'manual_record')
        for file in entry['files']:
            require(Path(file['path']).is_absolute(), 'reviewed file paths must be absolute', 'manual_record')
            review.file(file['path'], file)


def delivery_gates(path, review):
    gates = [dict(id=name, required=True, status='pending', reports=[], checks=[],
                  reason='Requires explicit delivery review; never closed automatically.') for name in MANUAL_GATES]
    archive = None
    if path is None:
        return gates, archive
    try:
        path = path.resolve()
        require(path.is_relative_to(HERE), 'delivery review must be inside this results tree', 'manual_record')
        archive = {'file': review.file(path)}
        for gate in gates:
            gate['reports'] = [archive['file']]
        data = json.loads(path.read_text())
        archive['record'] = data  # Preserve the actual declaration, including an invalid one.
        check_delivery_record(data, review)
        entries = {entry['id']: entry for entry in data['gates']}
        for gate in gates:
            gate.update(status='pass', reason=entries[gate['id']]['explanation'])
            gate['checks'] = [dict(kind='manual-record', description='Explicit reviewer pass recorded; full source, UTC timestamp and reviewed file identities checked. Document contents were not evaluated by this index.')]
    except Exception as error:
        for gate in gates:
            gate.update(status='failed', reason=str(error), failure_kind=getattr(error, 'kind', 'manual_record'))
    return gates, archive


def self_test():
    """Read-only controls: no candidate capture, process, temporary output or GPU."""
    import unittest

    class Controls(unittest.TestCase):
        def test_application_sanitizer_requires_one_report_per_cuda_lane(self):
            from copy import deepcopy
            for count in (2, 6):
                lanes = [dict(backend='cuda', resolved_plan={'resolved': 'cuda'},
                    **{('parameter' if count == 6 else 'parameter_file'): str(HERE / str(n) / 'case.par')},
                    sanitizer={'tool': 'memcheck', 'report': {'path': str(HERE / str(n) / 'sanitizer.log')}})
                    for n in range(count)]
                data = {'lanes': lanes, 'regrid_diagnostic': {'backend': 'cuda', 'macro_step': 1}}
                before = deepcopy(data)
                check_application_sanitizer_lanes(data, 'memcheck', count)
                self.assertEqual(data, before)
                for mutation in ('missing_lane', 'missing_report', 'duplicate', 'outside_lane', 'tool'):
                    invalid = deepcopy(data)
                    if mutation == 'missing_lane':
                        invalid['lanes'].pop()
                    elif mutation == 'missing_report':
                        invalid['lanes'][0].pop('sanitizer')
                    elif mutation == 'duplicate':
                        invalid['lanes'][0]['sanitizer'] = deepcopy(invalid['lanes'][1]['sanitizer'])
                    elif mutation == 'outside_lane':
                        invalid['lanes'][0]['sanitizer']['report']['path'] = str(HERE / 'outside' / 'sanitizer.log')
                    else:
                        invalid['lanes'][0]['sanitizer']['tool'] = 'racecheck'
                    with self.subTest(count=count, mutation=mutation), self.assertRaises(Rejected):
                        check_application_sanitizer_lanes(invalid, 'memcheck', count)

        def test_sanitizer_inventory_keeps_science_and_race_windows_distinct(self):
            from copy import deepcopy
            recipe = load_recipe('validation/backend/results/final-first-law-20260907/run_sanitizers.py',
                                 'index_test_sanitizer_recipe')
            configured = {name: [str(RELEASE / ('arch_' + name)), 'configured-argument']
                          for name in recipe.CASES}
            for contract in ({'tool': 'memcheck'},
                             {'tool': 'racecheck', 'sparse_race_interval': 1e-12}):
                profile = recipe.sparse_profile(contract['tool'], contract.get('sparse_race_interval'))
                commands = recipe.sanitizer_commands(RELEASE, configured, profile)
                data = dict(tool=contract['tool'], sparse_profile=profile,
                    identity={'build': {'cmake_options': {'ARCH_BINARY_DIR': str(RELEASE)}}},
                    cases=[dict(name=name, command=command, returncode=0,
                        stdout={'path': str(HERE / name / 'arch.stdout')},
                        stderr={'path': str(HERE / name / 'arch.stderr')},
                        sanitizer={'tool': contract['tool'],
                                   'report': {'path': str(HERE / name / 'sanitizer.log')}})
                           for name, command in commands.items()])
                before = deepcopy(data)
                record, checked_profile = check_sanitizer_inventory(data, contract, configured, recipe)
                self.assertEqual(data, before)
                self.assertEqual(record['name'], 'audit31_sparse_cells')
                self.assertEqual(checked_profile, profile)
                for mutation in ('tool', 'profile', 'interval', 'steps', 'missing', 'duplicate', 'returncode',
                                 'missing_sanitizer', 'wrong_sanitizer', 'reused_report', 'outside_lane', 'ordinary'):
                    invalid = deepcopy(data)
                    sparse = next(c for c in invalid['cases'] if c['name'] == 'audit31_sparse_cells')
                    if mutation == 'tool':
                        invalid['tool'] = 'memcheck' if contract['tool'] == 'racecheck' else 'racecheck'
                    elif mutation == 'profile':
                        invalid['sparse_profile'] = None
                    elif mutation == 'interval':
                        sparse['command'][3] = '1e-15'
                    elif mutation == 'steps':
                        sparse['command'][6] = '1'
                    elif mutation == 'missing':
                        invalid['cases'].pop()
                    elif mutation == 'duplicate':
                        invalid['cases'][-1] = deepcopy(invalid['cases'][0])
                    elif mutation == 'returncode':
                        sparse['returncode'] = 77
                    elif mutation == 'missing_sanitizer':
                        sparse.pop('sanitizer')
                    elif mutation == 'wrong_sanitizer':
                        sparse['sanitizer']['tool'] = 'other'
                    elif mutation == 'reused_report':
                        sparse['sanitizer']['report'] = deepcopy(invalid['cases'][0]['sanitizer']['report'])
                    elif mutation == 'outside_lane':
                        sparse['sanitizer']['report']['path'] = str(HERE / 'outside' / 'sanitizer.log')
                    else:
                        invalid['cases'][0]['command'].append('changed-control')
                    with self.subTest(tool=contract['tool'], mutation=mutation), self.assertRaises(Rejected):
                        check_sanitizer_inventory(invalid, contract, configured, recipe)
                wrong_contract = {'tool': contract['tool']}
                if contract['tool'] == 'racecheck':
                    with self.assertRaises(Rejected):
                        check_sanitizer_inventory(data, wrong_contract, configured, recipe)

        def test_missing_report_remains_pending(self):
            spec = ('test.missing', 'science', str(HERE / '__missing_test_report__.json'), {})
            self.assertFalse(Path(spec[2]).exists())
            gate = review_gate(spec, None)
            self.assertEqual(gate['status'], 'pending')
            self.assertEqual(gate['checks'], [])

        def test_dynamic_curved_requires_its_own_complete_report(self):
            from unittest.mock import patch
            spec, = [item for item in GATES if item[0] == 'runtime.dynamic_curved']
            self.assertEqual(spec[1:3], ('dynamic_curved',
                report('amr', 'dynamic-curved-final', 'release-923')))
            with patch.object(Path, 'is_file', return_value=False):
                gate = review_gate(spec, None)
            self.assertTrue(gate['required'])
            self.assertEqual(gate['status'], 'pending')
            self.assertEqual(gate['checks'], [])
            # The earlier matrix's successful single-direction coverage is
            # not a report of both complete runtime lifecycles.
            earlier = dict(schema=1, scope='backend-runtime-matrix',
                focused_gate_pass=True, release_qualified=False, status='pass', cases=[{}, {}])
            with self.assertRaisesRegex(Rejected, 'unexpected producer scope'):
                check_special(spec[0], spec[1], earlier, spec[3], None)

        def test_dynamic_curved_delegates_and_rejects_incomplete_coverage(self):
            from copy import deepcopy
            from types import SimpleNamespace
            from unittest.mock import Mock, patch
            recipe = load_recipe('validation/amr/results/dynamic-curved-final-20260907/replay.py',
                                 'index_test_dynamic_curved_recipe')
            originals, cases, parameters = recipe.derive_cases()
            conservation = dict(before={'parameter_sha256': 'synthetic'}, after={})
            records = [dict(id=case['id'], checkpoints=[dict(parity={},
                **{backend: {'parameter_sha256': 'synthetic'} for backend in ('cpu', 'cuda')},
                **{backend + '_conservation': conservation for backend in ('cpu', 'cuda')},
                **{backend + '_qualification': {'status': 'not-requested'} for backend in ('cpu', 'cuda')})
                for _ in case['accepted_steps']]) for case in cases]
            coverage = [{'id': case['id'], 'synthetic': True} for case in cases]
            checker = Mock(side_effect=lambda record, case, effective, instrument:
                next(item for item in coverage if item['id'] == case['id']))
            delegated = SimpleNamespace(derive_cases=recipe.derive_cases, check_coverage=checker,
                                       MANIFEST=recipe.MANIFEST)
            fixture = dict(schema=1, scope='Curved 2D application runtime refine/coarsen lifecycles',
                focused_gate_pass=True, release_qualified=False, status='pass',
                canonical_cases=originals, derived_cases=cases, cases=records, coverage=coverage,
                runtime_inputs=runtime.runtime_case_inputs(cases, ROOT),
                recipe={}, manifest={}, instrumentation=None)
            with patch(__name__ + '.load_recipe', return_value=delegated), \
                    patch.object(qualifier, 'check_comparison'), patch.object(qualifier, 'check_lane') as lane, \
                    patch.object(runtime, 'validate_conservation_metrics', return_value=conservation):
                check_special('runtime.dynamic_curved', 'dynamic_curved', fixture, {'tool': None}, Mock())
                self.assertEqual(checker.call_count, 2)
                for call, case, record in zip(checker.call_args_list, cases, records):
                    self.assertEqual(call.args, (record, case, parameters[case['id']], None))
                self.assertEqual(lane.call_count, 2 * sum(len(case['accepted_steps']) for case in cases))
                self.assertTrue(all(call.kwargs['rkl_policy'] == cases[0]['rkl_policy']
                    for call in lane.call_args_list if call.kwargs['backend'] == 'cuda'))
                for mutation in ('missing_case', 'short_window', 'missing_coverage', 'instrumented'):
                    data = deepcopy(fixture)
                    if mutation == 'missing_case':
                        data['cases'].pop()
                    elif mutation == 'short_window':
                        data['derived_cases'][0]['accepted_steps'].pop()
                    elif mutation == 'missing_coverage':
                        data['coverage'].pop()
                    else:
                        data['instrumentation'] = {'tool': 'memcheck'}
                    with self.subTest(mutation=mutation), self.assertRaises(Rejected):
                        check_special('runtime.dynamic_curved', 'dynamic_curved', data, {'tool': None}, Mock())

        def test_failed_runner_record_is_not_a_pass(self):
            data = dict(schema=1, scope='test', focused_gate_pass=False, release_qualified=False)
            with self.assertRaises(Rejected):
                success_record(data, {'scope': 'test'})

        def test_capacity_successor_requires_its_own_complete_report(self):
            from unittest.mock import patch
            spec, = [item for item in GATES if item[0] == 'resources.capacity']
            self.assertEqual(spec[1:3], ('capacity',
                report('backend', 'device-memory-first-law', 'release-926')))
            with patch.object(Path, 'is_file', return_value=False):
                gate = review_gate(spec, None)
            self.assertTrue(gate['required'])
            self.assertEqual(gate['status'], 'pending')
            self.assertEqual(gate['checks'], [])

        def test_capacity_delegates_lifetimes_and_rejects_incomplete_records(self):
            from copy import deepcopy
            from unittest.mock import Mock
            family = 'validation/backend/results/device-memory-first-law-20260907/'
            recipe = load_recipe(family + 'replay.py', 'index_test_capacity_recipe')
            fixture = load_recipe(family + 'test_allocation_lifetimes.py', 'index_capacity_fixtures')
            identity_fixture = load_recipe(family + 'test_process_identity.py', 'index_capacity_identity_fixtures')
            process_summary, capture, artifacts, observations = identity_fixture.fixture()
            summary = fixture.summarize_allocations(fixture.closed() +
                [fixture.event(3, 20, 'device_static')])
            summary['processes'][0].update(process_summary['processes'][0])
            classified = recipe.check_allocation_lifetimes(summary)
            identity = dict(artifacts={key: deepcopy(artifacts['sparse'])
                for key in recipe.WORKLOAD_ARTIFACTS.values()},
                artifact_observation={key: deepcopy(observations['sparse'])
                for key in recipe.WORKLOAD_ARTIFACTS.values()})
            profiler = dict(artifact={'path': '/tools/profiler', 'sha256': 'c' * 64},
                            artifact_observation=[1, 9, 10, 11, 12])
            tool_set = {'profiler': profiler}
            data = dict(schema=1, scope='bounded CUDA allocator capacity and storage overlap',
                focused_gate_pass=True, release_qualified=False, recipe={}, identity=identity,
                profiler=profiler['artifact'], instrumentation=dict(before=tool_set,
                    after=deepcopy(tool_set), verified_unchanged=True), records=[
                    dict(name=name, allocation_summary=deepcopy(summary),
                        allocation_lifetimes=deepcopy(classified), ordinary_evidence={},
                        process_identity_evidence=recipe.check_process_identities(summary, capture,
                            {recipe.WORKLOAD_ARTIFACTS[name]: artifacts['sparse']},
                            {recipe.WORKLOAD_ARTIFACTS[name]: observations['sparse']}))
                    for name in ('regrid_transaction', 'sparse_capacity', 'audit31_trajectory', 'amr_application')])
            before = deepcopy(data)
            check_special('resources.capacity', 'capacity', data, {}, Mock())
            self.assertEqual(data, before)
            self.assertTrue(all(r['allocation_lifetimes']['all_dynamic_allocations_released'] for r in data['records']))
            self.assertTrue(all(r['allocation_summary']['all_allocations_released'] is False for r in data['records']))
            # Exercise the index's declared-tool handoff, not only the leaf checker.
            handoff, launcher = identity_fixture.handoff_fixture()
            data['instrumentation']['before']['launcher'] = launcher
            data['instrumentation']['after'] = deepcopy(data['instrumentation']['before'])
            for record in data['records']:
                key = recipe.WORKLOAD_ARTIFACTS[record['name']]
                record['process_identity_evidence'] = recipe.check_process_identities(
                    summary, handoff[1], {key: artifacts['sparse']}, {key: observations['sparse']},
                    launcher=launcher)
            before = deepcopy(data)
            check_special('resources.capacity', 'capacity', data, {}, Mock())
            self.assertEqual(data, before)
            for mutation in ('dynamic_leak', 'static_only', 'missing_classification', 'changed_classification',
                             'physical_vram', 'missing_workload', 'missing_ordinary_evidence',
                             'missing_process_identity', 'wrong_process_image', 'changed_identity_claim',
                             'missing_instrumentation', 'changed_tools', 'unverified_tools'):
                invalid = deepcopy(data)
                record = invalid['records'][0]
                if mutation == 'dynamic_leak':
                    record['allocation_summary'] = fixture.summarize_allocations(fixture.closed() +
                        [fixture.event(3, 20, 'device_static'), fixture.event(4, 30)])
                elif mutation == 'static_only':
                    record['allocation_summary'] = fixture.summarize_allocations(
                        [fixture.event(3, 20, 'device_static')])
                elif mutation == 'missing_classification':
                    record.pop('allocation_lifetimes')
                elif mutation == 'changed_classification':
                    record['allocation_lifetimes']['all_allocations_released'] = True
                elif mutation == 'physical_vram':
                    record['allocation_summary']['physical_vram_measurement'] = True
                elif mutation == 'missing_workload':
                    invalid['records'].pop()
                elif mutation == 'missing_ordinary_evidence':
                    invalid['records'][-1]['ordinary_evidence'] = None
                elif mutation == 'missing_process_identity':
                    record.pop('process_identity_evidence')
                elif mutation == 'wrong_process_image':
                    record['process_identity_evidence']['observations'][0]['executable_path'] += '-other'
                elif mutation == 'changed_identity_claim':
                    record['process_identity_evidence']['processes'][0]['executable']['sha256'] = '0' * 64
                elif mutation == 'missing_instrumentation':
                    invalid.pop('instrumentation')
                elif mutation == 'changed_tools':
                    invalid['instrumentation']['after']['launcher']['artifact']['sha256'] = '0' * 64
                else:
                    invalid['instrumentation']['verified_unchanged'] = False
                with self.subTest(mutation=mutation), self.assertRaises((Rejected, ValueError)):
                    check_special('resources.capacity', 'capacity', invalid, {}, Mock())

        def test_skip_and_missing_test_are_rejected(self):
            inventory = {'tests': [{'name': 'a'}]}
            for xml in ('<testsuite><testcase name="a"><skipped/></testcase></testsuite>',
                        '<testsuite/>'):
                with self.assertRaises(Rejected):
                    check_ctest({'test_count': 1}, inventory, ET.fromstring(xml), 1)
            check_ctest({'test_count': 1}, inventory,
                        ET.fromstring('<testsuite><testcase name="a"/></testsuite>'), 1)

        def test_full_source_mismatch_with_same_hash_is_rejected(self):
            review = Review.__new__(Review)
            review.source = {'worktree_sha256': SOURCE, 'commit': 'candidate'}
            with self.assertRaises(Rejected) as caught:
                review.identity({'source': {'worktree_sha256': SOURCE, 'commit': 'other'}})
            self.assertEqual(caught.exception.kind, 'source')

        def test_hash_mismatch_is_rejected(self):
            review = Review.__new__(Review)
            review.files = {}
            path = Path(__file__).resolve()
            actual = review.file(path)
            self.assertEqual(actual, provenance.file_identity(path))
            with self.assertRaises(Rejected) as caught:
                review.file(path, {'path': str(path), 'sha256': '0' * 64})
            self.assertEqual(caught.exception.kind, 'integrity')

        def test_execution_controls_are_restored_after_error(self):
            before = provenance.execution_environment_identity()
            values = {key: None for key in ENV_KEYS}
            values['OMP_NUM_THREADS'] = '1'
            with self.assertRaisesRegex(RuntimeError, 'fixture'):
                with recorded_environment(values):
                    self.assertEqual(provenance.execution_environment_identity(), values)
                    raise RuntimeError('fixture')
            self.assertEqual(provenance.execution_environment_identity(), before)
            with self.assertRaises(Rejected):
                with recorded_environment({**values, 'OMP_NUM_THREADS': 1}):
                    self.fail('invalid controls must not be entered')
            self.assertEqual(provenance.execution_environment_identity(), before)

        def test_delivery_without_record_remains_pending(self):
            gates, archive = delivery_gates(None, None)
            self.assertIsNone(archive)
            self.assertTrue(all(g['status'] == 'pending' for g in gates))

        def test_delivery_record_outside_results_tree_is_rejected(self):
            gates, _ = delivery_gates(ROOT / 'README.md', None)
            self.assertTrue(all(g['status'] == 'failed' for g in gates))

        def test_delivery_negative_controls(self):
            from copy import deepcopy
            review = Review.__new__(Review)
            review.source, review.files = {'commit': 'in-memory fixture', 'worktree_sha256': SOURCE}, {}
            file = provenance.file_identity(Path(__file__).resolve())
            fixture = dict(schema=1, scope='final-delivery-manual-review', source=review.source,
                           reviewer='in-memory test fixture only', reviewed_utc='2026-09-07T00:00:00Z',
                           gates=[dict(id=name, status='pass', explanation='Synthetic control; not an actual review.',
                                       files=[file]) for name in MANUAL_GATES])
            mutations = (
                lambda d: d['gates'].pop(),
                lambda d: d['source'].update(commit='different'),
                lambda d: d['gates'][0]['files'][0].update(sha256='0' * 64),
                lambda d: d['gates'][0].update(status='pending'),
                lambda d: d['gates'][0].pop('status'),
                lambda d: d.update(reviewed_utc='2026-09-07T00:00:00'),
                lambda d: d.update(reviewed_utc='2026-09-07T09:00:00+09:00'),
                lambda d: d.update(reviewer=''),
            )
            for number, mutate in enumerate(mutations):
                data = deepcopy(fixture)
                mutate(data)
                with self.subTest(control=number), self.assertRaises(Rejected):
                    check_delivery_record(data, review)

    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
    return 0 if result.wasSuccessful() else 1


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--require-complete', action='store_true')
    parser.add_argument('--delivery-review', type=Path,
                        help='Explicit human/main-agent delivery review JSON inside this results tree')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    require(output.is_relative_to(HERE) and output != HERE, 'output must be a new child of this results directory')
    runtime.require_empty_output_root(output)
    review = Review()
    gates = [review_gate(spec, review) for spec in GATES]
    freeze = dict(id='artifacts.freeze', required=True, status='pending', reports=[], checks=[])
    try:
        path = ARCHIVE / 'final-artifacts.sha256'
        freeze['reports'].append(review.file(path))
        provenance.require_final_artifact_list(path, RELEASE / 'bin/ARCH', RELEASE)
        freeze.update(status='pass', reason='Existing final artifact list rechecked.')
        freeze['checks'].append(dict(kind='recheck', description='validation_provenance.require_final_artifact_list'))
    except Exception as error:
        freeze.update(status='failed' if path.exists() else 'pending', reason=str(error))
    gates.insert(0, freeze)
    by_id = {gate['id']: gate for gate in gates}
    dependencies = ['artifacts.freeze', 'runtime.cartesian', 'runtime.curved', 'runtime.uniform',
                    'runtime.generated', 'restart.smooth', 'restart.burn']
    aggregate = by_id['runtime.aggregate']
    aggregate['depends_on'] = dependencies
    if aggregate['status'] == 'pass' and any(by_id[name]['status'] != 'pass' for name in dependencies):
        aggregate.update(status='pending', reason='The referenced runtime components are not all accepted by this index.')
    manual, delivery_review = delivery_gates(args.delivery_review, review)
    gates.extend(manual)
    for name in ('audit150', 'audit200'):
        gates.append(dict(id='deferred.' + name, required=False, status='deferred', reports=[], checks=[],
                          reason='Owner-approved large-network external-machine queue, 2026-09-06 13:19 UTC; not a local pass.',
                          authority='docs/development/CudaReleaseStandard.md#owner-approved-scope-adjustment--2026-09-06-1319-utc'))
    history = historical_attempts(review)
    history[0]['closure_status'] = by_id['regression.release']['status']
    for attempt in history:
        if 'successor_gate' in attempt:
            attempt['successor_status'] = by_id[attempt['successor_gate']]['status']
            if attempt.get('failure_kind') in ('timeout', 'coverage', 'capacity-policy', 'process-identity', 'stage-witness') and attempt['successor_status'] == 'pass':
                attempt['disposition'] = 'superseded'
    review.finish()
    result = dict(schema=1, scope='final-release-evidence-index', release_qualified=False,
                  candidate_source=review.source, generated_utc=datetime.now(timezone.utc).isoformat(),
                  recipe=review.recipe, interpreter=review.interpreter,
                  identities=review.identities, gates=gates, historical_attempts=history,
                  delivery_review=delivery_review,
                  referenced_files=[identity for identity, _ in review.files.values()],
                  summary={status: sum(g['status'] == status for g in gates) for status in ('pass', 'pending', 'failed', 'deferred')})
    result['summary']['open_required_gates'] = [g['id'] for g in gates if g['required'] and g['status'] != 'pass']
    output.mkdir(parents=True, exist_ok=True)
    (output / 'index.json').write_text(json.dumps(result, indent=2) + '\n')
    rows = ['# Candidate evidence index', '',
            'This is a delivery audit of existing records, not a release certificate or a new scientific validation run.', '',
            '| Gate | Status | Review |', '| --- | --- | --- |']
    for gate in gates:
        reason = gate.get('reason', '').replace('|', '\\|').replace('\n', ' ')
        rows.append(f'| {gate["id"]} | {gate["status"]} | {reason} |')
    rows += ['', 'Full report identities, original execution controls and retained failed/interrupted attempts are in [index.json](index.json).',
             'The runtime qualifier and this index both retain `release_qualified: false`.', '']
    (output / 'README.md').write_text('\n'.join(rows))
    print(json.dumps(result['summary'], indent=2))
    return 1 if args.require_complete and result['summary']['open_required_gates'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
