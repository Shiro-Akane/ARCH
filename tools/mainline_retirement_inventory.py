#!/usr/bin/env python3
"""Read-only inventory for retirement of one explicitly named old build tree."""
import argparse
import collections
import gzip
import json
import os
from pathlib import Path
import re
import stat
import subprocess

from server_storage_cleanup import active_references

BASE = Path('/home/ubuntu/projects')
OLD = BASE / 'ARCH-cuda-v2-mainline-build'
HIGHFIVE = OLD / 'task-buildopt-20260901T/build-sm90/_deps/highfive-src'
OUT = BASE / 'ARCH-mainline-retirement-20260918'
ARCHIVES = ('.tar', '.tar.gz', '.tgz', '.tar.zst', '.zip', '.bundle')
TEXT_SUFFIXES = {'.txt', '.json', '.py', '.sh', '.ps1', '.cmake', '.ninja', '.git',
                 '.h', '.hpp', '.cuh', '.cu', '.cpp', '.yaml', '.yml', '.par'}


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def category(p):
    parts = set(p.relative_to(OLD).parts)
    if p.is_relative_to(HIGHFIVE):
        return 'live_highfive'
    if '.git' in parts:
        return 'git_storage'
    if parts & {'_deps', '.cache', '.venv', 'node_modules', '__pycache__'}:
        return 'vendor_or_cache'
    if p.name.endswith(ARCHIVES):
        return 'archive_container'
    if p.suffix in {'.o', '.obj', '.a', '.gch', '.pyc', '.so'}:
        return 'compiled_artifact'
    with p.open('rb') as stream:
        if stream.read(4) == b'\x7fELF':
            return 'compiled_elf'
    return 'material'


def inventory():
    assert OLD.resolve() == OLD and OLD.is_dir()
    OUT.mkdir(exist_ok=False)
    counts = collections.defaultdict(collections.Counter)
    links, repositories, special = [], [], []
    with gzip.open(OUT / 'inventory.jsonl.gz', 'wt') as stream:
        for directory, dirs, files in os.walk(OLD, followlinks=False):
            directory = Path(directory)
            if (directory / '.git').exists() and '.git' not in directory.parts:
                r = subprocess.run(['git', '-C', str(directory), 'rev-parse', '--verify', 'HEAD'],
                                   capture_output=True, text=True, env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'})
                repositories.append({'path': str(directory), 'head': r.stdout.strip(), 'status': r.returncode,
                                     'dependency': '_deps' in directory.parts or directory.is_relative_to(HIGHFIVE)})
            for name in list(dirs):
                p = directory / name
                if p.is_symlink():
                    links.append({'path': str(p), 'target': os.readlink(p)})
                    dirs.remove(name)
            for name in files:
                p = directory / name
                s = p.lstat()
                if stat.S_ISLNK(s.st_mode):
                    links.append({'path': str(p), 'target': os.readlink(p)})
                    continue
                if not stat.S_ISREG(s.st_mode):
                    special.append(str(p))
                    continue
                cat = category(p)
                e = {'path': str(p.relative_to(OLD)), 'bytes': s.st_size, 'blocks': s.st_blocks,
                     'inode': s.st_ino, 'device': s.st_dev, 'mtime_ns': s.st_mtime_ns,
                     'nlink': s.st_nlink, 'mode': stat.S_IMODE(s.st_mode), 'category': cat}
                stream.write(json.dumps(e) + '\n')
                counts[cat]['files'] += 1
                counts[cat]['bytes'] += s.st_size
                counts[cat]['allocated_bytes'] += s.st_blocks * 512
    save(OUT / 'categories.json', counts)
    save(OUT / 'links.json', links)
    save(OUT / 'repositories.json', repositories)
    save(OUT / 'special-files.json', special)
    save(OUT / 'process-check.json', active_references([{'path': str(OLD), 'build_root': str(OLD)}]))
    print(json.dumps(counts, indent=2), flush=True)


def references():
    results, links, errors = [], [], []
    needle = str(OLD).encode()
    # Scan ARCH siblings only; do not inspect unrelated users/projects.
    for root in sorted(BASE.glob('ARCH*')):
        if root in {OLD, OUT} or not root.is_dir() or root.is_symlink():
            continue
        for directory, dirs, files in os.walk(root, followlinks=False):
            directory = Path(directory)
            for name in list(dirs):
                p = directory / name
                if p.is_symlink():
                    target = str(p.resolve())
                    if target == str(OLD) or target.startswith(str(OLD) + '/'):
                        links.append({'path': str(p), 'target': target})
                    dirs.remove(name)
                elif name in {'.git', '_deps', '.venv', '__pycache__', 'node_modules'}:
                    dirs.remove(name)
            for name in files:
                p = directory / name
                try:
                    if p.is_symlink():
                        target = str(p.resolve())
                        if target == str(OLD) or target.startswith(str(OLD) + '/'):
                            links.append({'path': str(p), 'target': target})
                        continue
                    if p.suffix not in TEXT_SUFFIXES and name not in {'.git', 'Makefile', 'CMakeLists.txt'}:
                        continue
                    if p.stat().st_size > 32 * 1024 * 1024:
                        continue
                    data = p.read_bytes()
                    if needle not in data:
                        continue
                    paths = sorted({x.decode(errors='replace') for x in re.findall(
                        re.escape(needle) + rb'''[^\s"'<>;,)\]}]*''', data)})
                    results.append({'file': str(p), 'references': paths})
                except (PermissionError, FileNotFoundError) as e:
                    errors.append({'path': str(p), 'error': str(e)})
    save(OUT / 'external-text-references.json', results)
    save(OUT / 'external-symlinks.json', links)
    save(OUT / 'reference-scan-errors.json', errors)
    targets = collections.Counter(t for e in results for t in e['references'])
    print(json.dumps({'files_with_references': len(results), 'symlinks': links, 'errors': errors,
                      'top_targets': targets.most_common(15)}, indent=2), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('mode', choices=['inventory', 'references'])
    args = p.parse_args()
    inventory() if args.mode == 'inventory' else references()
