#!/usr/bin/env python3
"""Retire only the explicitly reviewed, superseded Task-G red C++ snapshot.

Do not back up its source payload. Retain data, logs, scripts, history and all other trees.
"""
import argparse
import json
import os
from pathlib import Path
import stat

from server_storage_audit import digest, save
from server_storage_cleanup import active_references

BASE = Path('/home/ubuntu/projects/ARCH-cuda-v2-mainline-build/task-stage1-20260827T')
ROOT = BASE / 'taskg-red-source'
CODE_SUFFIXES = {'.h', '.hpp', '.hh', '.hxx', '.c', '.cc', '.cpp', '.cxx', '.cu', '.cuh'}


def selected(path):
    rel = path.relative_to(ROOT)
    return ((rel.parts[0] in {'src', 'tests', 'simulation'} and path.suffix in CODE_SUFFIXES) or
            rel.as_posix() in {'CMakeLists.txt', 'CMakePresets.json'})


def entry(p):
    s = p.lstat()
    assert stat.S_ISREG(s.st_mode) and p.resolve() == p
    return {'path': str(p), 'bytes': s.st_size, 'mtime_ns': s.st_mtime_ns,
            'inode': s.st_ino, 'nlink': s.st_nlink, 'sha256': digest(p), 'build_root': str(ROOT)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    assert ROOT.resolve() == ROOT and ROOT.is_dir() and not (ROOT / '.git').exists()
    failure = BASE / 'logs/taskg-formal-red.stderr'
    successor = BASE / 'logs/taskgh-cpu-ctest.stdout'
    assert 'Cannot find source file' in failure.read_text()
    assert '100% tests passed out of 14' in successor.read_text()
    if not args.apply:
        args.out.mkdir(parents=True, exist_ok=False)
        remove, retain = [], []
        for p in sorted(ROOT.rglob('*')):
            if p.is_symlink():
                raise RuntimeError(f'Symlink requires separate review: {p}')
            if p.is_file():
                (remove if selected(p) else retain).append(entry(p))
        for e in remove:
            assert e['nlink'] == 1, e['path']
        save(args.out / 'source-delete-manifest.json', remove)
        save(args.out / 'retained-manifest.json', retain)
        save(args.out / 'decision.json', {
            'source_root': str(ROOT), 'decision': 'discard superseded, failed Task-G red C++ snapshot',
            'failure': entry(failure), 'successor_evidence': entry(successor),
            'delete_count': len(remove), 'delete_bytes': sum(e['bytes'] for e in remove),
            'source_payload_backed_up': False, 'log_data_and_scripts_retained': True,
            'not_applied_to_mutation_tests': 'Expected-negative tests are not failed experiments.',
        })
        print(f"Prepared {len(remove)} source/config files, {sum(e['bytes'] for e in remove)} bytes")
        return
    remove = json.loads((args.out / 'source-delete-manifest.json').read_text())
    retain = json.loads((args.out / 'retained-manifest.json').read_text())
    assert not (args.out / 'source-deletion-journal.jsonl').exists()
    for e in remove + retain:
        assert entry(Path(e['path'])) == e, e['path']
    live = active_references(remove)
    save(args.out / 'process-check.json', live)
    assert not live['matching_references'] and not live['unsafe_visibility']
    with (args.out / 'source-deletion-journal.jsonl').open('x') as stream:
        for e in remove:
            p = Path(e['path'])
            assert p.is_relative_to(ROOT) and selected(p) and entry(p) == e and e['nlink'] == 1
            p.unlink()
            stream.write(json.dumps({'path': str(p), 'sha256': e['sha256'], 'deleted': True}) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
    for e in retain:
        assert entry(Path(e['path'])) == e, e['path']
    assert not any((ROOT / d).exists() and any(selected(p) for p in (ROOT / d).rglob('*') if p.is_file())
                   for d in ['src', 'tests', 'simulation'])
    result = {'deleted_files': len(remove), 'deleted_bytes': sum(e['bytes'] for e in remove),
              'retained_files_verified': len(retain), 'source_payload_uploaded': False,
              'no_recursive_deletion': True, 'no_processes_stopped': True,
              'failure_log_sha256': digest(failure), 'successor_log_sha256': digest(successor)}
    save(args.out / 'source-cleanup-receipt.json', result)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
