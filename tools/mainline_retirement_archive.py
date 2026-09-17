#!/usr/bin/env python3
"""Supplement old-root archives without extracting untrusted archive paths.

All new payload is personal-repository historical material, NOT accepted code.
Archives retain file contents/metadata, not byte-identical compression envelopes.
Published legacy payload and Git objects are referenced rather than uploaded twice.
No deletion. Sensitive material is held outside the retired root, never published.
"""
import argparse
import collections
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
import re

from legacy_archive_pack import CHUNK, Store, SECRET, file_sha, is_source_file, sha

BASE = Path('/home/ubuntu/projects')
OLD = BASE / 'ARCH-cuda-v2-mainline-build'
OUT = BASE / 'ARCH-mainline-retirement-20260918'
PACKAGE = OUT / 'personal-supplement-v1'
PREVIOUS = BASE / 'ARCH-legacy-archive-20260917'
ARCHIVES = ('.tar', '.tar.gz', '.tar.zst', '.tgz', '.zip')
SENSITIVE_PARTS = {'.ssh', '.aws', '.azure', '.codex', '.claude'}
SENSITIVE_NAMES = {'.env', 'id_rsa', 'id_ed25519', 'credentials', 'credentials.json'}
COMPILED = {'.o', '.obj', '.a', '.gch', '.pyc', '.pdb', '.cubin', '.fatbin', '.ptx'}


def save(path, data):
    path.write_text(json.dumps(data, indent=2) + '\n')


def safe_member(name):
    p = PurePosixPath(name)
    return not p.is_absolute() and '..' not in p.parts and '\\' not in name


def omitted_member(name, data):
    p = PurePosixPath(name)
    if 'taskg-red-source' in name and is_source_file(p, data):
        return 'previously_confirmed_failed_source'
    if set(p.parts) & {'_deps', '.cache', '.venv', 'node_modules', '__pycache__'}:
        return 'rebuildable_vendor_or_cache'
    if data.startswith(b'\x7fELF') or p.suffix in COMPILED or '.so' in p.suffixes:
        return 'rebuildable_compiled_artifact'
    return None


class Collector:
    def __init__(self):
        PACKAGE.mkdir(exist_ok=False)
        self.store = Store(PACKAGE, compressed_budget=2 * 1024**3)
        self.manifest = gzip.open(PACKAGE / 'files.jsonl.gz', 'wt')
        self.counts = collections.Counter()
        self.published = set(json.loads(Path('/tmp/mainline-published-git-index-20260918.json').read_text())['objects'])
        self.prior, self.seen, self.containers = {}, {}, {}
        self.held = []
        for name in ('shared', 'personal'):
            with gzip.open(PREVIOUS / (name + '-v1') / 'files.jsonl.gz', 'rt') as stream:
                for line in stream:
                    e = json.loads(line)
                    self.prior.setdefault(e['sha256'], {'prior_archive': name, 'prior_path': e['path']})
        metadata = json.loads((BASE / 'ARCH-storage-audit-20260917/build-metadata-manifest.json').read_text())
        for e in metadata:
            self.prior.setdefault(e['sha256'], {'rebuild_member': e['path'].removeprefix(str(BASE) + '/')})

    def emit(self, e):
        self.manifest.write(json.dumps(e) + '\n')
        self.counts[e['kind']] += 1

    def content(self, origin, data, meta=None, bypass_name=False):
        p = PurePosixPath(origin)
        digest = sha(data)
        e = {'origin': origin, 'bytes': len(data), 'sha256': digest, **(meta or {})}
        secret = SECRET.search(data)
        if secret or (not bypass_name and (set(p.parts) & SENSITIVE_PARTS or p.name in SENSITIVE_NAMES
                                          or p.suffix in {'.key', '.pem', '.p12'})):
            dest = OUT / 'private-quarantine' / digest
            dest.parent.mkdir(exist_ok=True, mode=0o700)
            if not dest.exists():
                with dest.open('xb') as stream:
                    stream.write(data)
                dest.chmod(0o600)
            assert file_sha(dest) == digest
            self.held.append({**e, 'kind': 'private_quarantine', 'reason': 'sensitive_name_or_credential_pattern'})
            self.emit({**e, 'kind': 'private_quarantine'})
            return
        if digest in self.prior:
            storage = self.prior[digest]
        else:
            blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            if blob in self.published:
                storage = {'git_blob': blob}
            else:
                if digest not in self.seen:
                    self.seen[digest] = [self.store.put(data[i:i + CHUNK]) for i in range(0, len(data), CHUNK)]
                storage = {'chunks': self.seen[digest]}
        self.emit({**e, 'kind': 'file', 'historical_source_unvalidated': is_source_file(p, data), **storage})

    def archive(self, origin, data, depth=0):
        assert depth <= 10, 'Archive nesting depth exceeded; no deletion authorized'
        digest = sha(data)
        if digest in self.containers:
            self.emit({'origin': origin, 'kind': 'duplicate_container', 'sha256': digest,
                       'bytes': len(data), 'same_members_as': self.containers[digest]})
            return
        self.containers[digest] = origin
        self.emit({'origin': origin, 'kind': 'container', 'sha256': digest, 'bytes': len(data),
                   'envelope_preserved': False})

        def member(name, payload, meta):
            assert safe_member(name), f'Unsafe archive member: {origin}: {name}'
            target = origin + '!/' + name
            why = omitted_member(target, payload)
            if why:
                self.emit({'origin': target, 'kind': why, 'bytes': len(payload), 'sha256': sha(payload)})
            elif name.endswith(ARCHIVES):
                self.archive(target, payload, depth + 1)
            else:
                self.content(target, payload, meta)

        raw = io.BytesIO(data)
        if origin.endswith('.zip'):
            with zipfile.ZipFile(raw) as z:
                for m in z.infolist():
                    assert safe_member(m.filename)
                    if not m.is_dir():
                        member(m.filename, z.read(m), {'mode': m.external_attr >> 16, 'zip_time': m.date_time})
        else:
            proc = None
            if origin.endswith('.tar.zst'):
                # Anonymous temporary input avoids pipe input/output deadlock for large containers.
                with tempfile.TemporaryFile(dir=OUT) as f:
                    f.write(data)
                    f.seek(0)
                    proc = subprocess.Popen(['zstd', '-d', '-c'], stdin=f, stdout=subprocess.PIPE)
                    self.tar_members(origin, proc.stdout, member)
                    proc.stdout.close()
                    assert proc.wait() == 0
            else:
                self.tar_members(origin, raw, member)

    def tar_members(self, origin, stream, member):
        with tarfile.open(fileobj=stream, mode='r|*') as t:
            for m in t:
                assert safe_member(m.name), (origin, m.name)
                if m.isfile():
                    assert m.size <= 512 * 1024**2, (origin, m.name, m.size)
                    member(m.name, t.extractfile(m).read(), {'mode': m.mode, 'mtime': m.mtime})
                elif m.isdir():
                    continue
                elif m.issym() or m.islnk():
                    self.emit({'origin': origin + '!/' + m.name, 'kind': 'archive_link',
                               'target': m.linkname, 'hardlink': m.islnk(), 'mode': m.mode})
                else:
                    raise RuntimeError(f'Unsupported special archive entry: {origin}: {m.name}')

    def collect(self):
        bundles = json.loads((OUT / 'git-history-coverage.json').read_text())['bundles']
        assert all(not e['unpublished_heads'] for e in bundles)
        covered_bundles = {e['path']: e for e in bundles}
        with gzip.open(OUT / 'inventory.jsonl.gz', 'rt') as stream:
            for i, line in enumerate(stream):
                e = json.loads(line)
                if e['category'] not in {'material', 'archive_container'}:
                    continue
                path = OLD / e['path']
                s = path.stat()
                assert path.resolve() == path and (s.st_size, s.st_mtime_ns, s.st_ino) == (e['bytes'], e['mtime_ns'], e['inode'])
                if path.suffix == '.bundle':
                    assert str(path) in covered_bundles
                    self.emit({'origin': e['path'], 'kind': 'published_git_bundle', 'sha256': file_sha(path),
                               'bytes': s.st_size, 'refs': covered_bundles[str(path)]['refs']})
                else:
                    data = path.read_bytes()
                    assert path.stat().st_mtime_ns == s.st_mtime_ns
                    if e['category'] == 'archive_container':
                        self.archive(e['path'], data)
                    else:
                        why = omitted_member(e['path'], data)
                        if why:
                            self.emit({'origin': e['path'], 'kind': why, 'bytes': len(data), 'sha256': sha(data)})
                        else:
                            self.content(e['path'], data, {'mode': e['mode'], 'mtime_ns': e['mtime_ns']})
                if i % 10000 == 0:
                    print('inventory', i, dict(self.counts), 'packs', len(self.store.volumes), flush=True)
        # Preserve exact loose objects and provenance, without publishing an unreviewed branch.
        objects = json.loads((OUT / 'git-unpublished-objects.json').read_text())
        by_repo = collections.defaultdict(list)
        for key, repo in objects.items():
            by_repo[repo].append(key)
        for repo, ids in by_repo.items():
            proc = subprocess.Popen(['git', '-C', repo, 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            try:
                for key in ids:
                    proc.stdin.write((key + '\n').encode())
                    proc.stdin.flush()
                    header = proc.stdout.readline().split()
                    assert len(header) == 3 and header[0].decode() == key
                    typ, length = header[1].decode(), int(header[2])
                    data = proc.stdout.read(length)
                    assert proc.stdout.read(1) == b'\n'
                    assert hashlib.sha1(typ.encode() + b' ' + str(length).encode() + b'\0' + data).hexdigest() == key
                    self.content('git-objects/' + key, data, {'git_type': typ, 'git_oid': key}, True)
            finally:
                proc.stdin.close()
                assert proc.wait(timeout=30) == 0
                proc.stdout.close()
        self.manifest.close()
        self.store.close()
        save(PACKAGE / 'chunks.json', self.store.objects)
        save(PACKAGE / 'volumes.json', self.store.volumes)
        save(PACKAGE / 'collection-summary.json', {
            'counts': self.counts, 'unique_payload_bytes': sum(e['bytes'] for e in self.store.objects.values()),
            'compressed_payload_bytes': sum(e['bytes'] for e in self.store.volumes),
            'packs': len(self.store.volumes), 'unpublished_git_objects': len(objects),
            'private_quarantine_entries': len(self.held), 'production_acceptance': False,
            'all_new_payload_visibility': 'personal repository only'})
        save(OUT / 'private-quarantine-manifest.json', self.held)
        for name in ('git-history-coverage.json', 'git-unpublished-objects.json', 'links.json'):
            shutil.copy2(OUT / name, PACKAGE / name)
        shutil.copy2('/tmp/mainline-published-git-index-20260918.json', PACKAGE / 'published-git-index.json')
        print((PACKAGE / 'collection-summary.json').read_text(), flush=True)


def append_lfs():
    """Preserve LFS content, not merely the Git pointer or a remote availability claim."""
    old = json.loads((PACKAGE / 'collection-summary.json').read_text())
    assert 'lfs_cache_paths' not in old
    known = {}
    for root in (PACKAGE, PREVIOUS / 'shared-v1', PREVIOUS / 'personal-v1'):
        with gzip.open(root / 'files.jsonl.gz', 'rt') as stream:
            for line in stream:
                e = json.loads(line)
                if e.get('kind', 'file') == 'file':
                    if root == PACKAGE:
                        storage = {k: e[k] for k in ('chunks', 'git_blob', 'prior_archive', 'prior_path', 'rebuild_member') if k in e}
                    else:
                        storage = {'prior_archive': 'shared' if root.name == 'shared-v1' else 'personal', 'prior_path': e['path']}
                    known.setdefault(e['sha256'], storage)
    store = Store(PACKAGE, compressed_budget=2 * 1024**3)
    store.volumes = json.loads((PACKAGE / 'volumes.json').read_text())
    store.objects = json.loads((PACKAGE / 'chunks.json').read_text())
    entries = []
    with gzip.open(OUT / 'inventory.jsonl.gz', 'rt') as stream:
        for line in stream:
            e = json.loads(line)
            if '/.git/lfs/objects/' not in e['path']:
                continue
            path = OLD / e['path']
            assert re.fullmatch('[0-9a-f]{64}', path.name)
            data = path.read_bytes()
            assert len(data) == e['bytes'] and sha(data) == path.name
            assert not SECRET.search(data)
            if path.name not in known:
                known[path.name] = {'chunks': [store.put(data[i:i + CHUNK]) for i in range(0, len(data), CHUNK)]}
            entries.append({'origin': e['path'], 'kind': 'file', 'bytes': len(data), 'sha256': path.name,
                            'lfs_oid': path.name, **known[path.name]})
    with gzip.open(PACKAGE / 'files.jsonl.gz', 'at') as stream:
        for e in entries:
            stream.write(json.dumps(e) + '\n')
    store.close()
    save(PACKAGE / 'chunks.json', store.objects)
    save(PACKAGE / 'volumes.json', store.volumes)
    old['counts']['file'] += len(entries)
    old.update(lfs_cache_paths=len(entries), unique_payload_bytes=sum(e['bytes'] for e in store.objects.values()),
               compressed_payload_bytes=sum(e['bytes'] for e in store.volumes), packs=len(store.volumes))
    save(PACKAGE / 'collection-summary.json', old)
    print(json.dumps(old, indent=2))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('mode', choices=['collect', 'append-lfs'])
    a = p.parse_args()
    Collector().collect() if a.mode == 'collect' else append_lfs()


if __name__ == '__main__':
    main()
