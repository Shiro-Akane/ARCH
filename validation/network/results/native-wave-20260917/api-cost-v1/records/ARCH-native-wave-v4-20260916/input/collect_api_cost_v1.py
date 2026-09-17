"""Collect a finished API-cost diagnostic, never start/restart scientific work."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
sys.path.insert(0, str(ROOT / 'input' if (ROOT / 'input').is_dir() else Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(ROOT / 'api-cost-input-v1'))
from collect_factory import completion, inventory, sha, successful_guard
from run_focused_cost import observer_result
from run_trajectories import validate, METHODS


def main():
    control, out = ROOT / 'api-cost-control-v1', ROOT / 'api-cost-focused-v1'
    code = completion(control)
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'arch_cuda_generated_sparse_burn_audit150',
                 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('owned compute remains active or process check failed')
    path = out / 'record.json'
    record = json.loads(path.read_text()) if path.exists() else {}
    summary = dict(worker_exit_code=code, diagnostic_contracts_pass=False,
                   performance_qualified=False, application_qualified=False, release_qualified=False)
    if code == 0:
        result = validate('focused', record, out, 0, diagnostic=True)
        if (not result['diagnostic_numerical_pass'] or result['trajectory_matrix_pass']
                or result != record.get('numerical_validation') or record.get('identities_verified_after') is not True):
            raise ValueError('success without complete diagnostic-only numerical validation')
        for run in record['runs']:
            n, method, _ = run['name'].split('-')
            network = int(n[5:])
            actual = observer_result((out / (run['name'] + '.stderr')).read_text(), network, method)
            if actual != run['observations']:
                raise ValueError('observer transcript/result mismatch')
            command = next(row for row in record['commands'] if row['name'] == run['name'])
            if (command['command'][0] != str(ROOT / f'batch-launch-focused-v1/arch_cuda_generated_sparse_burn_audit{network}')
                    or command['environment'] != {'LD_PRELOAD': record['observer']['preload']}):
                raise ValueError('unqualified executable or observer environment')
        successful_guard(control / 'diagnostic-guard.log')
        summary.update(diagnostic_contracts_pass=True, diagnostic_numerical_pass=True,
                       completed_harnesses=result['completed_harnesses'], trajectory_matrix_pass=False)
    paths = {path for directory in (control, out, ROOT / 'api-cost-input-v1')
             for path in directory.rglob('*') if path.is_file()}
    for name, digest in {**record.get('inputs', {}), **record.get('artifacts', {})}.items():
        path = Path(name)
        if sha(path) != digest:
            raise ValueError('execution input/product changed: ' + name)
        paths.add(path)
    factory = ROOT / 'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(factory / (name + '.sha256'), ROOT / 'source')
        paths.add(factory / (name + '.sha256'))
    dependencies = {str(path): dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=digest)
                    for path, digest in inventory(factory / 'vendor.sha256', ROOT / 'source').items()}
    paths.update((ROOT / 'input/collect_factory.py', Path(__file__).resolve()))
    if any(path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(ROOT) for path in paths):
        raise ValueError('unsafe/missing archive input')
    evidence = ROOT / 'api-cost-evidence-v1'
    raw, packed, receipt = ROOT / 'api-cost-raw-v1.tar.zst', ROOT / 'api-cost-compact-v1.tar.zst', ROOT / 'api-cost-collection-v1.json'
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
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    collection = dict(**summary, raw=raw_record,
        compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
        embedded_compact=compact.relative_to(projects).as_posix())
    with receipt.open('x') as stream:
        json.dump(collection, stream, indent=2)
        stream.write('\n')
    print(json.dumps(collection, indent=2))


if __name__ == '__main__':
    main()
