#!/usr/bin/env python3
"""Read-only ARCH storage inventory; no deletion is performed by this script."""
import argparse
import collections
import datetime
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path('/home/ubuntu/projects')
OLD_ROOTS = [BASE / 'ARCH-cuda-v2-mainline-build', BASE / 'ARCH-cuda-v2-build']
PROTECTED_PARTS = {'.git', '_deps', '.venv', 'validation-python', 'network-python-20260909'}


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def classify(path, root):
    """Deliberately excludes executables, source, archives and dependencies."""
    relative = path.relative_to(root)
    if set(relative.parts) & PROTECTED_PARTS:
        return None
    if path.suffix in {'.o', '.gch'} and 'CMakeFiles' in relative.parts:
        category = 'rebuildable_cmake_object_or_pch'
    elif path.name.startswith('libarch_') and path.suffix == '.a':
        category = 'rebuildable_arch_static_archive'
    else:
        return None
    for parent in path.parents:
        if not parent.is_relative_to(root):
            break
        cache = parent / 'CMakeCache.txt'
        if cache.is_file() and not cache.is_symlink():
            content = cache.read_text(errors='replace')
            source = next((line.split('=', 1)[1] for line in content.splitlines()
                           if line.startswith('CMAKE_HOME_DIRECTORY:INTERNAL=')), None)
            if source and (Path(source) / 'CMakeLists.txt').is_file():
                return category, parent, source
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    save(args.out / 'filesystem-before.json', {
        'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'statvfs': dict(zip(['f_bsize', 'f_frsize', 'f_blocks', 'f_bfree', 'f_bavail',
                             'f_files', 'f_ffree', 'f_favail', 'f_flag', 'f_namemax'],
                            os.statvfs(BASE))),
    })
    du = subprocess.run(['du', '-x', '-B1', '--max-depth=1', str(BASE)],
                        capture_output=True, text=True, check=True)
    (args.out / 'projects-du-before.txt').write_text(du.stdout)
    roots = sorted(p for p in BASE.iterdir() if p.name.startswith('ARCH')
                   and p.is_dir() and not p.is_symlink())
    summary = collections.defaultdict(lambda: collections.Counter())
    candidates, caches, largest, survivors = [], {}, [], []
    cutoff = datetime.datetime(2026, 9, 10, tzinfo=datetime.timezone.utc).timestamp()
    with gzip.open(args.out / 'arch-files-before.jsonl.gz', 'wt') as manifest:
        for root in roots:
            for directory, dirs, files in os.walk(root, followlinks=False):
                dirs[:] = [d for d in dirs if not (Path(directory) / d).is_symlink()]
                for name in files:
                    p = Path(directory) / name
                    s = p.lstat()
                    if not stat.S_ISREG(s.st_mode):
                        continue
                    record = {'path': str(p), 'bytes': s.st_size, 'blocks': s.st_blocks,
                              'inode': s.st_ino, 'device': s.st_dev, 'nlink': s.st_nlink,
                              'mtime_ns': s.st_mtime_ns}
                    manifest.write(json.dumps(record) + '\n')
                    summary[root.name]['files'] += 1
                    summary[root.name]['logical_bytes'] += s.st_size
                    summary[root.name]['allocated_bytes'] += s.st_blocks * 512
                    if s.st_size > 100 * 1024 * 1024:
                        largest.append(record)
                    if root not in OLD_ROOTS:
                        continue
                    selection = classify(p, root)
                    if selection and s.st_nlink == 1 and s.st_mtime < cutoff and p.resolve() == p:
                        category, build, source = selection
                        record.update(category=category, build_root=str(build), source_root=source)
                        candidates.append(record)
                        caches[str(build)] = source
                        summary[root.name]['candidate_bytes'] += s.st_size
                        summary[root.name]['candidate_allocated_bytes'] += s.st_blocks * 512
                    else:
                        survivors.append(record)
    print('Metadata scan complete; hashing selected intermediate artifacts.', flush=True)
    for i, record in enumerate(candidates):
        record['sha256'] = digest(record['path'])
        if i % 500 == 0:
            print(f'Hashed {i}/{len(candidates)} candidates', flush=True)
    save(args.out / 'candidates.json', candidates)
    save(args.out / 'build-roots.json', caches)
    save(args.out / 'summary.json', dict(summary))
    save(args.out / 'largest-files.json', sorted(largest, key=lambda x: x['bytes'], reverse=True))
    with gzip.open(args.out / 'old-roots-retained-files.jsonl.gz', 'wt') as stream:
        for record in survivors:
            stream.write(json.dumps(record) + '\n')
    print(json.dumps({'candidates': len(candidates),
                      'candidate_bytes': sum(r['bytes'] for r in candidates),
                      'build_roots': len(caches)}, indent=2), flush=True)


if __name__ == '__main__':
    main()
