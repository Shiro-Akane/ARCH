"""Bind staged S5 sources to the exact overlay and successful-build manifest."""
import hashlib
from pathlib import Path
import subprocess
import tarfile

archive = Path('build/s5-indicator-overlay-v1.tar')
assert hashlib.sha256(archive.read_bytes()).hexdigest() == '0e967f94b5cfb67eff60c339ef1d3fd6d79dd15c94bec45aa901064da52c9dc7'
manifest = Path('validation/backend/results/hpc-cuda-optimization/S5-indicators-20260914/build/source-files.sha256')
entries = dict(line.split('  ', 1)[::-1] for line in manifest.read_text().splitlines())
expected = {'src/cuda/amr/RefinementIndicators.cu', 'src/cuda/amr/RefinementIndicators.h',
    'src/cuda/runtime/amr/CudaBackendIndicators.cpp', 'src/cuda/runtime/control/CudaBackendInternal.h',
    'tests/cuda/test_cuda_multiblock_hydro.cu', 'tests/cuda/test_refinement_indicators.cpp'}
with tarfile.open(archive) as bundle:
    files = [m for m in bundle.getmembers() if m.isfile()]
    assert {m.name for m in files} == expected
    for member in files:
        raw = bundle.extractfile(member).read()
        assert hashlib.sha256(raw).hexdigest() == entries[member.name], member.name
        blob = subprocess.check_output(['git', 'show', ':' + member.name])
        assert blob == raw.replace(b'\r\n', b'\n'), member.name
        print('S5_STAGED_SOURCE_MATCH', member.name, 'LF-normalized' if blob != raw else 'exact-bytes')
print('S5_INDEX_BUILD_OVERLAY_MATCH_PASS')
