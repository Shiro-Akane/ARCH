"""Preserve a finished same-binary shape diagnostic, including failed attempts."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
sys.path.insert(0, str(ROOT / 'input' if (ROOT / 'input').is_dir() else Path(__file__).resolve().parent.parent))
from collect_factory import completion, inventory, sha, successful_guard
from run_diagnostic import METHODS, THREADS, arguments, validate


def main():
    control, output = ROOT / 'advance-shape-control-v1', ROOT / 'advance-shape-diagnostic-v1'
    code = completion(control)
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'arch_cuda_generated_sparse_burn_audit150',
                 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('owned compute still active or process check failed')
    path = output / 'record.json'
    record = json.loads(path.read_text()) if path.exists() else {}
    if code == 0:
        expected = {f'audit{n}-{m}-sample{i}-threads{t}' for n in (150, 200)
                    for m in METHODS for i, t in enumerate(THREADS)}
        actual = [row['name'] for row in record.get('runs', [])]
        if (record.get('status') != 'passed' or record.get('identities_verified_after') is not True
                or set(actual) != expected or len(actual) != len(expected)
                or {row['name'] for row in record.get('commands', [])} != expected | {'compile-probe'}
                or len(record['commands']) != len(expected) + 1
                or any(row.get('returncode') != 0 or row.get('timed_out') is not False for row in record['commands'])):
            raise ValueError('incomplete diagnostic cannot pass')
        for row in record['runs']:
            if (row['network'] not in (150, 200) or row['method'] not in METHODS
                    or row['sample'] not in range(len(THREADS))
                    or row['threads'] != THREADS[row['sample']]
                    or row['name'] != f"audit{row['network']}-{row['method']}-sample{row['sample']}-threads{row['threads']}"):
                raise ValueError('record route/order does not match ABBA')
            command = next(item for item in record['commands'] if item['name'] == row['name'])
            exe = ROOT / f"batch-launch-focused-v1/arch_cuda_generated_sparse_burn_audit{row['network']}"
            if (command['command'] != arguments(exe, row['method'])
                    or command['environment'] != {'LD_PRELOAD': str(output / 'advance_shape_probe.so'),
                                                  'ARCH_SPARSE_ADVANCE_THREADS': str(row['threads'])}):
                raise ValueError('actual command/environment mismatch')
            data = validate((output / (row['name'] + '.stdout')).read_text(),
                            (output / (row['name'] + '.stderr')).read_text(),
                            row['network'], row['method'], row['threads'])
            if any(row.get(k) != v for k, v in data.items()):
                raise ValueError('transcript/result changed')
        successful_guard(control / 'diagnostic-guard.log')
    paths = {p for directory in (control, output, ROOT / 'advance-shape-input-v1')
             for p in directory.rglob('*') if p.is_file()}
    for name, digest in {**record.get('inputs', {}), **record.get('artifacts', {})}.items():
        path = Path(name)
        if sha(path) != digest:
            raise ValueError('input/product changed: ' + name)
        paths.add(path)
    factory = ROOT / 'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(factory / (name + '.sha256'), ROOT / 'source')
        paths.add(factory / (name + '.sha256'))
    dependencies = {str(p): dict(resolved=str(p.resolve()), bytes=p.stat().st_size, sha256=h)
                    for p, h in inventory(factory / 'vendor.sha256', ROOT / 'source').items()}
    paths.update((ROOT / 'input/collect_factory.py', Path(__file__).resolve()))
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(ROOT) for p in paths):
        raise ValueError('unsafe/missing archive member')
    evidence = ROOT / 'advance-shape-evidence-v1'
    raw, packed = ROOT / 'advance-shape-raw-v1.tar.zst', ROOT / 'advance-shape-compact-v1.tar.zst'
    receipt = ROOT / 'advance-shape-collection-v1.json'
    if any(p.exists() for p in (evidence, raw, packed, receipt)):
        raise ValueError('existing/partial outputs must be preserved')
    compact = evidence / 'compact'
    compact.mkdir(parents=True)
    projects = ROOT.parent
    manifest = {p.relative_to(projects).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(paths)}
    omitted = []
    for path in sorted(paths):
        relative = path.relative_to(projects)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or path.suffix in ('.o', '.a', '.so', '.zst'):
            omitted.append(relative.as_posix())
            continue
        target = compact / 'records' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                        ('external-dependencies.json', dependencies)):
        (compact / name).write_text(json.dumps(value, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects),
                    *manifest, str(compact.relative_to(projects))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(projects).as_posix()]['sha256']:
            raise ValueError('input changed during collection')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    summary = dict(worker_exit_code=code, diagnostic_contracts_pass=(code == 0),
                   performance_qualified=False, application_qualified=False, release_qualified=False)
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
