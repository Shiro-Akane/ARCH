"""Losslessly compact only the already double-backed-up fixed coupled traces.

Run between formal phases, never concurrently with ARCH or CUDA compilation.
Preserves all unique log bytes, both original raw archives and all science records.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile

ROOT = Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914')
BASE = ROOT / 'build/fix-20260914'
ARCHIVE = ROOT / 'build/fixed-science-v1.tar.zst'
EXPECTED_SHA = '613405b2b5570bdbb081a95feed5f863b66e699e9afab9a6d1881c79b99abbae'
EXPECTED_BYTES = 1601189714


def digest(stream):
    h = hashlib.sha256()
    while chunk := stream.read(1024*1024):
        h.update(chunk)
    return h.hexdigest()


def sha(path):
    with path.open('rb') as stream:
        return digest(stream)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--local-verified-sha256', required=True)
    p.add_argument('--local-verified-bytes', required=True, type=int)
    p.add_argument('--apply', action='store_true')
    a = p.parse_args()
    for command in ('ARCH', 'nvcc', 'ptxas'):
        assert subprocess.run(['pgrep', '-x', command], stdout=subprocess.DEVNULL).returncode == 1, \
            'Wait for active formal/build jobs; do not disturb them'
    assert a.local_verified_sha256 == EXPECTED_SHA and a.local_verified_bytes == EXPECTED_BYTES
    assert not ARCHIVE.is_symlink() and ARCHIVE.resolve().is_relative_to(ROOT.resolve())
    assert ARCHIVE.stat().st_size == EXPECTED_BYTES and sha(ARCHIVE) == EXPECTED_SHA
    science = json.loads((BASE/'coupled-scale-v1/evidence.json').read_text())
    assert science['status'] == 'passed' and science['identity_verified']
    assert len(science['cases']) == 18 and len(science['lanes']) == 36
    expected = {}
    for line in (BASE/'science-archive-v1/compact/raw-files.sha256').read_text().splitlines():
        checksum, name = line.split('  ', 1)
        assert len(checksum) == 64 and name not in expected
        expected[name] = checksum
    targets = {}
    for name in ('coupled-scale-v1', 'coupled-critical-b128-v1'):
        scope = BASE/name
        assert not scope.is_symlink() and scope.resolve().is_relative_to(BASE.resolve())
        for path in scope.rglob('MicrophysicsTiming_backend_trace.tsv'):
            assert not path.is_symlink() and path.resolve().is_relative_to(scope.resolve())
            if path.stat().st_size <= 2*1024*1024:
                continue
            relative = path.relative_to(ROOT).as_posix()
            assert relative in expected and relative not in targets
            assert not path.with_suffix('.tsv.zst').exists()
            targets[relative] = path
    assert targets
    verified = {}
    with subprocess.Popen(['zstd', '-dc', str(ARCHIVE)], stdout=subprocess.PIPE) as decoder:
        with tarfile.open(fileobj=decoder.stdout, mode='r|') as tar:
            for member in tar:
                if member.name not in targets:
                    continue
                assert member.isfile() and member.name not in verified
                path = targets[member.name]
                assert member.size == path.stat().st_size and sha(path) == expected[member.name]
                stream = tar.extractfile(member)
                assert stream is not None and digest(stream) == expected[member.name]
                verified[member.name] = dict(bytes=member.size, sha256=expected[member.name])
        assert decoder.wait() == 0
    assert set(verified) == set(targets) and sha(ARCHIVE) == EXPECTED_SHA
    output = BASE/'coupled-preformal-trace-compression-v1.json'
    assert not output.exists()
    record = dict(scope='only fixed coupled-scale and critical-b128 completed backend TSV traces',
        archive=str(ARCHIVE), archive_sha256=EXPECTED_SHA, archive_bytes=EXPECTED_BYTES,
        windows_copy='C:/tmp/ARCH-perf-20260909/build/fixed-science-v1.tar.zst',
        windows_copy_sha256=a.local_verified_sha256, windows_copy_bytes=a.local_verified_bytes,
        verified=verified, bytes=sum(v['bytes'] for v in verified.values()),
        compressed=[], removed=[], applied=False)
    output.write_text(json.dumps(record, indent=2)+'\n')
    if a.apply:
        try:
            for name, path in targets.items():
                assert sha(path) == expected[name]
                destination = path.with_suffix('.tsv.zst')
                subprocess.run(['zstd', '-T1', '-3', '--quiet', '--keep', str(path),
                                '-o', str(destination)], check=True)
                with subprocess.Popen(['zstd', '-dc', str(destination)], stdout=subprocess.PIPE) as decoder:
                    assert digest(decoder.stdout) == expected[name]
                    assert decoder.wait() == 0
                assert sha(path) == expected[name] and not path.is_symlink()
                record['compressed'].append(dict(original=name, path=str(destination),
                    bytes=destination.stat().st_size, sha256=sha(destination),
                    decompressed_sha256=expected[name]))
                path.unlink()
                record['removed'].append(name)
            record['applied'] = True
        finally:
            output.write_text(json.dumps(record, indent=2)+'\n')
    print(json.dumps(dict(applied=record['applied'], files=len(verified),
        original_bytes=record['bytes'], compressed_bytes=sum(v['bytes'] for v in record['compressed']))))
    print('VERIFIED_COUPLED_SCIENCE_TRACE_COMPRESSION_PASS')


if __name__ == '__main__':
    main()
