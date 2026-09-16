"""Preserve a completed native capacity attempt, including timeout evidence.

The factory/focused archive remains the source/network provenance parent. This
increment stores new logs, executables AND the costly frozen CUDA factory objects.
It never starts a test, changes a budget, or promotes a partial matrix to a pass.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess

from collect_factory import completion, inventory, sha, successful_guard


def matrix_status(record, output, code):
    expected = {f'audit{n}-{m}-pool{p}' for n in (150, 200)
                for m in ('be_nr', 'bd', 'ros4') for p in (8, 32)}
    if record['steps'] != 4 or record['duration'] != 1e-10:
        raise ValueError('original four-step physical trajectory required')
    passed = set()
    commands = {row['name']: row for row in record['commands']}
    if len(commands) != len(record['commands']):
        raise ValueError('duplicate command names')
    for run in record['runs']:
        name = run['name']
        if name not in expected or name in passed or run.get('passed') is not True:
            raise ValueError('invalid or duplicate passed harness')
        command = commands[name]
        if command.get('returncode') != 0 or command.get('timed_out') is not False:
            raise ValueError('passed harness without a completed successful command')
        args = command['command']
        method = name.split('-')[1]
        pool = name.rsplit('pool', 1)[1]
        if args[1:] != ['1e7', '3e9', '1e-10', '1e8', '1e-7', '4',
                        '--ode', method, '--storage-cells', '32', '33',
                        '--pool-cells', pool, 'c12=0.5', 'o16=0.5']:
            raise ValueError('trajectory arguments changed')
        lines = (output / (name + '.stdout')).read_text().splitlines()
        if lines.count('GENERATED_SPARSE_BURN_PARITY_PASS') != 1:
            raise ValueError('missing/ambiguous completion marker')
        metrics = [line for line in lines if line.startswith(('metrics,', 'gpu_step,', 'cpu_step,'))]
        if metrics != run['metrics']:
            raise ValueError('record/transcript mismatch')
        for kind in ('cpu_step', 'gpu_step'):
            keys = [(int(v.split(',')[2]), int(v.split(',')[3]))
                    for v in metrics if v.startswith(kind + ',')]
            if keys != [(storage, step) for storage in (32, 33) for step in range(4)]:
                raise ValueError('incomplete or reordered physical trajectory')
        passed.add(name)
    if code == 0 and (record['status'] != 'passed' or passed != expected):
        raise ValueError('worker success without all twelve complete harnesses')
    if code != 0 and record['status'] != 'failed':
        raise ValueError('failed worker without retained failed record')
    return dict(completed_harnesses=sorted(passed), planned_harnesses=sorted(expected),
                complete_matrix=(code == 0 and passed == expected))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--attempt', required=True)
    args = parser.parse_args()
    root = args.root.resolve(strict=True)
    projects = Path('/home/ubuntu/projects')
    if root.parent != projects or root.name != 'ARCH-native-wave-v4-20260916':
        raise ValueError('unexpected isolated root')
    if not re.fullmatch(r'v[1-9][0-9]*', args.attempt):
        raise ValueError('versioned attempt required')
    suffix = args.attempt
    control = root / ('capacity-control-' + suffix)
    output = root / ('capacity-' + suffix)
    code = completion(control)
    record = json.loads((output / 'record.json').read_text())
    status = matrix_status(record, output, code)
    # Do not archive a live experiment, or hide a resource-guard failure.
    for process in ('ARCH', 'nvcc', 'ptxas', 'cc1plus',
                    'arch_cuda_generated_sparse_burn_audit150',
                    'arch_cuda_generated_sparse_burn_audit200'):
        result = subprocess.run(['pgrep', '-f', '^.*/' + re.escape(process) + r'( |$)'],
                                capture_output=True)
        if result.returncode != 1:
            raise RuntimeError('owned computation active or process check failed')
    if code == 0:
        successful_guard(control / 'capacity-guard.log')
    source, build = root / 'source', root / 'factory-release'
    factory = root / 'factory-control-v2'
    for name in ('source-files.sha256', 'network-files.sha256', 'artifacts.sha256', 'vendor.sha256'):
        inventory(factory / name, source)
    dependencies = {str(p): dict(resolved=str(p.resolve()), bytes=p.stat().st_size, sha256=h)
                    for p, h in inventory(factory / 'vendor.sha256', source).items()}
    paths = {p for part in (control, output) for p in part.rglob('*') if p.is_file()}
    paths.update(factory / name for name in
                 ('source-files.sha256', 'network-files.sha256', 'artifacts.sha256', 'vendor.sha256'))
    paths.update(root / name for name in
                 ('factory-focused-collection-v1.json', 'factory-focused-local-receipt-v1.json'))
    paths.update(build / name for name in ('CMakeCache.txt', 'compile_commands.json',
                                         'build.ninja', 'libarch_cuda_sparse_provider.a'))
    paths.add(source / 'tests/cuda/test_generated_sparse_burn.cpp')
    for network in ('audit150', 'audit200'):
        paths.add(build / ('CMakeFiles/arch_cuda_generated_sparse_burn_' + network +
                          '.dir/tests/cuda/test_generated_sparse_burn_factory.cu.o'))
    for name in ('run_sparse_capacity_v1.py', 'capacity_worker_' + suffix + '.sh',
                 'dispatch_capacity_' + suffix + '.sh'):
        paths.add(root / 'input' / name)
    paths.update((Path(__file__).resolve(), Path(__file__).with_name('collect_factory.py').resolve()))
    for artifact in record['artifacts']:
        for label in ('factory', 'executable'):
            if label + '_path' in artifact:
                path = Path(artifact[label + '_path'])
                if sha(path) != artifact[label + '_sha256']:
                    raise ValueError('runtime artifact changed: ' + str(path))
                paths.add(path)
    if (sha(source / 'tests/cuda/test_generated_sparse_burn.cpp') != record['source_sha256']
            or sha(build / 'libarch_cuda_sparse_provider.a') != record['provider_sha256']
            or sha(build / 'compile_commands.json') != record['compile_commands_sha256']
            or sha(root / 'input/run_sparse_capacity_v1.py') != record['recipe_sha256']):
        raise ValueError('capacity input identity changed')
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(projects) for p in paths):
        raise ValueError('unsafe/missing collection input')
    evidence = root / ('capacity-evidence-' + suffix)
    raw = root / ('capacity-raw-' + suffix + '.tar.zst')
    packed = root / ('capacity-compact-' + suffix + '.tar.zst')
    receipt = root / ('capacity-collection-' + suffix + '.json')
    if any(p.exists() for p in (evidence, raw, packed, receipt)):
        raise ValueError('never overwrite an existing or partial collection')
    compact = evidence / 'compact'
    compact.mkdir(parents=True)
    manifest = {p.relative_to(projects).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p))
                for p in sorted(paths)}
    omitted = []
    for path in sorted(paths):
        relative = path.relative_to(projects)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or path.suffix in ('.a', '.o', '.so', '.dat', '.h5', '.zst'):
            omitted.append(relative.as_posix())
            continue
        target = compact / 'records' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, content in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                          ('external-dependencies.json', dependencies)):
        (compact / name).write_text(json.dumps(content, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects),
                    *manifest, str(compact.relative_to(projects))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(projects).as_posix()]['sha256']:
            raise ValueError('input changed during collection')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    summary = dict(**status, worker_exit_code=code, capacity_matrix_pass=status['complete_matrix'],
                   application_qualified=False, performance_qualified=False, release_qualified=False)
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    result = dict(**summary, raw=raw_record,
                  compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
                  embedded_compact=compact.relative_to(projects).as_posix())
    with receipt.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
