"""Archive a finished batch-kernel contract attempt, including failed builds.

No experiment is launched here. Source provenance has the previously archived
v4 factory/capacity parents; this increment retains all new inputs and products.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
HELPERS = ROOT / 'input' if (ROOT / 'input').is_dir() else Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HELPERS))
from collect_factory import completion, inventory, sha, successful_guard
KERNEL_COMBINATIONS = {(n, p, s, a) for n in (1, 151, 201, 513)
                       for p in (1, 2, 8, 32) for s in range(4) for a in (0, 1)}


def require_complete(record, output):
    expected = {'kernel-matrix'} | {f'provider-n{n}-c{p}' for n in (151, 201) for p in (1, 2, 8, 32)}
    if (record.get('status') != 'kernel-and-provider-contracts-passed'
            or record.get('identities_verified_after') is not True
            or set(record.get('tests', {})) != expected
            or any(test.get('passed') is not True for test in record['tests'].values())
            or record['tests']['kernel-matrix'].get('cases') != len(KERNEL_COMBINATIONS)
            or len(record.get('artifacts', {})) != 7):
        raise ValueError('worker success without all kernel/provider contract evidence')
    commands = {row['name']: row for row in record.get('commands', [])}
    if (len(commands) != len(record.get('commands', [])) or not expected <= set(commands)
            or any(row.get('status') != 'passed' or row.get('returncode') != 0 for row in commands.values())):
        raise ValueError('missing or failed execution commands')
    lines = (output / 'kernel-matrix.stdout').read_text().splitlines()
    expected_lines = {f'WAVE_KERNEL_BITWISE_PARITY_PASS extent={n} capacity={p} scenario={s} all_active={a}'
                      for n, p, s, a in KERNEL_COMBINATIONS}
    results = [line for line in lines if line.startswith('WAVE_KERNEL_BITWISE_PARITY_PASS ')]
    if len(results) != len(expected_lines) or set(results) != expected_lines:
        raise ValueError('missing/duplicate kernel execution combination')
    if not lines or lines[-1] != f'WAVE_KERNEL_MATRIX_PASS cases={len(KERNEL_COMBINATIONS)} scope=execution-equivalence-not-nuclear-or-performance':
        raise ValueError('missing complete kernel matrix marker')
    for n in (151, 201):
        for p in (1, 2, 8, 32):
            prefix = f'SPARSE_WAVE_CONTRACT_PASS extent={n} capacity={p} '
            if sum(line.startswith(prefix) for line in (output / f'provider-n{n}-c{p}.stdout').read_text().splitlines()) != 1:
                raise ValueError('missing/ambiguous provider completion')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', choices=['v1'], required=True)
    parser.parse_args()
    projects = ROOT.parent
    control, output = ROOT / 'batch-launch-control-v1', ROOT / 'batch-launch-contract-v1'
    code = completion(control)
    record = json.loads((output / 'record.json').read_text()) if (output / 'record.json').exists() else {}
    if code == 0:
        require_complete(record, output)
        successful_guard(control / 'contracts-guard.log')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'kernel-test', 'provider-test',
                 'arch_cuda_generated_sparse_burn_audit150', 'arch_cuda_generated_sparse_burn_audit200'):
        status = subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'], capture_output=True)
        if status.returncode != 1:
            raise RuntimeError('owned computation remains active or process check failed')
    paths = {p for directory in (control, output, ROOT / 'batch-launch-input-v1')
             for p in directory.rglob('*') if p.is_file()}
    for path, expected_hash in {**record.get('inputs', {}), **record.get('artifacts', {})}.items():
        p = Path(path)
        if sha(p) != expected_hash:
            raise ValueError('input/product changed since execution: ' + path)
        paths.add(p)
    factory = ROOT / 'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(factory / (name + '.sha256'), ROOT / 'source')
        paths.add(factory / (name + '.sha256'))
    dependencies = {str(p): dict(resolved=str(p.resolve()), bytes=p.stat().st_size, sha256=h)
                    for p, h in inventory(factory / 'vendor.sha256', ROOT / 'source').items()}
    paths.update(ROOT / name for name in ('capacity-collection-v2.json', 'capacity-local-receipt-v2.json',
                                         'factory-focused-collection-v1.json'))
    paths.update((Path(__file__).resolve(), ROOT / 'input/collect_factory.py'))
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(ROOT) for p in paths):
        raise ValueError('unsafe/missing archive input')
    evidence = ROOT / 'batch-launch-evidence-v1'
    raw, packed = ROOT / 'batch-launch-raw-v1.tar.zst', ROOT / 'batch-launch-compact-v1.tar.zst'
    receipt = ROOT / 'batch-launch-collection-v1.json'
    if any(p.exists() for p in (evidence, raw, packed, receipt)):
        raise ValueError('existing/partial archive outputs must be preserved')
    compact = evidence / 'compact'
    compact.mkdir(parents=True)
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
            raise ValueError('archive input changed while collecting')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    summary = dict(worker_exit_code=code, contracts_pass=(code == 0),
                   kernel_combinations=len(KERNEL_COMBINATIONS) if code == 0 else None,
                   nuclear_qualified=False, performance_qualified=False, release_qualified=False)
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    result = dict(**summary, raw=raw_record, compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
                  embedded_compact=compact.relative_to(projects).as_posix())
    with receipt.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
