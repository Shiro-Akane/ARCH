#!/usr/bin/env python3
"""Publish an already-verified archive from an isolated server checkout.

No production worktree is changed. A local index tree must match byte-for-byte
before committing. This utility does not push; normal non-force git pushes follow
only after the commit is also mirrored and checked on the local machine.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/home/ubuntu/projects/ARCH-archive-publication-20260917')
SOURCE = Path('/home/ubuntu/projects/ARCH-legacy-archive-20260917/shared-v1')
RECORDS = Path('validation/backend/results/legacy-archive-20260917')
BASE = '2e36b86cbd86122a110d026f7f64705f03b73a53'


def git(*args, **kwargs):
    return subprocess.check_output(['git', '-c', 'credential.helper=!gh auth git-credential',
                                    '-C', str(ROOT), *args], **kwargs)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--metadata-patch', type=Path, required=True)
    parser.add_argument('--patch-sha256', required=True)
    parser.add_argument('--expected-tree', required=True)
    parser.add_argument('--author-name', required=True)
    parser.add_argument('--author-email', required=True)
    args = parser.parse_args()
    assert ROOT.resolve() == ROOT and SOURCE.resolve() == SOURCE
    assert git('rev-parse', 'HEAD', text=True).strip() == BASE
    assert not git('status', '--porcelain=v1', text=True).strip()
    assert digest(args.metadata_patch) == args.patch_sha256
    # New archive/tool paths only. No existing source is patched.
    git('apply', '--index', '--whitespace=nowarn', str(args.metadata_patch))
    dest = ROOT / RECORDS / 'shared'
    assert dest.is_relative_to(ROOT) and not dest.exists()
    shutil.copytree(SOURCE, dest, symlinks=True)
    assert not any(p.is_symlink() for p in dest.rglob('*'))
    receipt = ROOT / RECORDS / 'shared-full-verification.json'
    assert json.loads(receipt.read_text())['all_passed'] is True
    shutil.copy2(receipt, dest / 'local-verification.json')
    for volume in json.loads((dest / 'volumes.json').read_text()):
        name = volume['path']
        assert Path(name).name == name
        p = dest / name
        assert p.stat().st_size == volume['bytes'] and digest(p) == volume['sha256']
    git('add', '--sparse', '--', str(RECORDS / 'shared'))
    tree = git('write-tree', text=True).strip()
    assert tree == args.expected_tree, ('Local/server index tree mismatch', tree)
    env = {**os.environ, 'GIT_AUTHOR_NAME': args.author_name, 'GIT_AUTHOR_EMAIL': args.author_email,
           'GIT_COMMITTER_NAME': args.author_name, 'GIT_COMMITTER_EMAIL': args.author_email}
    git('commit', '--no-gpg-sign', '-m',
        'archive: preserve legacy logs and data; retire superseded failed source', env=env)
    head = git('rev-parse', 'HEAD', text=True).strip()
    assert git('rev-parse', 'HEAD^', text=True).strip() == BASE
    result = {'head': head, 'parent': BASE, 'tree': tree,
              'commit_base64': base64.b64encode(git('cat-file', 'commit', head)).decode(),
              'pack_hashes_rechecked': True, 'local_server_tree_identical': True,
              'published': False}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
