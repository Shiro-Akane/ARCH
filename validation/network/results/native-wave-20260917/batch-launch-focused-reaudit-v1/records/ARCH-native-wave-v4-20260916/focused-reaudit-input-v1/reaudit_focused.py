"""Read-only scientific re-audit after the default-storage transcript parser bug.

Preserves original exit=1 and qualification=false. Does not rerun CUDA, change a
budget, edit a transcript or replace a prior receipt. Creates separate evidence.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
sys.path.insert(0, str(ROOT / 'input' if (ROOT / 'input').is_dir() else Path(__file__).resolve().parent.parent))
from collect_factory import completion, inventory, sha, successful_guard
from run_trajectories import validate


def main():
    control, output = ROOT / 'batch-launch-focused-control-v1', ROOT / 'batch-launch-focused-v1'
    parent = ROOT / 'batch-launch-focused-collection-v1.json'
    local = ROOT / 'batch-launch-focused-local-receipt-v1.json'
    parent_data, backup = json.loads(parent.read_text()), json.loads(local.read_text())
    if (completion(control) != 1 or parent_data.get('worker_exit_code') != 1
            or parent_data.get('validation_error') != "ValueError('actual trajectory controls/completion differ')"
            or parent_data['raw'] != backup['raw']
            or backup.get('status') != 'both_archives_and_all_members_byte_verified'):
        raise ValueError('exact preserved parser failure and verified backup required')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'arch_cuda_generated_sparse_burn_audit150',
                 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('owned compute active or process check failed')
    record_path, old_qualification_path = output / 'record.json', output / 'qualification.json'
    record, old = json.loads(record_path.read_text()), json.loads(old_qualification_path.read_text())
    if (old.get('worker_exit_code') != 0 or old.get('error') != "ValueError('actual trajectory controls/completion differ')"
            or old.get('trajectory_matrix_pass') is not False or old.get('identities_verified_after') is not False
            or record.get('status') != 'passed'):
        raise ValueError('only the known post-execution parser failure may be re-audited')
    source = ROOT / 'source/tests/cuda/test_generated_sparse_burn.cpp'
    if sha(source) != '551d021ac6ce26378dff6823e135a499cc9bbdc76276a7612a45ef21b5cab205':
        raise ValueError('frozen C++ transcript producer changed')
    # The fixed parser requires no storage_controls line only for the exact
    # C++ defaults; command args, per-step sizes and aggregate pool still match.
    result = validate('focused', record, output, old['worker_exit_code'])
    if not result['trajectory_matrix_pass'] or len(result['completed_harnesses']) != 6:
        raise ValueError('incomplete scientific evidence')
    successful_guard(control / 'focused-guard.log')
    for path, digest in old['inputs'].items():
        if sha(Path(path)) != digest:
            raise ValueError('original execution input changed: ' + path)
    for artifact in record['artifacts']:
        for label in ('factory', 'executable'):
            if sha(Path(artifact[label + '_path'])) != artifact[label + '_sha256']:
                raise ValueError('original execution product changed')
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        inventory(ROOT / 'factory-control-v2' / (name + '.sha256'), ROOT / 'source')
        if not (control / (name + '-check-after.log')).is_file():
            raise ValueError('original post-execution identity check missing')
    compact_parent = ROOT / 'batch-launch-focused-evidence-v1/compact'
    old_manifest = json.loads((compact_parent / 'raw-manifest.json').read_text())
    paths = set()
    for relative, item in old_manifest.items():
        path = ROOT.parent / relative
        if (path.is_symlink() or not path.resolve().is_relative_to(ROOT)
                or path.stat().st_size != item['bytes'] or sha(path) != item['sha256']):
            raise ValueError('archived original member changed: ' + relative)
        paths.add(path)
    for kind in ('raw', 'compact'):
        archived = Path(parent_data[kind]['path'])
        if archived.stat().st_size != parent_data[kind]['bytes'] or sha(archived) != parent_data[kind]['sha256']:
            raise ValueError('original archive changed')
    new_dir = ROOT / 'focused-reaudit-v1'
    evidence = ROOT / 'batch-launch-focused-reaudit-evidence-v1'
    raw, packed = ROOT / 'batch-launch-focused-reaudit-raw-v1.tar.zst', ROOT / 'batch-launch-focused-reaudit-compact-v1.tar.zst'
    receipt = ROOT / 'batch-launch-focused-reaudit-collection-v1.json'
    if any(path.exists() for path in (new_dir, evidence, raw, packed, receipt)):
        raise ValueError('existing/partial re-audit must be preserved')
    inputs = dict(old['inputs'])
    for path in (parent, local, record_path, old_qualification_path, Path(__file__),
                 Path(__file__).with_name('run_trajectories.py')):
        inputs[str(path)] = sha(path)
        paths.add(path)
    new_dir.mkdir()
    qualification = dict(**result, identities_verified_after=True, inputs=inputs,
        original_outer_exit_code=1, original_scientific_process_exit_code=0,
        scope='read-only-revalidation-of-complete-original-trajectories-no-new-GPU-run',
        original_failure_preserved=True, parser_fix='exact-default-storage-line-omission-only')
    qualification_path = new_dir / 'qualification.json'
    qualification_path.write_text(json.dumps(qualification, indent=2) + '\n')
    paths.add(qualification_path)
    compact = evidence / 'compact'
    compact.mkdir(parents=True)
    manifest = {p.relative_to(ROOT.parent).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(paths)}
    omitted = []
    for path in sorted(paths):
        relative = path.relative_to(ROOT.parent)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or path.suffix in ('.o', '.a', '.so', '.zst'):
            omitted.append(relative.as_posix())
            continue
        target = compact / 'records' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                        ('external-dependencies.json', json.loads((compact_parent / 'external-dependencies.json').read_text()))):
        (compact / name).write_text(json.dumps(value, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(ROOT.parent),
                    *manifest, str(compact.relative_to(ROOT.parent))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(ROOT.parent).as_posix()]['sha256']:
            raise ValueError('input changed during read-only re-audit')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    summary = dict(**result, original_outer_exit_code=1, original_scientific_process_exit_code=0,
                   revalidation_pass=True, new_gpu_runs=0, original_failure_preserved=True)
    (compact / 'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    collection = dict(**summary, raw=raw_record,
        compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
        embedded_compact=compact.relative_to(ROOT.parent).as_posix())
    with receipt.open('x') as stream:
        json.dump(collection, stream, indent=2)
        stream.write('\n')
    print(json.dumps(collection, indent=2))


if __name__ == '__main__':
    main()
