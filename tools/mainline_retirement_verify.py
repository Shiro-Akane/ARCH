#!/usr/bin/env python3
"""Verify supplemental archive bytes and all referenced earlier evidence.

Only content-addressed chunks are extracted, into a new disposable directory.
Archive origins and link targets are never interpreted as extraction destinations.
"""
import argparse
import collections
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile

from legacy_archive_pack import CHUNK, file_sha, sha


def lines(path):
    with gzip.open(path, 'rt') as stream:
        for line in stream:
            yield json.loads(line)


class Reader:
    def __init__(self, package, prior, metadata, git_repos, temporary):
        self.cache = temporary
        self.git_repos = git_repos
        self.processes = {}
        self.available = {}
        self.prior = {}
        self.meta = {}
        self.pack_count = self.chunk_count = 0
        for name, root in [('new', package), *prior.items()]:
            self.load_chunks(root)
            if name != 'new':
                self.prior[name] = {e['path']: e for e in lines(root / 'files.jsonl.gz')}
        with tarfile.open(metadata, 'r|gz') as t:
            for member in t:
                if member.isfile():
                    data = t.extractfile(member).read()
                    self.meta[member.name] = (sha(data), len(data))

    def load_chunks(self, root):
        chunks = json.loads((root / 'chunks.json').read_text())
        volumes = json.loads((root / 'volumes.json').read_text())
        seen = set()
        for v in volumes:
            assert Path(v['path']).name == v['path']
            path = root / v['path']
            assert path.stat().st_size == v['bytes'] and file_sha(path) == v['sha256']
            with tarfile.open(path, 'r:gz') as t:
                for m in t:
                    assert m.isfile() and re.fullmatch('[0-9a-f]{64}', m.name)
                    assert m.name not in seen and m.name in chunks
                    c = chunks[m.name]
                    assert c['volume'] == v['path'] and c['bytes'] == m.size <= CHUNK
                    data = t.extractfile(m).read()
                    assert sha(data) == m.name
                    dest = self.cache / m.name
                    if not dest.exists():
                        with dest.open('xb') as f:
                            f.write(data)
                    seen.add(m.name)
            self.pack_count += 1
        assert seen == set(chunks)
        self.chunk_count += len(seen)

    def git_blob(self, key):
        assert re.fullmatch('[0-9a-f]{40}', key)
        for repo in self.git_repos:
            if repo not in self.available:
                # Avoid lazy-fetching from partial clones during a read-only audit.
                self.available[repo] = set(subprocess.check_output(
                    ['git', '-C', str(repo), 'cat-file', '--batch-all-objects',
                     '--batch-check=%(objectname)'], text=True).splitlines())
            if key not in self.available[repo]:
                continue
            if repo not in self.processes:
                self.processes[repo] = subprocess.Popen(['git', '-C', str(repo), 'cat-file', '--batch'],
                                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            p = self.processes[repo]
            p.stdin.write((key + '\n').encode())
            p.stdin.flush()
            h = p.stdout.readline().split()
            if len(h) == 2 and h[1] == b'missing':
                continue
            assert len(h) == 3 and h[1] == b'blob', h
            data = p.stdout.read(int(h[2]))
            assert p.stdout.read(1) == b'\n'
            assert hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() == key
            return data
        raise RuntimeError('Missing published Git blob: ' + key)

    def data(self, e):
        if 'prior_archive' in e:
            p = self.prior[e['prior_archive']][e['prior_path']]
            assert (p['sha256'], p['bytes']) == (e['sha256'], e['bytes'])
            return self.data(p)
        if 'git_blob' in e:
            return self.git_blob(e['git_blob'])
        return b''.join((self.cache / key).read_bytes() for key in e['chunks'])

    def close(self):
        for p in self.processes.values():
            p.stdin.close()
            assert p.wait(timeout=30) == 0
            p.stdout.close()


def verify(package, prior, metadata, repos, output):
    entries, verified, kinds = 0, {}, collections.Counter()
    with tempfile.TemporaryDirectory(prefix='retirement-verify-', dir=package.parent) as tmp:
        r = Reader(package, prior, metadata, repos, Path(tmp))
        try:
            for e in lines(package / 'files.jsonl.gz'):
                kinds[e['kind']] += 1
                if e['kind'] != 'file':
                    continue
                if e['sha256'] not in verified:
                    if 'rebuild_member' in e:
                        actual = r.meta[e['rebuild_member']]
                    else:
                        data = r.data(e)
                        actual = sha(data), len(data)
                        if 'git_oid' in e:
                            assert hashlib.sha1(e['git_type'].encode() + b' ' + str(len(data)).encode() + b'\0' + data).hexdigest() == e['git_oid']
                    verified[e['sha256']] = actual
                assert verified[e['sha256']] == (e['sha256'], e['bytes']), e['origin']
                entries += 1
            receipt = {'all_passed': True, 'files_verified': entries, 'unique_contents_verified': len(verified),
                       'kinds': kinds, 'packs_verified': r.pack_count, 'chunks_verified': r.chunk_count,
                       'supplement_manifest_sha256': file_sha(package / 'files.jsonl.gz'),
                       'volumes_manifest_sha256': file_sha(package / 'volumes.json')}
        finally:
            r.close()
    output.write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--package', required=True, type=Path)
    p.add_argument('--shared', required=True, type=Path)
    p.add_argument('--personal', required=True, type=Path)
    p.add_argument('--metadata', required=True, type=Path)
    p.add_argument('--git-repo', action='append', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    verify(a.package, {'shared': a.shared, 'personal': a.personal}, a.metadata, a.git_repo, a.output)
