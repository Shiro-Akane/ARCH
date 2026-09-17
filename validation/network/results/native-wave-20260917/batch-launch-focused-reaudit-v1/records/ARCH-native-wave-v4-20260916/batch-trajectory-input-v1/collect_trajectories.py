"""Archive one finished isolated nuclear profile; never run an experiment."""
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
sys.path.insert(0, str(Path(__file__).resolve().parent))
from collect_factory import completion, inventory, sha, successful_guard
from run_trajectories import PROFILES, validate


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    args = parser.parse_args()
    name = 'batch-launch-' + args.profile
    control, output = ROOT / (name + '-control-v1'), ROOT / (name + '-v1')
    code = completion(control)
    for process in ('ARCH', 'nvcc', 'ptxas', 'cc1plus',
                    'arch_cuda_generated_sparse_burn_audit150', 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/' + re.escape(process) + r'( |$)'], capture_output=True).returncode != 1:
            raise RuntimeError('owned computation active or process check failed')
    record_path = output / 'record.json'
    record = json.loads(record_path.read_text()) if record_path.exists() else None
    qualification_path = output / 'qualification.json'
    qualification = json.loads(qualification_path.read_text()) if qualification_path.exists() else None
    summary = dict(profile=args.profile, worker_exit_code=code, trajectory_matrix_pass=False,
                   performance_qualified=False, application_qualified=False, release_qualified=False)
    if record is not None:
        try:
            result = validate(args.profile, record, output, code)
            summary.update(result)
        except Exception as error:
            if code == 0:
                raise
            summary['validation_error'] = repr(error)
    if code == 0:
        if (not summary['trajectory_matrix_pass'] or qualification is None
                or qualification.get('identities_verified_after') is not True
                or qualification.get('trajectory_matrix_pass') is not True
                or qualification.get('profile') != args.profile):
            raise ValueError('success without complete qualified physical trajectories')
        successful_guard(control / (args.profile + '-guard.log'))
    paths = {p for directory in (control, output, ROOT / 'batch-trajectory-input-v1')
             for p in directory.rglob('*') if p.is_file()}
    if qualification:
        for path, digest in qualification['inputs'].items():
            if sha(Path(path)) != digest:
                raise ValueError('trajectory input changed: ' + path)
            paths.add(Path(path))
    if record:
        for artifact in record['artifacts']:
            for label in ('factory', 'executable'):
                if label + '_path' in artifact:
                    path = Path(artifact[label + '_path'])
                    if sha(path) != artifact[label + '_sha256']:
                        raise ValueError('trajectory product changed: ' + str(path))
                    paths.add(path)
    factory, source = ROOT / 'factory-control-v2', ROOT / 'source'
    for manifest in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(factory / (manifest + '.sha256'), source)
        paths.add(factory / (manifest + '.sha256'))
    dependencies = {str(path): dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=digest)
                    for path, digest in inventory(factory / 'vendor.sha256', source).items()}
    build = ROOT / 'factory-release'
    paths.update(build / file for file in ('CMakeCache.txt', 'compile_commands.json', 'build.ninja'))
    paths.update(build / (f'CMakeFiles/arch_cuda_generated_sparse_burn_audit{n}.dir/tests/cuda/test_generated_sparse_burn_factory.cu.o')
                 for n in (150, 200))
    paths.update(ROOT / file for file in ('batch-launch-contract-v1/record.json',
        'batch-launch-contract-v1/libarch_cuda_sparse_provider.a', 'factory-focused-collection-v1.json',
        'batch-launch-collection-v1.json', 'batch-launch-local-receipt-v1.json',
        'input/run_sparse_capacity_v1.py', 'input/collect_factory.py',
        'source/tests/cuda/test_generated_sparse_burn.cpp'))
    parent = PROFILES[args.profile]['parent']
    paths.update(ROOT / (parent + suffix) for suffix in ('-collection-v1.json', '-local-receipt-v1.json'))
    paths.add(Path(__file__).resolve())
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(ROOT) for p in paths):
        raise ValueError('unsafe/missing archive input')
    evidence = ROOT / (name + '-evidence-v1')
    raw, packed = ROOT / (name + '-raw-v1.tar.zst'), ROOT / (name + '-compact-v1.tar.zst')
    receipt = ROOT / (name + '-collection-v1.json')
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
    for filename, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                            ('external-dependencies.json', dependencies)):
        (compact / filename).write_text(json.dumps(value, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects),
                    *manifest, str(compact.relative_to(projects))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(projects).as_posix()]['sha256']:
            raise ValueError('archive input changed while collecting')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
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
