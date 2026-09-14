"""Read the existing local canonical archive without expanding gigabytes to disk.

Preparation only: compute an independent local member manifest. No SSH, deletes,
server operations, production edits or scientific qualification occur here.
"""
import hashlib
import json
from pathlib import Path
import subprocess


BASE = Path(__file__).resolve().parent
ARCHIVE = BASE / 's4-validation-20260913-evidence.tar.zst'
SHA = 'ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493'
SIZE = 1236911980
PREFIX = 's4-validation-20260913/curved/'
OUT = BASE / 's4-curved-local-proof-20260915'


def file_hash(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main():
    if OUT.exists():
        raise RuntimeError('Fresh proof output required; no existing evidence overwritten')
    assert ARCHIVE.stat().st_size == SIZE and file_hash(ARCHIVE) == SHA
    listing = subprocess.run(['tar', '-tvf', str(ARCHIVE)], check=True,
                             capture_output=True, text=True).stdout.splitlines()
    selected = []
    for line in listing:
        fields = line.split(maxsplit=8)
        if len(fields) != 9:
            continue
        name = fields[8]
        if name.startswith(PREFIX) and name.endswith('.h5'):
            assert fields[0].startswith('-'), 'Only regular HDF members accepted'
            assert '\\' not in name and '..' not in name.split('/')
            assert not any(c in name for c in '\r\n\t')
            selected.append((name, int(fields[4])))
    assert len(selected) == 384 and len({name for name, _ in selected}) == 384
    assert sum(size for _, size in selected) == 5229992704
    OUT.mkdir()
    names = OUT / 'members.txt'
    names.write_text(''.join(name + '\n' for name, _ in selected), encoding='utf-8', newline='\n')
    verified = {}
    command = ['tar', '-xOf', str(ARCHIVE), '-T', str(names)]
    with (OUT / 'bsdtar.stderr').open('wb') as error:
        proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=error)
        try:
            for name, size in selected:
                remaining = size
                digest = hashlib.sha256()
                while remaining:
                    chunk = proc.stdout.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise RuntimeError(f'Archive stdout ended early at {name}')
                    digest.update(chunk)
                    remaining -= len(chunk)
                verified[name] = dict(bytes=size, sha256=digest.hexdigest())
            assert proc.stdout.read(1) == b'', 'Unexpected extra selected-member bytes'
            assert proc.wait() == 0, 'Archive reader failed'
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
            proc.stdout.close()
    assert file_hash(ARCHIVE) == SHA
    proof = dict(status='local_archive_member_proof_only',
        archive=dict(path=str(ARCHIVE), bytes=SIZE, sha256=SHA,
            server_path='/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913/build/s4-validation-20260913-evidence.tar.zst'),
        prefix=PREFIX, files=len(verified), bytes=sum(v['bytes'] for v in verified.values()),
        method='bsdtar member-byte stdout in verified archive listing order; no expanded HDF files',
        server_live_copies_checked=False, any_files_removed=False, scientific_validation=False,
        command=command, members=verified)
    path = OUT / 'local-proof.json'
    path.write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(path=str(path), sha256=file_hash(path), files=proof['files'],
                         bytes=proof['bytes'], any_files_removed=False)))


if __name__ == '__main__':
    main()
