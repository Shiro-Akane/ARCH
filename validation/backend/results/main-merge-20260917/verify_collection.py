#!/usr/bin/env python3
"""Verify both server archives and every member before publishing the projection."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile

source = Path(sys.argv[1])
destination = Path(__file__).resolve().parent
receipt = json.loads((source / 'collection-v1.json').read_text())


def sha(data):
    return hashlib.sha256(data).hexdigest()


def members(kind):
    entry = receipt[kind]
    path = source / entry['name']
    data = path.read_bytes()
    assert len(data) == entry['bytes'] and sha(data) == entry['sha256'], kind
    result = {}
    with tarfile.open(path, 'r:gz') as archive:
        for member in archive:
            name = PurePosixPath(member.name)
            assert member.isfile() and not name.is_absolute() and '..' not in name.parts
            assert '\\' not in member.name and ':' not in member.name
            assert member.name not in result
            result[member.name] = archive.extractfile(member).read()
    return result


raw, compact = members('raw'), members('compact')
manifest = json.loads(raw['collection-members-v1.json'])
expected = {item['path']: item for item in manifest}
assert len(expected) == len(manifest)
assert set(raw) == set(expected) | {'collection-members-v1.json'}
assert len(raw) == receipt['files'] and len(raw) - len(compact) == receipt['raw_only_files']
for name, value in expected.items():
    assert len(raw[name]) == value['bytes'] and sha(raw[name]) == value['sha256'], name
for name, data in compact.items():
    assert name in raw and data == raw[name], name
records = destination / 'records'
records.mkdir(exist_ok=False)
for name, data in compact.items():
    path = records / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
archives = destination / 'archives'
archives.mkdir(exist_ok=False)
# This particular raw package is small enough for ordinary Git; older large
# raw archives retain their separate server/local backup policy.
shutil.copy2(source / receipt['raw']['name'], archives / receipt['raw']['name'])
shutil.copy2(source / 'collection-v1.json', destination / 'collection-v1.json')
(destination / 'local-receipt-v1.json').write_text(json.dumps({
    'status': 'both_archives_and_all_members_verified', 'raw_files': len(raw),
    'compact_files': len(compact), 'raw': receipt['raw'], 'compact': receipt['compact'],
    'raw_committed_to_git': True, 'gpu_qualification_added': False,
    'verifier_sha256': sha(Path(__file__).read_bytes())}, indent=2) + '\n')
print(f'verified {len(raw)} raw members and {len(compact)} projected members')
