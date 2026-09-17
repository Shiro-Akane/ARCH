"""Archive the isolated fresh factory build and completed focused test attempt.

Records successful and failed focused attempts without converting a failure to
a qualification. Never starts a build/test or changes a source/library file.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inventory(manifest, base):
    result = {}
    for line in manifest.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64}) [ *](.+)', line)
        if not match:
            raise ValueError('invalid SHA manifest: ' + str(manifest))
        path = Path(match[2])
        if not path.is_absolute():
            path = base / path
        if path in result or sha(path) != match[1]:
            raise ValueError('duplicate/changed input: ' + str(path))
        result[path] = match[1]
    if not result:
        raise ValueError('empty SHA manifest: ' + str(manifest))
    return result


def completion(control):
    value = (control / 'exit-code').read_text().strip()
    if not re.fullmatch(r'\d{1,3}', value) or not 0 <= int(value) <= 255:
        raise ValueError('missing/invalid final worker exit code')
    return int(value)


def successful_guard(path):
    lines = path.read_text().splitlines()
    required = {
        'MEMORY_GUARD_RESULT ': ('guard_stopped=False', 'stop_reason=none'),
        'GPU_MEMORY_OBSERVATION ': ('scope=whole_device', 'complete=True'),
        'SYSTEM_PRESSURE_OBSERVATION ': ('scope=linux_system', 'complete=True'),
    }
    for prefix, tokens in required.items():
        matches = [line for line in lines if line.startswith(prefix)]
        if len(matches) != 1 or any(token not in matches[0].split() for token in tokens):
            raise ValueError('missing/failed/ambiguous guard evidence: ' + str(path))


def focused_status(root, code):
    reports = {}
    for network in ('audit150', 'audit200'):
        path = root / 'focused-v1' / network / 'evidence.json'
        if path.exists():
            report = json.loads(path.read_text())
            if (report.get('focused_gate_pass') is not True
                    or report.get('release_qualified') is not False
                    or report.get('identity_verified_after_run') is not True
                    or set(report['summary']['methods']) != {'1', '2', '3'}
                    or report['summary']['steps'] != 4
                    or report['summary']['storage_sizes'] != [2, 3]
                    or report['summary']['requested_pool_cells'] != 2):
                raise ValueError('incomplete/mismatched focused report: ' + network)
            transcript = root / 'focused-v1' / network / 'trajectory/arch.stdout'
            if sha(transcript) != report['transcript']['sha256']:
                raise ValueError('focused transcript changed: ' + network)
            reports[network] = report
    if code == 0 and set(reports) != {'audit150', 'audit200'}:
        raise ValueError('success worker without both complete focused reports')
    return reports


def collect(root):
    root = root.resolve(strict=True)
    projects = Path('/home/ubuntu/projects')
    if root.parent != projects or root.name != 'ARCH-native-wave-v4-20260916':
        raise ValueError('unexpected isolated root')
    source, build = root / 'source', root / 'factory-release'
    control = root / 'factory-control-v2'
    if completion(control) != 0 or 'NATIVE_FRESH_FACTORIES_BUILD_PASS_NOT_TRAJECTORY_PASS' not in (
            control / 'worker.log').read_text():
        raise ValueError('successful complete fresh build required')
    successful_guard(control / 'build-guard.log')
    code = completion(root / 'focused-control-v1')
    reports = focused_status(root, code)
    if code == 0:
        for network in ('audit150', 'audit200'):
            successful_guard(root / 'focused-control-v1' / (network + '-guard.log'))
    # No archive of active owned computation. An SSH disconnect never authorizes
    # restarting a worker or recording a partial run as successfully complete.
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus',
                 'arch_cuda_generated_sparse_burn_audit150',
                 'arch_cuda_generated_sparse_burn_audit200'):
        check = subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'],
                               capture_output=True)
        if check.returncode not in (0, 1) or check.returncode == 0:
            raise RuntimeError('owned computation still active: ' + name)
    paths = set()
    for name in ('source-files.sha256', 'network-files.sha256', 'artifacts.sha256'):
        paths.update(inventory(control / name, source))
    dependencies = {str(p): dict(resolved=str(p.resolve(strict=True)),
                        bytes=p.stat().st_size, sha256=expected)
                    for p, expected in inventory(control / 'vendor.sha256', source).items()}
    for report in reports.values():
        for record in report['identity']['build']['sparse_link']['libraries']:
            p = Path(record['path'])
            if sha(p) != record['sha256']:
                raise ValueError('focused dependency changed: ' + str(p))
            if p.is_relative_to(build):
                paths.add(p)
            else:
                dependencies[str(p)] = dict(resolved=str(p.resolve(strict=True)),
                    bytes=p.stat().st_size, sha256=record['sha256'])
    for part in ('input', 'factory-control', 'factory-control-v2', 'factory-overlay',
                 'focused-control-v1', 'focused-v1'):
        paths.update(p for p in (root / part).rglob('*') if p.is_file())
    for name in ('CMakeCache.txt', 'compile_commands.json', 'build.ninja', '.ninja_log'):
        paths.add(build / name)
    paths.add(source / 'CMakeLists.txt')
    for name in ('tools/run_memory_guarded.py', 'tools/validation_provenance.py',
                 'tools/validate_backend_results.py', 'validation/network/run_sparse_validation.py'):
        paths.add(source / name)
    paths.add(Path(__file__).resolve())
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(projects) for p in paths):
        raise ValueError('missing/symlinked/out-of-scope archive input')
    out = root / 'factory-focused-evidence-v1'
    raw = root / 'factory-focused-raw-v1.tar.zst'
    packed = root / 'factory-focused-compact-v1.tar.zst'
    receipt_path = root / 'factory-focused-collection-v1.json'
    if any(p.exists() for p in (out, raw, packed, receipt_path)):
        raise ValueError('preserve existing/partial archive outputs')
    compact = out / 'compact'
    compact.mkdir(parents=True)
    manifest = {p.relative_to(projects).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p))
                for p in sorted(paths)}
    (compact / 'raw-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (compact / 'external-dependencies.json').write_text(json.dumps(dependencies, indent=2) + '\n')
    omitted = []
    for p in sorted(paths):
        with p.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        relative = p.relative_to(projects)
        if binary or p.suffix in ('.a', '.o', '.so', '.tar', '.zst', '.dat', '.h5'):
            omitted.append(relative.as_posix())
            continue
        target = compact / 'records' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
    (compact / 'raw-only-files.json').write_text(json.dumps(omitted, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects),
                    *manifest, str(compact.relative_to(projects))], check=True)
    # Detect mutation during collection; a failed partial archive is retained.
    for p in paths:
        if sha(p) != manifest[p.relative_to(projects).as_posix()]['sha256']:
            raise ValueError('input changed during collection: ' + str(p))
    summary = dict(factory_build_pass=True, focused_worker_exit_code=code,
        focused_networks_passed=sorted(reports), focused_gate_pass=(code == 0),
        application_qualified=False, performance_qualified=False, release_qualified=False,
        external_dependencies_hash_only=len(dependencies), raw_only_files=len(omitted))
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(out), 'compact'], check=True)
    result = dict(**summary, raw=raw_record, compact=dict(path=str(packed),
                  bytes=packed.stat().st_size, sha256=sha(packed)))
    with receipt_path.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    collect(Path(sys.argv[1]))
