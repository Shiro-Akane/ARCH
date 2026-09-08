"""Archive the bounded final-identity independent burn review (CPU only)."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'validation/eos'))
import validation_provenance as provenance
import validate_backend_results as runtime
from helm_reference import TABLE

BUILD = ROOT / 'build/release-core-throughput-cmake'
BINARY = BUILD / 'arch_burn_mainline_reference'
OUTPUT = Path(__file__).resolve().parent / 'release-888'
LOG = ROOT / 'build/burn-independent-final-888.log'
PYTHON = Path('/home/shiroakane/yt_env/bin/python')
RECIPE = ROOT / 'validation/burn/time_reference.py'
EXPECTED_SOURCE = '73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171'


def identity():
    return provenance.capture_focused(artifacts={'burn_reference': BINARY},
        source_root=ROOT, build_dir=BUILD)


def main():
    if os.environ.get('OMP_NUM_THREADS') != '1':
        raise RuntimeError('this CPU-only concurrent review requires OMP_NUM_THREADS=1')
    runtime.require_empty_output_root(OUTPUT)
    if LOG.exists():
        raise RuntimeError('refusing to overwrite a previous resource log')
    before = identity()
    if before['source']['worktree_sha256'] != EXPECTED_SOURCE:
        raise RuntimeError('candidate source identity no longer matches release-888')
    inputs = {str(path): provenance.file_identity(path)
              for path in (RECIPE, PYTHON, TABLE, Path(__file__).resolve())}
    command = [str(PYTHON), '-B', str(RECIPE), '--binary', str(BINARY),
               '--build-dir', str(BUILD)]
    guarded = ['/usr/bin/python3', '-B', str(ROOT / 'tools/run_memory_guarded.py'),
               '--min-available-mib', '1536', '--max-swap-growth-mib', '256',
               '--pressure-guard', '--log', str(LOG), '--', *command]
    OUTPUT.mkdir(parents=True, exist_ok=True)
    completed = runtime.run_arch_with_logs(guarded, source_root=ROOT,
        lane_root=OUTPUT, timeout=180)
    if LOG.exists():
        shutil.copyfile(LOG, OUTPUT / 'resource.log')
    if completed.returncode:
        raise RuntimeError(f'independent review/guard failed: {completed.returncode}')
    if completed.stderr:
        raise RuntimeError('unexpected guard stderr; inspect retained diagnostics')
    original_log = provenance.file_identity(LOG)
    resource_log = OUTPUT / 'resource.log'
    if provenance.sha256(resource_log) != original_log['sha256']:
        raise RuntimeError('resource archive differs from original log')
    text = resource_log.read_text(encoding='utf-8')
    review, end = json.JSONDecoder().raw_decode(text)
    trailers = [line for line in text[end:].splitlines() if line]
    if len(trailers) != 2 or not trailers[0].startswith('MEMORY_GUARD_RESULT ') \
            or 'guard_stopped=False' not in trailers[0] \
            or 'stop_reason=none' not in trailers[0] \
            or not trailers[1].startswith('SYSTEM_PRESSURE_OBSERVATION ') \
            or 'complete=True' not in trailers[1]:
        raise RuntimeError('missing clean complete resource/pressure record')
    if completed.stdout.strip().splitlines() != trailers:
        raise RuntimeError('guard stdout does not match retained resource summaries')
    if review.get('focused_gate_pass') is not True \
            or review.get('reference_derivation') is not False \
            or review.get('identity_verified_after_run') is not True:
        raise RuntimeError('review did not pass immutable-reference acceptance')
    records = review['records']
    if len(records) != 4 or {row['network'] for row in records} \
            != {'aprox13', 'aprox19', 'aprox21', 'iso7'}:
        raise RuntimeError('incomplete built-in network coverage')
    for row in records:
        integrations = row['time_integrations']
        if len(integrations) != 4 or {(x['method'], x['max_step_fraction'])
                for x in integrations} != {('DOP853', 1), ('DOP853', 0.125),
                                          ('Radau', 1), ('Radau', 0.125)}:
            raise RuntimeError('incomplete independent time/refinement coverage')
    provenance.require_unchanged(before, review['identity'])
    provenance.require_unchanged(before, identity())
    if review['table'] != inputs[str(TABLE)]:
        raise RuntimeError('review table identity differs from frozen input')
    for path, expected in inputs.items():
        if provenance.file_identity(Path(path)) != expected:
            raise RuntimeError(f'review input changed: {path}')
    if provenance.file_identity(LOG) != original_log:
        raise RuntimeError('original resource log changed during archiving')
    review_path = OUTPUT / 'review.json'
    review_path.write_text(text[:end] + '\n', encoding='utf-8')
    evidence = {
        'schema': 1,
        'scope': 'final-candidate independent built-in time/first-law/EOS review',
        'focused_gate_pass': True, 'release_qualified': False,
        'identity': before, 'identity_verified_after_run': True,
        'command': command, 'guarded_command': guarded,
        'inputs': inputs, 'archived_utc': datetime.now(timezone.utc).isoformat(),
        'independent_review': provenance.file_identity(review_path),
        'raw_logs': {str(path): provenance.file_identity(path)
                     for path in (resource_log, LOG, OUTPUT / 'arch.stdout',
                                  OUTPUT / 'arch.stderr')},
        'resource_summary': trailers[0], 'system_pressure': trailers[1],
        'records': records,
    }
    (OUTPUT / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n',
                                        encoding='utf-8')
    integrations = [item for row in records for item in row['time_integrations']]
    print(json.dumps({
        'focused_gate_pass': True, 'evidence': str(OUTPUT / 'evidence.json'),
        'source': before['source']['worktree_sha256'],
        'artifact': before['artifacts']['burn_reference']['sha256'],
        'maximum_species_linf': max(x['species_linf'] for x in integrations),
        'maximum_temperature_relative': max(x['relative_temperature'] for x in integrations),
        'maximum_endpoint_eos_relative': max(x['relative_endpoint_eos_error'] for x in records),
        'maximum_first_law_budget_fraction': max(x['first_law_absolute_error'] /
            x['first_law_absolute_budget'] for x in integrations),
        'resource_summary': trailers[0], 'system_pressure': trailers[1],
    }, indent=2))


if __name__ == '__main__':
    main()
