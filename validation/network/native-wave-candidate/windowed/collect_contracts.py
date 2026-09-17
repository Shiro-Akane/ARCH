"""Archive a completed (possibly failed) independent window-contract attempt.

Not a dispatcher; no active experiment may overlap collection. Original source
provenance is retained by the factory parent; all new inputs/products are here.
"""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
sys.path.insert(0, str(ROOT/'input' if (ROOT/'input').is_dir() else Path(__file__).resolve().parent.parent))
from collect_factory import completion, inventory, sha, successful_guard
sys.path.insert(0, str(ROOT/'window-input-v1' if (ROOT/'window-input-v1').is_dir() else Path(__file__).resolve().parent))
from protocol import MATRIX, verify_record


def main():
    control, output = ROOT/'window-control-v1', ROOT/'window-contract-v1'
    code = completion(control)
    record = json.loads((output/'record.json').read_text()) if (output/'record.json').exists() else {}
    if code == 0:
        verify_record(record, output)
        successful_guard(control/'window-guard.log')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'window-test',
                 'leaf-audit150-baseline', 'leaf-audit150-inline', 'leaf-audit200-baseline', 'leaf-audit200-inline'):
        result = subprocess.run(['pgrep', '-f', '^.*/'+re.escape(name)+r'( |$)'], capture_output=True)
        if result.returncode != 1:
            raise RuntimeError('owned build/experiment remains active or process check failed')
    gpu = subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                         capture_output=True, text=True, check=True).stdout.strip()
    if gpu:
        raise RuntimeError('GPU remains active; collection must wait')
    paths = {path for directory in (control, output, ROOT/'window-input-v1')
             for path in directory.rglob('*') if path.is_file()}
    for path, expected in {**record.get('inputs', {}), **record.get('artifacts', {})}.items():
        actual = Path(path)
        if sha(actual) != expected:
            raise ValueError('window input/product changed: '+path)
        paths.add(actual)
    parent = ROOT/'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        manifest = parent/(name+'.sha256')
        inventory(manifest, ROOT/'source')
        paths.add(manifest)
    inventory(control/'recipes.sha256', ROOT/'source')
    dependencies = {str(path): dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=value)
                    for path, value in inventory(parent/'vendor.sha256', ROOT/'source').items()}
    paths.update(ROOT/name for name in ('leaf-inline-collection-v1.json', 'leaf-inline-local-receipt-v1.json',
                                        'batch-launch-collection-v1.json', 'batch-launch-local-receipt-v1.json',
                                        'factory-focused-collection-v1.json'))
    paths.update((Path(__file__).resolve(), ROOT/'input/collect_factory.py'))
    if any(path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(ROOT) for path in paths):
        raise ValueError('unsafe/missing window archive input')
    evidence = ROOT/'window-evidence-v1'
    raw, packed = ROOT/'window-raw-v1.tar.zst', ROOT/'window-compact-v1.tar.zst'
    receipt = ROOT/'window-collection-v1.json'
    if any(path.exists() for path in (evidence, raw, packed, receipt)):
        raise ValueError('existing/partial archive outputs must be preserved')
    compact = evidence/'compact'
    compact.mkdir(parents=True)
    manifest = {path.relative_to(ROOT.parent).as_posix(): dict(bytes=path.stat().st_size, sha256=sha(path))
                for path in sorted(paths)}
    omitted = []
    for path in sorted(paths):
        relative = path.relative_to(ROOT.parent)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or path.suffix in ('.o', '.a', '.so', '.zst'):
            omitted.append(relative.as_posix())
            continue
        target = compact/'records'/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                        ('external-dependencies.json', dependencies)):
        (compact/name).write_text(json.dumps(value, indent=2)+'\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(ROOT.parent),
                    *manifest, str(compact.relative_to(ROOT.parent))], check=True)
    for path in paths:
        if sha(path) != manifest[path.relative_to(ROOT.parent).as_posix()]['sha256']:
            raise ValueError('window archive input changed during collection')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest))
    summary = dict(worker_exit_code=code, contracts_pass=code == 0, contract_count=len(MATRIX) if code == 0 else None,
                   nuclear_qualified=False, performance_qualified=False, release_qualified=False)
    (compact/'raw-archive.json').write_text(json.dumps(dict(**raw_record, **summary), indent=2)+'\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    result = dict(**summary, raw=raw_record,
                  compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=sha(packed)),
                  embedded_compact=compact.relative_to(ROOT.parent).as_posix())
    with receipt.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
