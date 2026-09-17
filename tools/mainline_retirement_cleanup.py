#!/usr/bin/env python3
"""Retire the exact inventoried old root only after published preservation.

Default is dry-run. Never follows links, never stops a process, never removes a
path outside OLD. A tiny compatibility scaffold for pinned HighFive remains.
"""
import argparse
import datetime
import fcntl
import gzip
import json
import os
from pathlib import Path
import stat
import subprocess

from legacy_archive_pack import file_sha
from mainline_retirement_inventory import OLD, OUT, HIGHFIVE, BASE, save
from mainline_dependency_migrate import DEST, manifest
from server_storage_cleanup import active_references


def load_entries():
    with gzip.open(OUT / 'inventory.jsonl.gz', 'rt') as stream:
        return [json.loads(line) for line in stream]


def validate(e, removed_hardlinks=None):
    assert not Path(e['path']).is_absolute() and '..' not in Path(e['path']).parts
    p = OLD / e['path']
    assert p.is_relative_to(OLD) and p.resolve(strict=True) == p and not p.is_symlink(), p
    s = p.lstat()
    assert stat.S_ISREG(s.st_mode) and s.st_dev == OLD.stat().st_dev, p
    links = e['nlink'] - (removed_hardlinks or {}).get((e['device'], e['inode']), 0)
    assert (s.st_size, s.st_ino, s.st_dev, s.st_mtime_ns, s.st_nlink) == (
        e['bytes'], e['inode'], e['device'], e['mtime_ns'], links), p


def processes():
    result = active_references([{'path': str(OLD), 'build_root': str(OLD)}])
    assert not result['matching_references'] and not result['unsafe_visibility'], result
    return result


def preflight(commit):
    assert OLD == Path('/home/ubuntu/projects/ARCH-cuda-v2-mainline-build')
    assert OLD.resolve() == OLD and not OLD.is_symlink() and not os.path.ismount(OLD)
    receipt = json.loads((OUT / 'published-backup-receipt.json').read_text())
    assert receipt['commit'] == commit and receipt['local_fetch_and_pack_verification_passed'] is True
    remote = subprocess.check_output(['git', 'ls-remote', 'https://github.com/Arsenic-er/ARCH.git',
                                      'refs/heads/main'], text=True).split()[0]
    assert remote == commit, 'Remote main changed; review new history before deleting'
    package = OUT / 'personal-supplement-v1'
    verify = json.loads((package / 'verification.json').read_text())
    assert verify['all_passed'] and verify['supplement_manifest_sha256'] == file_sha(package / 'files.jsonl.gz')
    assert verify['volumes_manifest_sha256'] == file_sha(package / 'volumes.json')
    assert receipt['supplement_manifest_sha256'] == verify['supplement_manifest_sha256']
    for v in json.loads((package / 'volumes.json').read_text()):
        assert file_sha(package / v['path']) == v['sha256']
    dep = json.loads((OUT / 'dependency-migration.json').read_text())
    assert dep['compile_exit_code'] == 0 and manifest(HIGHFIVE) == manifest(DEST) == dep['manifest']
    for e in json.loads((OUT / 'private-quarantine-manifest.json').read_text()):
        assert file_sha(OUT / 'private-quarantine' / e['sha256']) == e['sha256']
    entries = load_entries()
    links = json.loads((OUT / 'links.json').read_text())
    expected = {str(OLD / e['path']) for e in entries} | {e['path'] for e in links}
    actual, directories = set(), []
    for directory, dirs, files in os.walk(OLD, followlinks=False):
        d = Path(directory)
        assert d.resolve() == d and not os.path.ismount(d) and d.stat().st_dev == OLD.stat().st_dev
        directories.append(d)
        for name in list(dirs):
            p = d / name
            if p.is_symlink():
                actual.add(str(p))
                dirs.remove(name)
        actual.update(str(d / name) for name in files)
    assert actual == expected, {'added': sorted(actual - expected)[:20], 'missing': sorted(expected - actual)[:20]}
    for e in entries:
        validate(e)
    for e in links:
        p = Path(e['path'])
        assert p.is_relative_to(OLD) and p.is_symlink() and os.readlink(p) == e['target']
    # Preserve active experiment trees and baseline files independently of old-root data.
    protected = json.loads((BASE / 'ARCH-storage-audit-20260917/protected-hashes.json').read_text())
    checked = []
    for e in protected:
        p = Path(e['path'])
        if p.is_relative_to(OLD):
            assert p.is_relative_to(HIGHFIVE), p
        assert file_sha(p) == e['sha256'], p
        checked.append(e)
    process_check = processes()
    save(OUT / 'retirement-preflight.json', {'published_commit': commit, 'files': len(entries),
         'links': len(links), 'directories': len(directories), 'protected_files': len(checked),
         'process_check': process_check, 'all_passed': True})
    return entries, links, directories, checked


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--published-commit', required=True)
    p.add_argument('--apply', action='store_true')
    args = p.parse_args()
    lock = (OUT / 'retirement.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    entries, links, directories, protected = preflight(args.published_commit)
    if not args.apply:
        print('Retirement preflight passed; no files deleted', flush=True)
        return
    journal_path = OUT / 'retirement-deletion-journal.jsonl'
    assert not journal_path.exists(), 'Previous deletion journal exists; inspect instead of retrying'
    before = os.statvfs(BASE)
    old_bytes = int(subprocess.check_output(['du', '-s', '-B1', str(OLD)], text=True).split()[0])
    removed_hardlinks = {}
    with journal_path.open('x') as journal:
        for i, e in enumerate(entries):
            if i % 20000 == 0:
                processes()
                print(f'Deleting checked file {i}/{len(entries)}', flush=True)
            validate(e, removed_hardlinks)
            (OLD / e['path']).unlink()
            if e['nlink'] > 1:
                key = e['device'], e['inode']
                removed_hardlinks[key] = removed_hardlinks.get(key, 0) + 1
            journal.write(json.dumps({'path': e['path'], 'bytes': e['bytes'], 'deleted': True}) + '\n')
            if i % 2000 == 0:
                journal.flush()
                os.fsync(journal.fileno())
        for e in links:
            path = Path(e['path'])
            assert path.is_symlink() and os.readlink(path) == e['target']
            path.unlink()
            journal.write(json.dumps({'path': str(path.relative_to(OLD)), 'link_deleted': True}) + '\n')
        journal.flush()
        os.fsync(journal.fileno())
    keep = {OLD, *HIGHFIVE.parent.parents}
    keep.add(HIGHFIVE.parent)
    for path in sorted(directories, key=lambda p: len(p.parts), reverse=True):
        if path not in keep:
            assert path.is_relative_to(OLD) and not path.is_symlink()
            path.rmdir()  # Empty only: unexpected new files prevent directory removal.
    assert not HIGHFIVE.exists()
    HIGHFIVE.symlink_to(DEST, target_is_directory=True)
    assert HIGHFIVE.resolve() == DEST and manifest(HIGHFIVE) == manifest(DEST)
    smoke = json.loads((OUT / 'dependency-migration.json').read_text())['compile_command']
    smoke = [x.replace(str(DEST), str(HIGHFIVE)) for x in smoke]
    subprocess.run(smoke, check=True)
    failures = [e['path'] for e in protected if file_sha(Path(e['path'])) != e['sha256']]
    assert not failures, failures
    os.sync()
    after = os.statvfs(BASE)
    remaining = int(subprocess.check_output(['du', '-s', '-B1', str(OLD)], text=True).split()[0])
    receipt = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'published_backup_commit': args.published_commit, 'removed_files': len(entries),
               'removed_links': len(links), 'old_allocated_bytes': old_bytes,
               'old_root_remaining_bytes': remaining, 'old_root_reduction_bytes': old_bytes - remaining,
               'disk_available_before_bytes': before.f_bavail * before.f_frsize,
               'disk_available_after_bytes': after.f_bavail * after.f_frsize,
               'protected_hash_count': len(protected), 'protected_hash_failures': failures,
               'compatibility_link': str(HIGHFIVE), 'dependency_target': str(DEST),
               'post_cleanup_include_compile_passed': True, 'no_processes_stopped': True}
    save(OUT / 'retirement-receipt.json', receipt)
    print(json.dumps(receipt, indent=2), flush=True)


if __name__ == '__main__':
    main()
