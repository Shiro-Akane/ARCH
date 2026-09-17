#!/usr/bin/env python3
"""Exact-file cleanup gated by immutable inventory, local backup and publication.

No recursive removal, path glob removal, git cleanup, process termination or source edits.
The unlinked compiler artifacts are rebuildable, NOT byte-for-byte backed up.
"""
import argparse
import datetime
import fcntl
import gzip
import json
import os
from pathlib import Path
import re
import stat
import subprocess

from server_storage_audit import BASE, OLD_ROOTS, classify, digest, save
from server_storage_prepare import S4, ZSTD_SHA


def validate_entry(entry):
    p = Path(entry['path'])
    if not p.is_absolute() or p.resolve(strict=True) != p:
        raise ValueError(f'Noncanonical or symlink path: {p}')
    s = p.lstat()
    if not stat.S_ISREG(s.st_mode) or s.st_nlink != 1 or os.path.ismount(p):
        raise ValueError(f'Not an exclusive regular file: {p}')
    for name, value in [('bytes', s.st_size), ('inode', s.st_ino), ('device', s.st_dev),
                        ('mtime_ns', s.st_mtime_ns), ('nlink', s.st_nlink)]:
        if entry[name] != value:
            raise ValueError(f'Changed since inventory ({name}): {p}')
    root = next((r for r in OLD_ROOTS if p.is_relative_to(r)), None)
    if root:
        selected = classify(p, root)
        cutoff = datetime.datetime(2026, 9, 10, tzinfo=datetime.timezone.utc).timestamp()
        if not selected or selected[0] != entry['category'] or s.st_mtime >= cutoff:
            raise ValueError(f'Not an obsolete compiler intermediate: {p}')
        if str(selected[1]) != entry['build_root'] or str(selected[2]) != entry['source_root']:
            raise ValueError(f'Build/source mapping changed: {p}')
    elif p == S4 / 's4-validation-20260913-evidence.tar.gz':
        if entry['category'] != 'identical_decoded_archive':
            raise ValueError('Wrong duplicate archive category')
    elif p.parent == S4 / 's4-validation-20260913-transfer' and re.fullmatch(r'part-0[0-9]', p.name):
        if entry['category'] != 'verified_transfer_part':
            raise ValueError('Wrong transfer part category')
    else:
        raise ValueError(f'Outside exact cleanup scope: {p}')
    if s.st_dev != BASE.stat().st_dev:
        raise ValueError(f'Different filesystem: {p}')


def is_auth_identity(comm, cmdline):
    return bool((comm == '(sd-pam)' and cmdline.strip() == '(sd-pam)') or
                (comm == 'sshd' and re.fullmatch(r'sshd: ubuntu(?:@\S+)?', cmdline.strip())))


def active_references(entries):
    """Fail closed on visible matching cwd/exe/maps/fds; never stop a process."""
    paths = {e['path'] for e in entries}
    builds = {e['build_root'] for e in entries if 'build_root' in e}
    matches, inaccessible, unsafe_visibility = [], [], []
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit() or int(proc.name) == os.getpid():
            continue
        targets = []
        try:
            for field in ('cwd', 'exe'):
                try:
                    targets.append((field, os.readlink(proc / field)))
                except FileNotFoundError:
                    pass
            for fd in (proc / 'fd').iterdir():
                try:
                    targets.append(('fd', os.readlink(fd)))
                except FileNotFoundError:
                    pass
            try:
                for line in (proc / 'maps').read_text().splitlines():
                    fields = line.split(maxsplit=5)
                    if len(fields) == 6 and fields[-1].startswith('/'):
                        targets.append(('maps', fields[-1]))
            except FileNotFoundError:
                pass
        except PermissionError:
            inaccessible.append(int(proc.name))
            try:
                uid = proc.stat().st_uid
                comm = (proc / 'comm').read_text().strip()
                cmdline = (proc / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
                compiler = comm in {'ARCH', 'nvcc', 'cc1plus', 'cc1', 'ptxas', 'cicc',
                                    'ninja', 'make', 'g++', 'g++-11', 'g++-12', 'ld', 'ld.gold', 'cmake'}
                auth_service = is_auth_identity(comm, cmdline)
                if (uid == os.getuid() and not auth_service) or compiler or '/home/ubuntu/projects/ARCH' in cmdline:
                    unsafe_visibility.append({'pid': int(proc.name), 'uid': uid, 'comm': comm})
            except FileNotFoundError:
                pass
            except PermissionError:
                unsafe_visibility.append({'pid': int(proc.name), 'reason': 'identity unreadable'})
        except FileNotFoundError:
            continue
        for kind, target in targets:
            if target in paths or (target.startswith(str(BASE) + '/ARCH') and
                                   any(target == b or target.startswith(b + '/') for b in builds)):
                matches.append({'pid': int(proc.name), 'kind': kind, 'path': target})
    return {'matching_references': matches, 'inaccessible_pids': sorted(set(inaccessible)),
            'unsafe_visibility': unsafe_visibility,
            'visibility_note': 'Other-user fd/maps may be unreadable; their identity/command is checked. '
                               'No claim of privileged full-system fd visibility.'}


def check_tracked(entries):
    repos = {}
    for entry in entries:
        p = Path(entry['path'])
        for parent in p.parents:
            if (parent / '.git').exists():
                repos.setdefault(parent, []).append(str(p.relative_to(parent)))
                break
    for repo, names in repos.items():
        # Query once per repository, without refreshing or mutating its index.
        result = subprocess.run(['git', '-C', str(repo), 'ls-files', '-z'], check=True,
                                capture_output=True)
        tracked = set(result.stdout.decode().split('\0'))
        if tracked.intersection(names):
            raise ValueError(f'Git-tracked cleanup candidates: {repo}: {tracked.intersection(names)}')
    return len(repos)


def check_survivors(audit, removed):
    missing, changed = [], []
    with gzip.open(audit / 'arch-files-before.jsonl.gz', 'rt') as manifest:
        for line in manifest:
            old = json.loads(line)
            if old['path'] in removed:
                continue
            p = Path(old['path'])
            try:
                s = p.lstat()
            except FileNotFoundError:
                missing.append(old['path'])
                continue
            if old['bytes'] != s.st_size or old['mtime_ns'] != s.st_mtime_ns or old['inode'] != s.st_ino:
                changed.append(old['path'])
    return {'missing': missing, 'changed': changed}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audit', required=True, type=Path)
    parser.add_argument('--published-commit')
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if args.apply and not re.fullmatch('[0-9a-f]{40}', args.published_commit or ''):
        raise ValueError('Full published commit required')
    lock = (args.audit / 'cleanup.lock').open('a')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    entries = json.loads((args.audit / 'deletion-allowlist.json').read_text())
    assert len({e['path'] for e in entries}) == len(entries)
    preflight = json.loads((args.audit / 'preflight.json').read_text())
    receipt = json.loads((args.audit / 'local-backup-receipt.json').read_text())
    assert receipt['all_passed'] is True
    assert digest(args.audit / 'deletion-allowlist.json') == preflight['deletion_allowlist_sha256']
    assert receipt['deletion_allowlist_sha256'] == preflight['deletion_allowlist_sha256']
    assert digest(preflight['metadata_archive']['path']) == receipt['metadata_archive_sha256']
    assert receipt['metadata_archive_sha256'] == preflight['metadata_archive']['sha256']
    assert receipt['canonical_s4_sha256'] == ZSTD_SHA
    assert digest(S4 / 's4-validation-20260913-evidence.tar.zst') == ZSTD_SHA
    process_check = active_references(entries)
    save(args.audit / 'process-preflight.json', process_check)
    if process_check['matching_references'] or process_check['unsafe_visibility']:
        raise RuntimeError('Cannot prove no active references; see process-preflight.json')
    checked_repos = check_tracked(entries)
    for i, entry in enumerate(entries):
        validate_entry(entry)
        if digest(entry['path']) != entry['sha256']:
            raise ValueError(f'Content changed: {entry["path"]}')
        if i % 500 == 0:
            print(f'Preflight checked {i}/{len(entries)}', flush=True)
    protected = json.loads((args.audit / 'protected-hashes.json').read_text())
    for entry in protected:
        assert digest(entry['path']) == entry['sha256'], entry['path']
    if not args.apply:
        print(f'DRY RUN PASS: {len(entries)} exact files; {checked_repos} enclosing Git repositories')
        return
    if (args.audit / 'deletion-journal.jsonl').exists():
        raise RuntimeError('A prior deletion journal exists; review instead of retrying blindly')
    before = os.statvfs(BASE)
    with (args.audit / 'deletion-journal.jsonl').open('x') as journal:
        for i, entry in enumerate(entries):
            if i % 100 == 0:
                processes = active_references(entries)
                if processes['matching_references'] or processes['unsafe_visibility']:
                    raise RuntimeError('Process state changed; remaining files retained')
            validate_entry(entry)
            Path(entry['path']).unlink()  # Exact validated file only, never a directory.
            journal.write(json.dumps({'path': entry['path'], 'sha256': entry['sha256'],
                                      'bytes': entry['bytes'], 'deleted': True}) + '\n')
            journal.flush()
            os.fsync(journal.fileno())
    os.sync()
    after = os.statvfs(BASE)
    survivors = check_survivors(args.audit, {e['path'] for e in entries})
    protected_failures = [e['path'] for e in protected if digest(e['path']) != e['sha256']]
    receipt = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'published_preflight_commit': args.published_commit,
               'removed_files': len(entries), 'removed_logical_bytes': sum(e['bytes'] for e in entries),
               'removed_allocated_bytes': sum(e['blocks'] * 512 for e in entries),
               'available_before_bytes': before.f_bavail * before.f_frsize,
               'available_after_bytes': after.f_bavail * after.f_frsize,
               'retained_file_check': survivors, 'protected_hash_failures': protected_failures,
               'protected_hash_count': len(protected), 'checked_enclosing_git_repos': checked_repos,
               'no_recursive_deletion': True, 'no_processes_stopped': True}
    save(args.audit / 'cleanup-receipt.json', receipt)
    print(json.dumps(receipt, indent=2), flush=True)
    if survivors['missing'] or survivors['changed'] or protected_failures:
        raise RuntimeError('Post-cleanup survivor check needs review')


if __name__ == '__main__':
    main()
