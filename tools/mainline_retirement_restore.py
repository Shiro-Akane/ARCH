#!/usr/bin/env python3
"""Restore one exact historical entry to a new file, never to its recorded path."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

from legacy_archive_pack import file_sha, sha


def find(root, field, value):
    with gzip.open(root / 'files.jsonl.gz', 'rt') as stream:
        matches = [e for line in stream if (e := json.loads(line)).get(field) == value]
    assert len(matches) == 1, 'Require one exact origin (container aliases must use same_members_as)'
    return matches[0]


def read(root, entry, prior, metadata, repo):
    if 'prior_archive' in entry:
        p = prior[entry['prior_archive']]
        return read(p, find(p, 'path', entry['prior_path']), prior, metadata, repo)
    if 'git_blob' in entry:
        return subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', entry['git_blob']])
    if 'rebuild_member' in entry:
        with tarfile.open(metadata, 'r|gz') as t:
            for m in t:
                if m.name == entry['rebuild_member']:
                    assert m.isfile()
                    return t.extractfile(m).read()
        raise RuntimeError('Missing rebuild metadata member')
    chunks = json.loads((root / 'chunks.json').read_text())
    volumes = {v['path']: v for v in json.loads((root / 'volumes.json').read_text())}
    fetched = {}
    by_volume = {}
    for key in entry['chunks']:
        by_volume.setdefault(chunks[key]['volume'], set()).add(key)
    for volume, keys in by_volume.items():
        assert Path(volume).name == volume and file_sha(root / volume) == volumes[volume]['sha256']
        with tarfile.open(root / volume, 'r:gz') as t:
            for m in t:
                if m.name in keys:
                    assert m.isfile() and m.size == chunks[m.name]['bytes']
                    data = t.extractfile(m).read()
                    assert sha(data) == m.name
                    fetched[m.name] = data
    return b''.join(fetched[key] for key in entry['chunks'])


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--package', required=True, type=Path)
    p.add_argument('--shared', required=True, type=Path)
    p.add_argument('--personal', required=True, type=Path)
    p.add_argument('--metadata', required=True, type=Path)
    p.add_argument('--git-repo', required=True, type=Path)
    p.add_argument('--origin', required=True)
    p.add_argument('--destination', required=True, type=Path)
    a = p.parse_args()
    e = find(a.package, 'origin', a.origin)
    assert e['kind'] == 'file' and not a.destination.exists()
    data = read(a.package, e, {'shared': a.shared, 'personal': a.personal}, a.metadata, a.git_repo)
    assert len(data) == e['bytes'] and sha(data) == e['sha256']
    if 'git_oid' in e:
        assert hashlib.sha1(e['git_type'].encode() + b' ' + str(len(data)).encode() + b'\0' + data).hexdigest() == e['git_oid']
    a.destination.parent.mkdir(parents=True, exist_ok=True)
    with a.destination.open('xb') as stream:
        stream.write(data)
    print(json.dumps({'restored': a.origin, 'destination': str(a.destination.resolve()), 'sha256': e['sha256']}))


if __name__ == '__main__':
    main()
