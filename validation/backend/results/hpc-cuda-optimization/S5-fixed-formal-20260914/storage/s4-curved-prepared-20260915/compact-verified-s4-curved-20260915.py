"""Reclaim only archived, completed S4 curved HDF expanded copies.

Run between formal phases, never while any ARCH/build/sparse harness is active.
Both canonical archives and all logs/source/binaries remain untouched. A fresh
receipt records every removal, including partial failure. No scientific PASS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile


ROOT = Path('/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913')
PREFIX = 's4-validation-20260913/curved/'
ARCHIVE_NAME = 's4-validation-20260913-evidence.tar.zst'
SHA = 'ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493'
ARCHIVE_BYTES = 1236911980
PROOF_SHA = 'dd5f1a40d6add5097305f7b28e23055eb5b25abff2d9cee0a3d6a748be6c5640'
EXPECTED_FILES = 384
EXPECTED_BYTES = 5229992704


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(stream):
    h = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        h.update(chunk)
    return h.hexdigest()


def file_hash(path):
    with path.open('rb') as stream:
        return digest(stream)


def allocated_size(path):
    return path.stat().st_blocks * 512


def quiescent():
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        require(subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 1,
                'Application/build active or process check failed; no cleanup')
    require(subprocess.run(['pgrep', '-f', '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_'],
                           stdout=subprocess.DEVNULL).returncode == 1,
            'Sparse harness active or process check failed; no cleanup')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--local-proof', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    quiescent()
    require(ROOT.is_dir() and not ROOT.is_symlink(), 'Exact S4 project root required')
    base = ROOT / 'build'
    scope = base / PREFIX
    archive = base / ARCHIVE_NAME
    output = base / 's4-curved-compaction-20260915-v1.json'
    temporary = output.with_suffix('.json.tmp')
    require(not output.exists() and not temporary.exists(), 'Fresh receipt required; inspect old attempts')
    require(scope.is_dir() and not scope.is_symlink() and scope.resolve().is_relative_to(ROOT.resolve()),
            'S4 curved scope missing, linked or outside project')
    require(not args.local_proof.is_symlink() and file_hash(args.local_proof) == PROOF_SHA,
            'Independent local proof identity mismatch')
    proof = json.loads(args.local_proof.read_text())
    require(proof['status'] == 'local_archive_member_proof_only' and proof['prefix'] == PREFIX,
            'Wrong proof scope/status')
    require(proof['archive']['sha256'] == SHA and proof['archive']['bytes'] == ARCHIVE_BYTES,
            'Wrong local archive identity')
    expected = proof['members']
    require(len(expected) == EXPECTED_FILES and sum(v['bytes'] for v in expected.values()) == EXPECTED_BYTES,
            'Incomplete local HDF proof')
    targets = {}
    for name, row in expected.items():
        relative = Path(name)
        require(name.startswith(PREFIX) and name.endswith('.h5') and not relative.is_absolute()
                and '..' not in relative.parts and '\\' not in name, 'Unsafe member path')
        path = base / relative
        require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(scope.resolve()),
                'Missing, linked or out-of-scope HDF')
        targets[name] = path
    live = {p for p in scope.rglob('*.h5') if p.is_file()}
    require(live == set(targets.values()), 'Live HDF inventory differs; preserve and investigate')
    require(archive.is_file() and not archive.is_symlink() and archive.stat().st_size == ARCHIVE_BYTES
            and file_hash(archive) == SHA, 'Canonical server archive mismatch')
    verified = {}
    with subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE) as process:
        with tarfile.open(fileobj=process.stdout, mode='r|') as stream:
            for member in stream:
                if member.name not in targets:
                    continue
                require(member.isfile() and member.name not in verified, 'Duplicate or nonregular member')
                row = expected[member.name]
                path = targets[member.name]
                require(member.size == row['bytes'] == path.stat().st_size, 'HDF size differs')
                data = stream.extractfile(member)
                require(data is not None and digest(data) == row['sha256'] == file_hash(path),
                        'Local tar, server tar and live HDF hashes differ')
                verified[member.name] = row
        require(process.wait() == 0, 'Canonical archive decoder failed')
    require(verified.keys() == expected.keys() and file_hash(archive) == SHA, 'Incomplete or changed archive')
    quiescent()
    report = dict(status='verified_only', scope=str(scope), archive=str(archive), archive_sha256=SHA,
        local_archive=proof['archive']['path'], local_archive_sha256=SHA,
        local_proof_sha256=PROOF_SHA, verified=verified,
        logical_bytes=EXPECTED_BYTES, files=EXPECTED_FILES,
        allocated_bytes_before=sum(allocated_size(p) for p in targets.values()),
        free_bytes_before=shutil.disk_usage(base).free, removed=[], pending_removal=None, applied=False,
        original_archives_retained=True, source_binaries_logs_retained=True, scientific_validation=False)

    def save(initial=False):
        destination = output if initial else temporary
        with destination.open('x', encoding='utf-8', newline='\n') as handle:
            handle.write(json.dumps(report, indent=2) + '\n')
        if not initial:
            temporary.replace(output)

    save(initial=True)
    if args.apply:
        try:
            for name, path in targets.items():
                require(not path.is_symlink() and path.resolve().is_relative_to(scope.resolve())
                        and file_hash(path) == verified[name]['sha256'], 'HDF changed before removal')
                report.update(status='removing', pending_removal=name)
                save()
                path.unlink()
                report['removed'].append(name)
                report['pending_removal'] = None
                save()
            report.update(status='applied', applied=True, free_bytes_after=shutil.disk_usage(base).free)
            save()
        except BaseException as error:
            report.update(status='failed_after_partial_removal', error=repr(error))
            # Preserve the last durable receipt even if the filesystem stops accepting writes.
            if not temporary.exists():
                save()
            raise
    print(json.dumps({k: v for k, v in report.items() if k not in ('verified', 'removed')}, indent=2))
    print('VERIFIED_S4_CURVED_COMPACTION_COMPLETE_NOT_SCIENCE_PASS', len(report['removed']))


if __name__ == '__main__':
    main()
