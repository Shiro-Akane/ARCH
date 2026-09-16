"""Verify both downloaded archives and every raw/projected file, without rewriting bytes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(name):
    path = PurePosixPath(name)
    if path.is_absolute() or not path.parts or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('unsafe archive/manifest member: ' + name)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    download = args.download.resolve(strict=True)
    output = args.output.resolve()
    if output.exists() or output.is_symlink():
        raise ValueError('new verification/extraction directory required')
    receipt_path = download / 'factory-focused-collection-v1.json'
    receipt = json.loads(receipt_path.read_text())
    archives = {}
    for kind in ('raw', 'compact'):
        record = receipt[kind]
        archive = download / PurePosixPath(record['path']).name
        if archive.stat().st_size != record['bytes'] or sha(archive) != record['sha256']:
            raise ValueError('downloaded archive identity mismatch: ' + kind)
        members = subprocess.run(['tar', '-tf', str(archive)], capture_output=True,
                                 text=True, check=True).stdout.splitlines()
        details = subprocess.run(['tar', '-tvf', str(archive)], capture_output=True,
                                 text=True, check=True).stdout.splitlines()
        if len(details) != len(members) or any(not line.startswith(('-', 'd')) for line in details):
            raise ValueError('archive must contain only regular files/directories, no links or special nodes')
        for name in members:
            safe_relative(name)
        archives[kind] = archive
    output.mkdir(parents=True)
    for kind, archive in archives.items():
        destination = output / kind
        destination.mkdir()
        subprocess.run(['tar', '-xf', str(archive), '-C', str(destination)], check=True)
    compact = output / 'compact/compact'
    manifest = json.loads((compact / 'raw-manifest.json').read_text())
    omitted = json.loads((compact / 'raw-only-files.json').read_text())
    if len(manifest) != receipt['raw']['files'] or len(omitted) != len(set(omitted)):
        raise ValueError('raw inventory count/omissions inconsistent')
    if not set(omitted) <= set(manifest):
        raise ValueError('omitted member absent from raw inventory')
    total = 0
    for name, record in manifest.items():
        relative = safe_relative(name)
        path = output / 'raw' / relative
        if path.is_symlink() or path.stat().st_size != record['bytes'] or sha(path) != record['sha256']:
            raise ValueError('raw extracted member mismatch: ' + name)
        total += record['bytes']
    projected = {p.relative_to(compact / 'records').as_posix(): p
                 for p in (compact / 'records').rglob('*') if p.is_file()}
    if set(projected) != set(manifest) - set(omitted):
        raise ValueError('compact projection does not account for every raw member')
    for name, path in projected.items():
        if path.is_symlink() or path.stat().st_size != manifest[name]['bytes'] or sha(path) != manifest[name]['sha256']:
            raise ValueError('projected member changed bytes: ' + name)
    embedded = output / 'raw/ARCH-native-wave-v4-20260916/factory-focused-evidence-v1/compact'
    for name in ('raw-manifest.json', 'raw-only-files.json', 'external-dependencies.json'):
        if sha(embedded / name) != sha(compact / name):
            raise ValueError('embedded raw metadata differs from downloaded compact: ' + name)
    for kind, path in archives.items():
        if sha(path) != receipt[kind]['sha256']:
            raise ValueError('archive changed during verification: ' + kind)
    result = dict(status='both_archives_and_all_members_byte_verified',
        verified_utc=datetime.now(timezone.utc).isoformat(),
        raw=receipt['raw'], compact=receipt['compact'],
        local_archives={kind: str(path) for kind, path in archives.items()},
        raw_files=len(manifest), raw_file_bytes=total, projected_files=len(projected),
        raw_only_files=len(omitted), receipt_sha256=sha(receipt_path),
        verification_recipe_sha256=sha(Path(__file__)),
        scientific_qualification_added=False, performance_qualified=False)
    with (output / 'factory-focused-local-receipt-v1.json').open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
