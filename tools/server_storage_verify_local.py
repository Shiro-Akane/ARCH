#!/usr/bin/env python3
"""Verify all rebuild-metadata bytes and the local scientific archive before cleanup."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import tarfile

from server_storage_audit import digest, save
from server_storage_prepare import ZSTD_SHA


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--s4-archive', type=Path, required=True)
    args = parser.parse_args()
    preflight = json.loads((args.audit / 'preflight.json').read_text())
    archive = args.audit / 'rebuild-metadata.tar.gz'
    assert digest(archive) == preflight['metadata_archive']['sha256']
    expected = {r['path'].removeprefix('/home/ubuntu/projects/'): r
                for r in json.loads((args.audit / 'build-metadata-manifest.json').read_text())}
    checked = set()
    with tarfile.open(archive, 'r:gz') as tar:
        for member in tar:
            assert member.isfile() and member.name in expected and member.name not in checked, member.name
            assert not member.name.startswith('/') and '..' not in Path(member.name).parts
            h = hashlib.sha256()
            with tar.extractfile(member) as stream:
                for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
                    h.update(chunk)
            assert h.hexdigest() == expected[member.name]['sha256'], member.name
            assert member.size == expected[member.name]['bytes']
            checked.add(member.name)
    assert checked == set(expected)
    assert digest(args.s4_archive) == ZSTD_SHA
    allowlist_sha = digest(args.audit / 'deletion-allowlist.json')
    assert allowlist_sha == preflight['deletion_allowlist_sha256']
    receipt = {'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'all_passed': True, 'metadata_files_verified': len(checked),
               'metadata_archive_sha256': digest(archive),
               'metadata_archive_local_path': str(archive.resolve()),
               'canonical_s4_sha256': ZSTD_SHA,
               'canonical_s4_local_path': str(args.s4_archive.resolve()),
               'deletion_allowlist_sha256': allowlist_sha,
               'note': 'Compiler intermediates are discarded as rebuildable, not backed up byte-for-byte.'}
    save(args.audit / 'local-backup-receipt.json', receipt)
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
