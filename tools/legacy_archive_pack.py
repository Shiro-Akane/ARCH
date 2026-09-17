#!/usr/bin/env python3
"""Content-addressed legacy data/log archive; no unknown experimental source upload.

Published Git blobs are referenced, not copied. New source of uncertain acceptance is
held for review. Source deleted under an explicit retirement manifest is excluded.
"""
import argparse
import collections
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile

from legacy_archive import BASE, save

CHUNK = 4 * 1024 * 1024
PACK_BYTES = 32 * 1024 * 1024
SOURCE = {'.h', '.hpp', '.hh', '.hxx', '.c', '.C', '.cc', '.cpp', '.cxx', '.cu', '.cuh',
          '.py', '.sh', '.ps1', '.cmake', '.patch', '.diff', '.f', '.f90', '.F90', '.js',
          '.driver', '.inc', '.inl', '.tpp', '.ii', '.orig', '.bak', '.backup'}
SECRET = re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}|'
                    rb'AKIA[A-Z0-9]{16}|-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----|'
                    rb'sk-(?:proj-)?[A-Za-z0-9_-]{40,})')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(CHUNK), b''):
            h.update(block)
    return h.hexdigest()


def is_source_file(path, data):
    if path.suffix in SOURCE or path.name in {'CMakeLists.txt', 'Makefile'}:
        return True
    if path.suffix in {'.log', '.stdout', '.stderr', '.out', '.md', '.json', '.par', '.csv', '.tsv'}:
        return False
    return bool(re.search(rb'(?m)^\s*(?:#include\s*[<"]|namespace\s+\w+|template\s*<|'
                          rb'int\s+main\s*\(|#!/[^\n]*(?:python|bash)|cmake_minimum_required\s*\()', data[:8192]))


def git_index(out, shared_only=False):
    refs = ['refs/remotes/personal/codex/hpc-cuda-optimization',
            'refs/remotes/friend/codex/hpc-cuda-optimization']
    if not shared_only:
        refs.append('refs/remotes/personal/main')
    heads = {r: subprocess.check_output(['git', 'rev-parse', r], text=True).strip() for r in refs}
    lines = subprocess.check_output(['git', 'rev-list', '--objects', *heads.values()], text=True).splitlines()
    save(out, {'refs': heads, 'objects': sorted({line.split(' ', 1)[0] for line in lines})})


class Store:
    def __init__(self, root, compressed_budget=256 * 1024 * 1024):
        self.root = root
        self.compressed_budget = compressed_budget
        self.objects, self.volumes = {}, []
        self.tar = self.gzip = self.file = None
        self.used = 0

    def close(self):
        if self.tar:
            self.tar.close()
            self.gzip.close()
            self.file.close()
            p = self.root / self.name
            self.volumes.append({'path': self.name, 'bytes': p.stat().st_size, 'sha256': file_sha(p)})
            assert p.stat().st_size < 40 * 1024 * 1024
            if sum(v['bytes'] for v in self.volumes) > self.compressed_budget:
                raise RuntimeError('Compressed archive budget exceeded; keep server originals and review storage choice.')
            self.tar = None

    def put(self, data):
        key = sha(data)
        if key in self.objects:
            return key
        if not self.tar or self.used + len(data) > PACK_BYTES:
            self.close()
            self.name = f'pack-{len(self.volumes):04d}.tar.gz'
            self.file = (self.root / self.name).open('xb')
            self.gzip = gzip.GzipFile(filename='', fileobj=self.file, mode='wb', mtime=0)
            self.tar = tarfile.open(fileobj=self.gzip, mode='w')
            self.used = 0
        info = tarfile.TarInfo(key)
        info.size, info.mode, info.mtime = len(data), 0o444, 0
        self.tar.addfile(info, io.BytesIO(data))
        self.objects[key] = {'volume': self.name, 'bytes': len(data)}
        self.used += len(data)
        return key


def collect(audit, index):
    output = audit / 'payload-v1'
    output.mkdir(exist_ok=False)
    git = json.loads(index.read_text())
    published = set(git['objects'])
    retired = {e['path'].removeprefix(str(BASE) + '/') for e in json.loads(
        (audit / 'failed-source-retirement/source-delete-manifest.json').read_text())}
    store = Store(output)
    count = collections.Counter()
    held, seen = [], {}
    entries = json.loads((audit / 'candidates.json').read_text())
    with gzip.open(output / 'files.jsonl.gz', 'wt', encoding='utf-8') as manifest:
        for i, e in enumerate(entries):
            p = BASE / e['path']
            if e['path'] in retired:
                count['retired_source_excluded'] += 1
                continue
            if set(p.parts) & {'.codex', '.claude', '.ssh', '.aws', '.azure', '.config'}:
                held.append({'path': e['path'], 'reason': 'private_configuration_review'})
                continue
            if e['bytes'] > 256 * 1024 * 1024:
                held.append({'path': e['path'], 'reason': 'large_file_review', 'bytes': e['bytes']})
                continue
            before = p.stat()
            assert p.resolve() == p and before.st_size == e['bytes'] and before.st_mtime_ns == e['mtime_ns'], p
            data = p.read_bytes()
            after = p.stat()
            assert (before.st_size, before.st_mtime_ns, before.st_ino) == (after.st_size, after.st_mtime_ns, after.st_ino), p
            if SECRET.search(data):
                held.append({'path': e['path'], 'reason': 'credential_pattern_review'})
                continue
            digest = sha(data)
            blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            is_source = is_source_file(p, data)
            if blob in published:
                storage = {'git_blob': blob}
                count['published_git_references'] += 1
            elif is_source:
                held.append({'path': e['path'], 'reason': 'unpublished_source_acceptance_not_established',
                             'sha256': digest, 'bytes': len(data)})
                continue
            else:
                if digest not in seen:
                    seen[digest] = [store.put(data[j:j + CHUNK]) for j in range(0, len(data), CHUNK)]
                storage = {'chunks': seen[digest]}
                count['new_data_or_log_files'] += 1
            manifest.write(json.dumps({**e, 'sha256': digest, **storage}) + '\n')
            count['archived_logical_bytes'] += len(data)
            if i % 10000 == 0:
                print(f'Processed {i}/{len(entries)} working-copy files', flush=True)
    store.close()
    save(output / 'chunks.json', store.objects)
    save(output / 'volumes.json', store.volumes)
    save(output / 'held-for-review.json', held)
    save(output / 'published-git-bases.json', git['refs'])
    save(output / 'collection-summary.json', {
        'counts': dict(count), 'held_files': len(held),
        'held_reasons': dict(collections.Counter(e['reason'] for e in held)),
        'unique_payload_bytes': sum(x['bytes'] for x in store.objects.values()),
        'compressed_payload_bytes': sum(x['bytes'] for x in store.volumes),
        'pack_count': len(store.volumes), 'source_payload_uploaded': False,
        'scope': 'Loose legacy working-copy files; existing tar/zip/bundle containers remain indexed on server, not republished.',
    })
    print((output / 'collection-summary.json').read_text(), flush=True)


def split_visibility(root, shared_index):
    """Keep personal-main-only research out of the friend's fixed branch."""
    common = json.loads(shared_index.read_text())
    shared_objects = set(common['objects'])
    stores, manifests, references, counts = {}, {}, {}, {}
    private_parts = {'benchmarks', 'datasets', 'experiments', '.superpowers', '.codex', 'project_archive'}

    def private(entry):
        return bool(set(PurePosixPath(entry['path']).parts) & private_parts or
                    ('git_blob' in entry and entry['git_blob'] not in shared_objects))

    for name in ('shared', 'personal'):
        output = root.parent / (name + '-v1')
        output.mkdir(exist_ok=False)
        stores[name] = Store(output)
        manifests[name] = gzip.open(output / 'files.jsonl.gz', 'wt', encoding='utf-8')
        references[name] = set()
        counts[name] = collections.Counter()
    with gzip.open(root / 'files.jsonl.gz', 'rt', encoding='utf-8') as source:
        for line in source:
            e = json.loads(line)
            name = 'personal' if private(e) else 'shared'
            manifests[name].write(line)
            references[name].update(e.get('chunks', []))
            counts[name]['files'] += 1
            counts[name]['logical_bytes'] += e['bytes']
            counts[name]['git_references' if 'git_blob' in e else 'payload_files'] += 1
    for stream in manifests.values():
        stream.close()
    original_chunks = json.loads((root / 'chunks.json').read_text())
    by_volume = collections.defaultdict(set)
    for key, info in original_chunks.items():
        by_volume[info['volume']].add(key)
    for v in json.loads((root / 'volumes.json').read_text()):
        copied = set()
        for name, store in stores.items():
            if by_volume[v['path']] <= references[name]:
                store.close()
                target = f'pack-{len(store.volumes):04d}.tar.gz'
                assert not (store.root / target).exists()
                shutil.copy2(root / v['path'], store.root / target)
                store.volumes.append({**v, 'path': target})
                for key in by_volume[v['path']]:
                    store.objects[key] = {**original_chunks[key], 'volume': target}
                copied.add(name)
        remaining = [name for name in stores if name not in copied and by_volume[v['path']] & references[name]]
        if not remaining:
            continue
        with tarfile.open(root / v['path'], 'r:gz') as tar:
            for member in tar:
                assert member.isfile() and re.fullmatch('[0-9a-f]{64}', member.name)
                names = [name for name in remaining if member.name in references[name]]
                if not names:
                    continue
                data = tar.extractfile(member).read()
                assert sha(data) == member.name
                for name in names:
                    assert stores[name].put(data) == member.name
    held = json.loads((root / 'held-for-review.json').read_text())
    for name, store in stores.items():
        store.close()
        assert set(store.objects) == references[name]
        output = store.root
        save(output / 'chunks.json', store.objects)
        save(output / 'volumes.json', store.volumes)
        save(output / 'held-for-review.json', [e for e in held if ('personal' if private(e) else 'shared') == name])
        save(output / 'published-git-bases.json', common['refs'] if name == 'shared' else
             json.loads((root / 'published-git-bases.json').read_text()))
        summary = {**dict(counts[name]), 'visibility': name,
                   'unique_payload_bytes': sum(c['bytes'] for c in store.objects.values()),
                   'compressed_payload_bytes': sum(v['bytes'] for v in store.volumes),
                   'pack_count': len(store.volumes), 'source_payload_uploaded': False,
                   'original_scope': 'Loose legacy working copies only; existing archive containers are not republished.'}
        save(output / 'collection-summary.json', summary)
        print(json.dumps(summary, indent=2), flush=True)


def verify(root):
    chunks = json.loads((root / 'chunks.json').read_text())
    volumes = json.loads((root / 'volumes.json').read_text())
    assert shutil.disk_usage(root).free > sum(e['bytes'] for e in chunks.values()) + 512 * 1024 * 1024
    cache, used, unique = {}, set(), set()
    # Newly created, private temporary directory only; no existing user paths are removed.
    with tempfile.TemporaryDirectory(prefix='verify-chunks-', dir=root.resolve()) as temp:
        temp = Path(temp).resolve()
        assert temp.parent == root.resolve()
        for v in volumes:
            path = root / v['path']
            assert PurePosixPath(v['path']).name == v['path'] and file_sha(path) == v['sha256']
            assert path.stat().st_size == v['bytes']
            with tarfile.open(path, 'r:gz') as tar:
                for member in tar:
                    assert member.isfile() and re.fullmatch('[0-9a-f]{64}', member.name)
                    assert member.name in chunks and member.name not in unique
                    info = chunks[member.name]
                    assert info['volume'] == v['path'] and info['bytes'] == member.size <= CHUNK
                    data = tar.extractfile(member).read()
                    assert sha(data) == member.name
                    (temp / member.name).write_bytes(data)
                    unique.add(member.name)
        assert unique == set(chunks)
        git = subprocess.Popen(['git', 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        files, logical = 0, 0
        try:
            with gzip.open(root / 'files.jsonl.gz', 'rt', encoding='utf-8') as manifest:
                for line in manifest:
                    entry = json.loads(line)
                    origin = PurePosixPath(entry['path'])
                    assert not origin.is_absolute() and '..' not in origin.parts
                    key = entry.get('git_blob') or tuple(entry['chunks'])
                    if key not in cache:
                        if 'git_blob' in entry:
                            assert re.fullmatch('[0-9a-f]{40}', key)
                            git.stdin.write((key + '\n').encode())
                            git.stdin.flush()
                            header = git.stdout.readline().split()
                            assert len(header) == 3 and header[1] == b'blob'
                            data = git.stdout.read(int(header[2]))
                            assert git.stdout.read(1) == b'\n'
                            cache[key] = (sha(data), len(data))
                        else:
                            h, length = hashlib.sha256(), 0
                            for chunk in key:
                                data = (temp / chunk).read_bytes()
                                h.update(data)
                                length += len(data)
                            cache[key] = (h.hexdigest(), length)
                    assert cache[key] == (entry['sha256'], entry['bytes']), entry['path']
                    used.update(entry.get('chunks', []))
                    files += 1
                    logical += entry['bytes']
            assert used == unique
        finally:
            git.stdin.close()
            git.wait(timeout=30)
            git.stdout.close()
    result = {'all_passed': True, 'files_verified': files, 'logical_bytes_verified': logical,
              'chunks_verified': len(unique), 'packs_verified': len(volumes),
              'source_payload_uploaded': False, 'git_blobs_checked_against_local_published_history': True}
    save(root / 'local-verification.json', result)
    print(json.dumps(result, indent=2))


def restore_one(root, origin, destination):
    """Restore one exact historical path to a new caller-selected file, never overwrite."""
    selected = []
    with gzip.open(root / 'files.jsonl.gz', 'rt', encoding='utf-8') as manifest:
        for line in manifest:
            entry = json.loads(line)
            if entry['path'] == origin:
                selected.append(entry)
    assert len(selected) == 1, 'An exact archived origin path is required'
    entry = selected[0]
    assert not destination.exists(), 'Refusing to overwrite an existing file'
    if 'git_blob' in entry:
        assert re.fullmatch('[0-9a-f]{40}', entry['git_blob'])
        data = subprocess.check_output(['git', 'cat-file', 'blob', entry['git_blob']])
    else:
        chunks = json.loads((root / 'chunks.json').read_text())
        volumes = {e['path']: e for e in json.loads((root / 'volumes.json').read_text())}
        checked, parts = set(), []
        for key in entry['chunks']:
            assert re.fullmatch('[0-9a-f]{64}', key)
            name = chunks[key]['volume']
            assert PurePosixPath(name).name == name
            if name not in checked:
                assert file_sha(root / name) == volumes[name]['sha256']
                checked.add(name)
            with tarfile.open(root / name, 'r:gz') as tar:
                member = tar.getmember(key)
                assert member.isfile() and member.size == chunks[key]['bytes']
                part = tar.extractfile(member).read()
                assert sha(part) == key
                parts.append(part)
        data = b''.join(parts)
    assert (len(data), sha(data)) == (entry['bytes'], entry['sha256'])
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as stream:
        stream.write(data)
    print(json.dumps({'restored_origin': origin, 'destination': str(destination.resolve()),
                      'bytes': len(data), 'sha256': sha(data)}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['git-index', 'collect', 'split', 'verify', 'restore'])
    parser.add_argument('path', type=Path)
    parser.add_argument('--git-index', type=Path)
    parser.add_argument('--shared-only', action='store_true')
    parser.add_argument('--origin')
    parser.add_argument('--destination', type=Path)
    args = parser.parse_args()
    if args.mode == 'git-index':
        git_index(args.path, args.shared_only)
    elif args.mode == 'collect':
        collect(args.path, args.git_index)
    elif args.mode == 'split':
        split_visibility(args.path, args.git_index)
    elif args.mode == 'restore':
        assert args.origin and args.destination
        restore_one(args.path, args.origin, args.destination)
    else:
        verify(args.path)
