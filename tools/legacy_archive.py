#!/usr/bin/env python3
"""Inventory legacy ARCH working copies without modifying or deleting them."""
import argparse
import collections
import gzip
import json
import os
from pathlib import Path
import stat
import subprocess

BASE = Path('/home/ubuntu/projects')
SKIP_DIRS = {'.git', '_deps', '.venv', '__pycache__', 'node_modules', '.cache'}
LEGACY_NAMES = {
    'ARCH', 'ARCH-runs', 'ARCH-main-baseline', 'ARCH-hydro-policy-test', 'ARCH-v2-verify2',
    'ARCH-ns-diffusion-sandbox', 'ARCH-latest-diffusion-redo', 'ARCH-v2-latest-diffusion-redo',
    'ARCH-main-cuda-redo-983e04e', 'ARCH-c8-task5-golden',
    'ARCH-personal-gpu-amr-integration-20260903',
}
ARCHIVE_SUFFIXES = ('.tar', '.tar.gz', '.tar.zst', '.tgz', '.zip')


def save(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8', newline='\n')


def reason(path):
    parts = set(path.parts)
    if parts & SKIP_DIRS:
        return 'third_party_or_vcs_storage'
    if 'CMakeFiles' in parts and path.name not in {'CMakeError.log', 'CMakeOutput.log', 'CMakeConfigureLog.yaml'}:
        return 'generated_cmake_internals_already_archived_separately'
    if path.name.endswith(ARCHIVE_SUFFIXES):
        return 'container_to_inspect'
    if path.suffix == '.bundle':
        return 'git_history_bundle'
    if path.suffix in {'.o', '.obj', '.a', '.gch', '.pyc', '.pdb'} or '.so' in path.suffixes:
        return 'compiled_artifact'
    if path.name in {'.env', 'id_rsa', 'id_ed25519', 'credentials', 'credentials.json'} or path.suffix in {'.key', '.pem', '.p12'}:
        return 'sensitive_name_review'
    return None


def discover(out):
    out.mkdir(parents=True, exist_ok=False)
    roots = sorted(p for p in BASE.iterdir() if not p.is_symlink() and p.is_dir()
                   and (p.name in LEGACY_NAMES or p.name.startswith('ARCH-cuda-v2')))
    counts = collections.defaultdict(lambda: collections.Counter())
    repositories, candidates, containers, links = [], [], [], []
    with gzip.open(out / 'inventory.jsonl.gz', 'wt', encoding='utf-8') as inventory:
        for root in roots:
            for directory, dirs, files in os.walk(root, followlinks=False):
                directory = Path(directory)
                if '.git' in dirs or '.git' in files:
                    result = subprocess.run(['git', '-C', str(directory), 'rev-parse', '--verify', 'HEAD'],
                                            text=True, capture_output=True, env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'})
                    repositories.append({'path': str(directory), 'head': result.stdout.strip(), 'exit_code': result.returncode})
                for d in list(dirs):
                    p = directory / d
                    if d in SKIP_DIRS or p.is_symlink():
                        dirs.remove(d)
                        if p.is_symlink():
                            links.append({'path': str(p), 'target': os.readlink(p)})
                for name in files:
                    p = directory / name
                    s = p.lstat()
                    if stat.S_ISLNK(s.st_mode):
                        links.append({'path': str(p), 'target': os.readlink(p)})
                        continue
                    if not stat.S_ISREG(s.st_mode):
                        continue
                    r = reason(p)
                    if r is None:
                        with p.open('rb') as stream:
                            if stream.read(4) == b'\x7fELF':
                                r = 'compiled_elf_or_core'
                    entry = {'path': str(p.relative_to(BASE)), 'bytes': s.st_size,
                             'mtime_ns': s.st_mtime_ns, 'mode': stat.S_IMODE(s.st_mode),
                             'reason': r or 'source_log_or_data'}
                    inventory.write(json.dumps(entry) + '\n')
                    counts[r or 'source_log_or_data']['files'] += 1
                    counts[r or 'source_log_or_data']['bytes'] += s.st_size
                    if r is None:
                        candidates.append(entry)
                    elif r in {'container_to_inspect', 'git_history_bundle', 'sensitive_name_review'}:
                        containers.append(entry)
    save(out / 'candidates.json', candidates)
    save(out / 'containers-and-review.json', containers)
    save(out / 'repositories.json', repositories)
    save(out / 'symlinks.json', links)
    save(out / 'discovery-summary.json', {'roots': [str(p) for p in roots], 'categories': dict(counts)})
    print(json.dumps(dict(counts), indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    discover(args.out)
