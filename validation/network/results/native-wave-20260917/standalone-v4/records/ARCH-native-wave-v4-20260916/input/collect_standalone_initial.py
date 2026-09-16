"""Archive completed isolated native contracts, not ODE/application qualification."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from prepare_factory_overlay import verify_contract_receipt


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def collect(root):
    root = root.resolve(strict=True)
    projects = Path('/home/ubuntu/projects')
    if root.parent != projects or root.name != 'ARCH-native-wave-v4-20260916':
        raise ValueError('unexpected isolated root')
    if (root / 'control/exit-code').read_text().strip() != '0':
        raise ValueError('worker not successfully complete; preserve failure separately')
    _, record = verify_contract_receipt(root / 'contracts/record.json', root / 'input/payload',
                                        root / 'input/shared-inputs.json')
    guard = (root / 'control/guard.log').read_text()
    if not all(v in guard for v in ('guard_stopped=False', 'stop_reason=none',
                                    'scope=whole_device', 'complete=True', 'scope=linux_system')):
        raise ValueError('incomplete/failed resource guard')
    paths = {p for part in ('input', 'control', 'contracts') for p in (root / part).rglob('*') if p.is_file()}
    paths.update(root / part for part in ('worker.sh', 'input-v4.tar'))
    paths.update(Path(p) for p in record['inputs'])
    paths.update(Path(p) for p in record['artifacts'])
    paths.add(Path(__file__).resolve())
    paths.add(Path(__file__).with_name('prepare_factory_overlay.py').resolve())
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(projects) for p in paths):
        raise ValueError('missing/symlinked/out-of-scope input')
    out = root / 'standalone-evidence'
    raw = root / 'standalone-raw-v1.tar.zst'
    compact_pack = root / 'standalone-compact-v1.tar.zst'
    if any(p.exists() for p in (out, raw, compact_pack)):
        raise ValueError('refuse existing/partial archive outputs')
    compact = out / 'compact'
    compact.mkdir(parents=True)
    manifest = {p.relative_to(projects).as_posix(): dict(bytes=p.stat().st_size, sha256=sha(p)) for p in sorted(paths)}
    (compact / 'raw-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    for p in sorted(paths):
        with p.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or p.suffix in ('.a', '.o', '.so', '.tar', '.zst'):
            continue
        target = compact / 'records' / p.relative_to(projects)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(p, target)
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(projects),
                    *manifest, str(compact.relative_to(projects))], check=True)
    receipt = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest),
                   status=record['status'], contracts=len(record['tests']), ODE_qualified=False,
                   application_qualified=False, performance_qualified=False, release_qualified=False)
    (compact / 'raw-archive.json').write_text(json.dumps(receipt, indent=2) + '\n')
    subprocess.run(['tar', '--zstd', '-cf', str(compact_pack), '-C', str(out), 'compact'], check=True)
    result = dict(raw=receipt, compact=dict(path=str(compact_pack), bytes=compact_pack.stat().st_size,
                                           sha256=sha(compact_pack)))
    with (root / 'standalone-collection.json').open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    collect(Path(sys.argv[1]))
