"""Audit collected original BE evidence; never a formal speedup qualification.

The raw archive must first be extracted, without overwriting existing files,
to a new local directory. This verifier does not connect to or alter a server.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import sys

BASE = 'ARCH-microphysics-20260914/build/p12-20260914'
RUN = BASE + '/factor-cache/long-be-extended-wall-v1'
EXPECTED = [(network, pool) for network in ('audit150', 'audit200') for pool in (8, 32)]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def safe_file(root, relative):
    path = PurePosixPath(relative)
    require(not path.is_absolute() and '..' not in path.parts and
            '\\' not in relative and ':' not in relative, 'unsafe evidence path')
    file = root.joinpath(*path.parts)
    require(file.is_file() and not file.is_symlink() and
            file.resolve().is_relative_to(root.resolve()), 'missing/escaped evidence file')
    return file


def validate_transcript(text, network, pool):
    lines = text.splitlines()
    species = {'audit150': 150, 'audit200': 200}[network]
    require(lines.count('GENERATED_SPARSE_BURN_PARITY_PASS') == 1, 'missing/duplicate completion')
    require([v for v in lines if v.startswith('storage_controls,')] ==
            [f'storage_controls,32,33,{pool}'], 'storage controls changed')
    expected = [(cells, step) for cells in (32, 33) for step in range(16)]
    rows = {}
    for prefix in ('cpu_step', 'gpu_step'):
        rows[prefix] = [v.split(',') for v in lines if v.startswith(prefix + ',')]
        require(all(len(v) == 7 and v[1] == '1' for v in rows[prefix]), 'unexpected step schema/ODE')
        require([(int(v[2]), int(v[3])) for v in rows[prefix]] == expected,
                'missing, duplicated or reordered steps')
        require(all(int(v[4]) > 0 and int(v[5]) >= 0 and
                    math.isfinite(float(v[6])) and float(v[6]) > 0 for v in rows[prefix]),
                'invalid step counters/time')
    require(all(int(v[5]) <= int(v[4]) for v in rows['cpu_step']), 'invalid CPU rejection count')
    states = [v.split(',') for v in lines if v.startswith('state,')]
    require([(v[1], int(v[2])) for v in states] == [('1', 32), ('1', 33)], 'state completion differs')
    require(all(len(v) == species + 9 and all(math.isfinite(float(x)) for x in v[3:])
                for v in states), 'invalid final state schema/value')
    metrics = [v.split(',') for v in lines if v.startswith('metrics,')]
    require(len(metrics) == 1 and len(metrics[0]) == 9 and metrics[0][1] == '1', 'metrics schema differs')
    m = metrics[0]
    require(int(m[2]) == sum(int(v[4]) for v in rows['cpu_step']) and
            int(m[3]) == sum(int(v[5]) for v in rows['cpu_step']), 'CPU totals differ')
    field, limiter, evolution = map(float, m[4:7])
    require(all(math.isfinite(v) for v in (field, limiter, evolution)) and
            0 <= field <= 2e-10 and 0 <= limiter <= 2e-8 and
            evolution > 64 * sys.float_info.epsilon, 'original numerical/evolution gate failed')
    require(int(m[7]) == pool and int(m[8]) > 0, 'pool/workspace differs')
    return dict(network=network, capacity=pool, storage_cells=[32, 33], steps_per_storage=16,
                cpu_attempts=int(m[2]), cpu_rejections=int(m[3]), max_field_error=field,
                max_limiter_error=limiter, max_abundance_evolution=evolution,
                workspace_bytes_per_lane=int(m[8]),
                diagnostic_cpu_seconds=sum(float(v[6]) for v in rows['cpu_step']),
                diagnostic_gpu_seconds=sum(float(v[6]) for v in rows['gpu_step']),
                kernels=sum(int(v[4]) for v in rows['gpu_step']),
                synchronizations=sum(int(v[5]) for v in rows['gpu_step']))


def validate_guard(text, controller):
    require(controller.splitlines().count('BE_FOLLOWUP_RUNTIME_EXIT 0') == 1,
            'runtime/outer guard did not report successful exit')
    rows = {}
    for prefix in ('MEMORY_GUARD_RESULT', 'GPU_MEMORY_OBSERVATION', 'SYSTEM_PRESSURE_OBSERVATION'):
        matches = [v for v in text.splitlines() if v.startswith(prefix + ' ')]
        require(len(matches) == 1, 'missing/duplicate guard observation: ' + prefix)
        rows[prefix] = dict(item.split('=', 1) for item in matches[0].split()[1:])
    memory = rows['MEMORY_GUARD_RESULT']
    require(memory['guard_stopped'] == 'False' and memory['stop_reason'] == 'none', 'resource guard stopped run')
    require(int(memory['min_available_kib']) >= 32768 * 1024 and
            0 <= int(memory['peak_swap_kib']) - int(memory['baseline_swap_kib']) <= 64 * 1024,
            'original memory headroom/swap budget exceeded')
    require(rows['GPU_MEMORY_OBSERVATION']['complete'] == 'True' and
            rows['GPU_MEMORY_OBSERVATION']['scope'] == 'whole_device' and
            rows['SYSTEM_PRESSURE_OBSERVATION']['complete'] == 'True', 'incomplete resource observations')
    pressure = rows['SYSTEM_PRESSURE_OBSERVATION']
    require(pressure['scope'] == 'linux_system' and
            (float(pressure['memory_limit_percent']), float(pressure['io_limit_percent']),
             float(pressure['sustain_seconds'])) == (20., 50., 10.), 'pressure guard settings changed')
    return rows


def audit(root, raw_dir, raw_archive, compact_archive):
    manifest = json.loads((root / 'raw-manifest.json').read_text())
    receipt = json.loads((root / 'raw-archive.json').read_text())
    collection, end = json.JSONDecoder().raw_decode((root / 'collection.log').read_text())
    require((root / 'collection.log').read_text()[end:].strip() ==
            'LARGE_BE_EXTENDED_WALL_ARCHIVE_PASS_NOT_SCIENCE_PASS', 'collection receipt incomplete')
    require(collection['raw'] == receipt, 'raw receipts disagree')
    for archive, identity in ((raw_archive, receipt), (compact_archive, collection['compact'])):
        require(archive.stat().st_size == identity['bytes'] and digest(archive) == identity['sha256'],
                'archive backup identity differs')
    require(len(manifest) == receipt['files'], 'raw manifest count differs')
    expected_projection = set()
    for relative, expected_hash in manifest.items():
        raw = safe_file(raw_dir, relative)
        require(digest(raw) == expected_hash, 'raw member hash differs: ' + relative)
        with raw.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or raw.suffix in ('.o', '.a', '.so', '.zst'):
            continue
        projected = safe_file(root / 'records', relative)
        require(digest(projected) == expected_hash, 'projection hash differs: ' + relative)
        expected_projection.add(relative)
    require({v.relative_to(root / 'records').as_posix() for v in (root / 'records').rglob('*') if v.is_file()}
            == expected_projection, 'projection inventory differs')
    embedded = safe_file(raw_dir, RUN + '-archive/compact/raw-manifest.json')
    require(digest(embedded) == digest(root / 'raw-manifest.json'), 'raw embedded manifest differs')
    record = json.loads(safe_file(root / 'records', RUN + '/record.json').read_text())
    require(receipt['scientific_status'] == record['status'] == 'passed' and
            receipt['complete_matrix'] is True and receipt['formal_timing'] is False,
            'not a completed original numerical matrix')
    require((record['duration'], record['steps'], record['runtime_timeout_seconds'],
             record['build_command_timeout_seconds']) == (1e-9, 16, 21600, 1800) and
            'observer' not in record, 'original protocol changed')
    names = [f'{n}-be_nr-pool{c}' for n, c in EXPECTED]
    require([r['name'] for r in record['runs']] == names and all(r['passed'] is True for r in record['runs'])
            and receipt['completed_harnesses'] == names and receipt['completed_storage_trajectories'] == 8,
            'incomplete/reordered harness matrix')
    require(len(record['commands']) == 8 and all(r['returncode'] == 0 and r['timed_out'] is False
            for r in record['commands']), 'compile/link/runtime command failure')
    identities = {
        BASE + '/be-extended-wall-recipes-v1/validation/network/run_sparse_capacity.py': record['recipe_sha256'],
        'ARCH-microphysics-20260914/tests/cuda/test_generated_sparse_burn.cpp': record['source_sha256'],
        BASE + '/factor-cache/candidate-v2/libarch_cuda_sparse_provider.a': record['provider_sha256'],
        'ARCH-perf-20260909/build/large-network-20260909/release/compile_commands.json': record['compile_commands_sha256'],
    }
    require([r['network'] for r in record['artifacts']] == ['audit150', 'audit200'], 'factory inventory differs')
    for artifact in record['artifacts']:
        for kind in ('factory', 'executable'):
            relative = PurePosixPath(artifact[kind + '_path']).relative_to('/home/ubuntu/projects').as_posix()
            identities[relative] = artifact[kind + '_sha256']
        for row in [r for r in record['runs'] if r['name'].startswith(artifact['network'] + '-')]:
            require(all(row[k + '_sha256'] == artifact[k + '_sha256'] for k in ('factory', 'executable')),
                    'runtime product binding differs')
    require(all(manifest.get(k) == v for k, v in identities.items()), 'recorded source/product identity differs')
    require(record['provider_sha256'] == '24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd',
            'not the frozen original provider')
    require(all(r['timeout_seconds'] == 1800 for r in record['commands'] if r['name'].startswith(('compile-', 'link-'))),
            'build timeout changed')
    summaries = []
    for network, pool in EXPECTED:
        name = f'{network}-be_nr-pool{pool}'
        text = safe_file(root / 'records', RUN + '/' + name + '.stdout').read_text()
        summaries.append(validate_transcript(text, network, pool))
        row = next(r for r in record['runs'] if r['name'] == name)
        require(row['metrics'] == [v for v in text.splitlines() if v.startswith(('metrics,', 'gpu_step,', 'cpu_step,'))],
                'record metrics differ from original stdout')
        commands = [r for r in record['commands'] if r['name'] == name]
        require(len(commands) == 1 and commands[0]['timeout_seconds'] == 21600, 'runtime wall budget differs')
        expected = ['1e7', '3e9', '1e-09', '1e8', '1e-7', '16', '--ode', 'be_nr',
                    '--storage-cells', '32', '33', '--pool-cells', str(pool), 'c12=0.5', 'o16=0.5']
        require(commands[0]['command'][1:] == expected, 'runtime physical/accuracy arguments differ')
        summaries[-1]['harness_wall_seconds'] = commands[0]['elapsed_seconds']
    guards = validate_guard(safe_file(root / 'records', BASE + '/factor-cache/long-be-extended-wall-v1-guard.log').read_text(),
                            (root / 'controller.log').read_text())
    return dict(status='collected_original_BE_matrix_and_bytes_verified',
                formal_speedup_qualified=False, full_Helm_application_qualified=False,
                independent_reaction_oracle=False, auditor_sha256=digest(Path(__file__)),
                raw_bytes=receipt['bytes'], compact_bytes=collection['compact']['bytes'],
                recorded_input_product_sha256=identities, raw_sha256=receipt['sha256'],
                compact_sha256=collection['compact']['sha256'], raw_files=len(manifest),
                projected_files=len(expected_projection), harnesses=summaries, resource_observations=guards)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'raw-dir', 'raw-archive', 'compact-archive', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    require(not args.output.exists(), 'refuse to overwrite an earlier audit')
    result = audit(args.root, args.raw_dir, args.raw_archive, args.compact_archive)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))
