"""Revalidate and archive a finished leaf diagnostic, including first failures.

Never starts a build or GPU run. Scientific/ODE/application approval is not added.
"""
import json
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
NETWORKS = ROOT.parent / 'ARCH-large-networks-20260909'
sys.path.insert(0, str(ROOT / 'input'))
sys.path.insert(0, str(ROOT / 'leaf-inline-input-v1'))
from collect_factory import completion, inventory, sha, successful_guard
from run_leaf import VARIANTS, BUDGET, parse_output, snapshot, compare_snapshots, compile_recipe


def validate(record, out):
    if (record.get('status') != 'passed' or record.get('diagnostic_numerical_pass') is not True
            or record.get('identities_verified_after') is not True or record.get('budget') != BUDGET
            or record.get('order') != list(VARIANTS) or record.get('compositions') != ['co', 'uniform']
            or record.get('rho') != 1e7 or record.get('temperature') != 3e9
            or any(record.get(key) is not False for key in ('performance_qualified', 'application_qualified', 'release_qualified'))):
        raise ValueError('completed original diagnostic without expanded approval required')
    commands = {row['name']: row for row in record['commands']}
    if len(commands) != 20 or len(record['commands']) != 20 or len(record['runs']) != 16:
        raise ValueError('four compilations and sixteen real leaf runs required')
    rows = json.loads((ROOT / 'factory-release/compile_commands.json').read_text())
    expected_names, runs, comparisons = set(), [], []
    for network in (150, 200):
        for variant in ('baseline', 'inline'):
            name = f'compile-audit{network}-{variant}'
            expected_names.add(name)
            expected = compile_recipe(rows, network, out / 'timed_network_math.cu',
                out / f'audit{network}-{variant}/NetCustom_audit{network}.h', out / f'leaf-audit{network}-{variant}')
            if commands[name]['command'] != expected or commands[name]['environment'] != {} or commands[name]['timeout_seconds'] != 12000:
                raise ValueError('compile recipe/environment differed')
        for composition in ('co', 'uniform'):
            reference, current = None, []
            for ordinal, variant in enumerate(VARIANTS):
                name = f'audit{network}-{composition}-{ordinal}-{variant}'
                expected_names.add(name)
                binary = out / (name + '.bin')
                command = commands[name]
                if (command['command'] != [str(out / f'leaf-audit{network}-{variant}'), '1e7', '3e9']
                        or command['environment'] != {'ARCH_LEAF_COMPOSITION': composition, 'ARCH_LEAF_SNAPSHOT': str(binary)}
                        or command['timeout_seconds'] != 300):
                    raise ValueError('run recipe/environment differed')
                parsed = parse_output((out / (name + '.stdout')).read_text(), network, composition)
                vectors = snapshot(binary.read_bytes(), network)
                if parsed['nnz'] != vectors['nnz']:
                    raise ValueError('snapshot/transcript pattern mismatch')
                reference = reference or vectors
                parsed.update(name=name, variant=variant, composition=composition, network=network,
                    comparison_to_first_baseline=compare_snapshots(reference, vectors),
                    host_device_errors={key: value['host_device_error'] for key, value in vectors['fields'].items()})
                current.append(parsed)
                runs.append(parsed)
            medians = {v: statistics.median(row['median_gpu_ms'] for row in current if row['variant'] == v)
                       for v in ('baseline', 'inline')}
            comparisons.append(dict(network=network, composition=composition, median_gpu_ms=medians,
                                    baseline_over_inline=medians['baseline']/medians['inline']))
    if set(commands) != expected_names or record['runs'] != runs or record['comparisons'] != comparisons:
        raise ValueError('record differs from full actual evidence')
    for row in commands.values():
        if row.get('returncode') != 0 or row.get('timed_out') is not False or row['cwd'] != str(ROOT / 'factory-release'):
            raise ValueError('unsuccessful/incorrect command')
    return dict(diagnostic_numerical_pass=True, leaf_runs=16, comparisons=comparisons,
                trajectory_matrix_pass=False, performance_qualified=False, application_qualified=False, release_qualified=False)


def main():
    control, out = ROOT / 'leaf-inline-control-v1', ROOT / 'leaf-inline-v1'
    code = completion(control)
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'leaf-audit150-baseline', 'leaf-audit150-inline',
                 'leaf-audit200-baseline', 'leaf-audit200-inline'):
        if subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('owned compute remains active or process check failed')
    path = out / 'record.json'
    record = json.loads(path.read_text()) if path.exists() else {}
    summary = dict(worker_exit_code=code, diagnostic_numerical_pass=False, trajectory_matrix_pass=False,
                   performance_qualified=False, application_qualified=False, release_qualified=False)
    if code == 0:
        summary.update(validate(record, out))
        successful_guard(control / 'leaf-guard.log')
    elif record and record.get('status') != 'failed':
        # A killed process may not execute Python finally. Preserve its incomplete state,
        # do not fabricate a diagnostic pass or rewrite its record.
        summary['incomplete_record_status'] = record.get('status')
    paths = {path for directory in (control, out, ROOT / 'leaf-inline-input-v1')
             for path in directory.rglob('*') if path.is_file()}
    for name, digest in {**record.get('inputs', {}), **record.get('products', {})}.items():
        path = Path(name)
        if sha(path) != digest:
            raise ValueError('execution input/product changed: ' + name)
        paths.add(path)
    for directory, copied in record.get('network_copies', {}).items():
        current = {p.relative_to(directory).as_posix(): sha(p) for p in Path(directory).rglob('*') if p.is_file()}
        if current != copied:
            raise ValueError('private complete network inventory changed')
    factory = ROOT / 'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(factory / (name + '.sha256'), ROOT / 'source')
        paths.add(factory / (name + '.sha256'))
    dependencies = {str(path): dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=digest)
                    for path, digest in inventory(factory / 'vendor.sha256', ROOT / 'source').items()}
    paths.update((ROOT / 'input/collect_factory.py', Path(__file__).resolve()))
    if any(path.is_symlink() or not path.is_file() or not any(path.resolve().is_relative_to(base) for base in (ROOT, NETWORKS)) for path in paths):
        raise ValueError('unsafe/missing archive input')
    evidence = ROOT / 'leaf-inline-evidence-v1'
    raw, packed, receipt = ROOT / 'leaf-inline-raw-v1.tar.zst', ROOT / 'leaf-inline-compact-v1.tar.zst', ROOT / 'leaf-inline-collection-v1.json'
    if any(path.exists() for path in (evidence, raw, packed, receipt)):
        raise ValueError('preserve existing/partial collection')
    compact = evidence / 'compact'
    compact.mkdir(parents=True)
    projects = ROOT.parent
    manifest = {path.relative_to(projects).as_posix(): dict(bytes=path.stat().st_size, sha256=sha(path)) for path in sorted(paths)}
    omitted = []
    for path in sorted(paths):
        relative = path.relative_to(projects)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        # All original/private generated sources are in raw. Keep just the math
        # adapter in compact; avoid duplicating unchanged multi-MB rate tables in Git.
        generated = path.is_relative_to(NETWORKS) or (path.is_relative_to(out) and path.relative_to(out).parts[0].startswith('audit') and len(path.relative_to(out).parts) > 1)
        if binary or path.suffix in ('.o', '.a', '.so', '.zst', '.bin') or (generated and not path.name.endswith('.math.h')):
            omitted.append(relative.as_posix())
            continue
        target = compact / 'records' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted), ('external-dependencies.json', dependencies)):
        (compact / name).write_text(json.dumps(value, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects), *manifest, str(compact.relative_to(projects))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(projects).as_posix()]['sha256']:
            raise ValueError('archive input changed while collecting')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    collection = dict(**summary, raw=raw_record, compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
                      embedded_compact=compact.relative_to(projects).as_posix())
    with receipt.open('x') as stream:
        json.dump(collection, stream, indent=2)
        stream.write('\n')
    print(json.dumps(collection, indent=2))


if __name__ == '__main__':
    main()
