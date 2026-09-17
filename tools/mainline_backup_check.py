#!/usr/bin/env python3
"""Check a separately fetched Git copy before permitting server retirement."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from legacy_archive_pack import file_sha


def check(repo, commit, output):
    prefix = 'archives/mainline-retirement-20260918/supplement/'

    def git(*args):
        return subprocess.check_output(['git', '-C', str(repo), *args])

    assert git('rev-parse', 'HEAD').decode().strip() == commit
    remote = git('ls-remote', 'personal', 'refs/heads/main').decode().split()[0]
    assert remote == commit
    raw = git('show', commit + ':' + prefix + 'volumes.json')
    volumes = json.loads(raw)
    verification = json.loads(git('show', commit + ':' + prefix + 'verification.json'))
    assert verification['all_passed']
    manifest_sha = file_sha(repo / prefix / 'files.jsonl.gz')
    assert manifest_sha == verification['supplement_manifest_sha256']
    assert hashlib.sha256(raw).hexdigest() == verification['volumes_manifest_sha256']
    for v in volumes:
        p = repo / prefix / v['path']
        assert Path(v['path']).name == v['path']
        assert p.stat().st_size == v['bytes'] and file_sha(p) == v['sha256']
        h = hashlib.sha1(b'blob ' + str(v['bytes']).encode() + b'\0')
        with p.open('rb') as stream:
            for block in iter(lambda: stream.read(4 * 1024**2), b''):
                h.update(block)
        assert h.hexdigest() == git('rev-parse', commit + ':' + prefix + v['path']).decode().strip()
    result = {'commit': commit, 'remote': 'https://github.com/Arsenic-er/ARCH.git',
              'local_fetch_and_pack_verification_passed': True,
              'supplement_manifest_sha256': manifest_sha,
              'volumes_manifest_sha256': hashlib.sha256(raw).hexdigest(),
              'pack_count': len(volumes), 'compressed_bytes': sum(v['bytes'] for v in volumes),
              'full_reconstruction_receipt_in_commit': prefix + 'verification.json'}
    output.write_text(json.dumps(result, indent=2) + '\n', newline='\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--repo', required=True, type=Path)
    p.add_argument('--commit', required=True)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    check(a.repo, a.commit, a.output)
