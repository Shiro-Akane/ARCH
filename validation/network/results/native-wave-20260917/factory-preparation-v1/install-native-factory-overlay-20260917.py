"""Install a qualified private overlay into the explicitly new detached tree."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve(strict=True)
if root != Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916'):
    raise ValueError('unexpected root')
source = root / 'source'
overlay = root / 'factory-overlay'
manifest = json.loads((overlay / 'integration-record.json').read_text())
if manifest['status'] != 'prepared_factory_overlay_not_built_not_ODE_validated' or manifest['pending'] is not None:
    raise ValueError('incomplete overlay')
shared = json.loads((root / 'input/shared-inputs.json').read_text())
def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
normalizations = {}
for name, expected in shared['sha256'].items():
    original = (source / name).read_bytes()
    payload = (overlay / name).read_bytes()
    committed = subprocess.check_output(['git', '-C', str(source), 'show', '8da9b23d5597c2c8bd91d9dfa07b971d2bb91bef:' + name])
    if original != committed or hashlib.sha256(payload).hexdigest() != expected or original.replace(b'\r\n', b'\n') != payload.replace(b'\r\n', b'\n'):
        raise ValueError('canonical shared math changed beyond line endings: ' + name)
    normalizations[name] = dict(canonical_raw_sha256=sha(source / name), tested_payload_raw_sha256=expected)
if hashlib.sha256((source / 'src/cuda/microphysics/SparseOdeBatch.cuh').read_bytes().replace(b'\r\n', b'\n')).hexdigest() != '60ddf06c3a2731e299913ebb86e98b5ac1af83bb0e5dd213f39c7369119866e2':
    raise ValueError('canonical scheduler changed')
for name, expected in manifest['files'].items():
    p = overlay / name
    if p.stat().st_size != expected['bytes'] or sha(p) != expected['sha256']:
        raise ValueError('overlay changed')
for name, expected in manifest['files'].items():
    dest = source / name
    if not dest.resolve().is_relative_to(source.resolve()) or dest.is_symlink():
        raise ValueError('escaped destination')
    shutil.copyfile(overlay / name, dest)
    if sha(dest) != expected['sha256']:
        raise ValueError('installation mismatch')
print('ISOLATED_FACTORY_OVERLAY_INSTALLED_NOT_BUILT files=' + str(len(manifest['files'])))
print(json.dumps(dict(shared_math_line_endings_only=normalizations), indent=2))
