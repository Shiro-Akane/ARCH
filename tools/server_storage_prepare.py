#!/usr/bin/env python3
"""Prepare an immutable cleanup allowlist and rebuild-metadata backup. No deletion."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile

from server_storage_audit import BASE, OLD_ROOTS, digest, save

S4 = BASE / 'ARCH-hpc-s4-validation-20260913/build'
ZSTD_SHA = 'ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493'
TAR_SHA = '4a96dd7befd9917e8f8ec56fc1664edcefdb772ac6c9e56ee3bd94e3c2a15cad'


def hash_stream(stream):
    h, size = hashlib.sha256(), 0
    for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
        size += len(chunk)
        h.update(chunk)
    return {'sha256': h.hexdigest(), 'bytes': size}


def record(path):
    s = path.lstat()
    assert path.is_file() and not path.is_symlink() and path.resolve() == path
    return {'path': str(path), 'bytes': s.st_size, 'blocks': s.st_blocks,
            'inode': s.st_ino, 'device': s.st_dev, 'nlink': s.st_nlink,
            'mtime_ns': s.st_mtime_ns, 'sha256': digest(path)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audit', required=True, type=Path)
    args = parser.parse_args()
    assert not (args.audit / 'preflight.json').exists()
    candidates = json.loads((args.audit / 'candidates.json').read_text())
    builds = json.loads((args.audit / 'build-roots.json').read_text())
    canonical = S4 / 's4-validation-20260913-evidence.tar.zst'
    redundant = S4 / 's4-validation-20260913-evidence.tar.gz'
    zstd_record = record(canonical)
    assert zstd_record['sha256'] == ZSTD_SHA
    print('Canonical archive compressed SHA verified; checking decoded streams.', flush=True)
    zstd = subprocess.Popen(['/usr/bin/zstd', '-q', '-d', '-c', str(canonical)], stdout=subprocess.PIPE)
    decoded_zstd = hash_stream(zstd.stdout)
    assert zstd.wait() == 0
    with gzip.open(redundant, 'rb') as stream:
        decoded_gzip = hash_stream(stream)
    assert decoded_zstd == decoded_gzip == {'sha256': TAR_SHA, 'bytes': 8018995200}
    parts = sorted((S4 / 's4-validation-20260913-transfer').glob('part-*'))
    assert [p.name for p in parts] == [f'part-{i:02d}' for i in range(10)]
    h, part_records = hashlib.sha256(), []
    for p in parts:
        with p.open('rb') as stream:
            for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
                h.update(chunk)
        part_records.append(record(p))
    assert h.hexdigest() == ZSTD_SHA
    duplicates = [dict(record(redundant), category='identical_decoded_archive',
                       retained_copy=str(canonical)),
                  *[dict(r, category='verified_transfer_part', retained_copy=str(canonical))
                    for r in part_records]]
    save(args.audit / 'duplicate-archive-verification.json', {
        'canonical': zstd_record, 'decoded_zstd': decoded_zstd, 'decoded_gzip': decoded_gzip,
        'parts_combined_sha256': h.hexdigest(), 'duplicates': duplicates,
    })
    save(args.audit / 'deletion-allowlist.json', candidates + duplicates)
    print('Duplicate archives verified; collecting rebuild metadata.', flush=True)
    metadata = set()
    for directory in builds:
        root = Path(directory)
        for p in root.rglob('*'):
            if not p.is_file() or p.is_symlink() or '_deps' in p.relative_to(root).parts:
                continue
            if p.suffix in {'.cmake', '.txt', '.json', '.make', '.log', '.yaml'} or p.name in {
                    'Makefile', 'build.ninja', '.ninja_log', '.ninja_deps'}:
                if p.stat().st_size < 32 * 1024 * 1024:
                    metadata.add(p)
    protected = set()
    # Pending window tree is never a cleanup root. Also preserve its external dependencies.
    window = BASE / 'ARCH-native-wave-v4-20260916'
    for p in window.rglob('*'):
        if p.is_file() and not p.is_symlink() and '.git' not in p.relative_to(window).parts:
            protected.add(p)
    highfive = OLD_ROOTS[0] / 'task-buildopt-20260901T/build-sm90/_deps/highfive-src'
    for p in highfive.rglob('*'):
        if p.is_file() and not p.is_symlink():
            protected.add(p)
    protected.update([canonical, BASE / '.envs/arch/bin/nvcc', BASE / '.envs/arch/bin/cmake',
        BASE / '.envs/arch/bin/ninja',
        BASE / 'ARCH-microphysics-20260914/build/p12-20260914/validation-python/pyvenv.cfg',
        BASE / 'ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cu12/lib/libcudss.so.0',
        BASE / 'ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas/lib/libcublas.so.12',
        BASE / 'ARCH-perf-20260909/build/network-python-20260909/lib/python3.11/site-packages/nvidia/cublas/lib/libcublasLt.so.12',
        BASE / 'ARCH-multiphysics-fix-20260914/build/fix-20260914/release/bin/ARCH',
        BASE / 'ARCH-main-merge-20260917/cpu-build-v1/bin/ARCH'])
    # Resolve and deduplicate dependency symlinks. No deletion targets can be symlinks.
    protected_records = [record(p) for p in sorted({p.resolve(strict=True) for p in protected})]
    save(args.audit / 'protected-hashes.json', protected_records)
    metadata_records = [record(p) for p in sorted(metadata)]
    save(args.audit / 'build-metadata-manifest.json', metadata_records)
    archive = args.audit / 'rebuild-metadata.tar.gz'
    with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
        for p in sorted(metadata):
            tar.add(p, arcname=str(p.relative_to(BASE)), recursive=False)
    build_repo_status = {}
    for source in sorted(set(builds.values())):
        p = Path(source)
        if not (p / '.git').exists():
            continue
        process = subprocess.run(['git', '-C', source, 'status', '--porcelain=v1', '--untracked-files=no'],
                                 capture_output=True, text=True)
        head = subprocess.run(['git', '-C', source, 'rev-parse', 'HEAD'], capture_output=True, text=True)
        build_repo_status[source] = {'exit_code': process.returncode, 'status': process.stdout,
                                     'stderr': process.stderr, 'head': head.stdout.strip()}
    save(args.audit / 'source-repo-status.json', build_repo_status)
    preflight = {'candidate_count': len(candidates), 'duplicate_count': len(duplicates),
                 'candidate_bytes': sum(r['bytes'] for r in candidates),
                 'duplicate_bytes': sum(r['bytes'] for r in duplicates),
                 'metadata_archive': record(archive), 'metadata_files': len(metadata_records),
                 'protected_hashed_files': len(protected_records),
                 'deletion_allowlist_sha256': digest(args.audit / 'deletion-allowlist.json')}
    save(args.audit / 'preflight.json', preflight)
    print(json.dumps(preflight, indent=2), flush=True)


if __name__ == '__main__':
    main()
