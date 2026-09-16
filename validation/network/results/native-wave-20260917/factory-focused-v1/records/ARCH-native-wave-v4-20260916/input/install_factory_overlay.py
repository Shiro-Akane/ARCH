"""Install a qualified private overlay into the explicitly new detached tree."""
import hashlib
import json
from pathlib import Path
import shutil
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
for name, expected in shared['sha256'].items():
    if sha(source / name) != expected:
        raise ValueError('canonical shared math changed: ' + name)
if sha(source / 'src/cuda/microphysics/SparseOdeBatch.cuh') != 'dc7d736d6845019e0537a0327bba57433be3b13771dbf8aa7bea9c814dbf179f':
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
