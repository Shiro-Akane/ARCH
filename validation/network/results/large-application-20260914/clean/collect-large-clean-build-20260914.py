"""Preserve the completed clean large build before any fixed-stage integration."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-large-application-20260914')
BASE = ROOT / 'build/p12-20260914/large-application-v2'
OUT = BASE / 'clean-evidence-v1'
sys.path.insert(0, str(ROOT / 'tools'))
import summarize_cuda_compile_memory as metrics
import validation_provenance as provenance

assert not OUT.exists()
assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == '32cc416220a69cad5ab12030ba7b26db360bcd22'
for name in ('artifacts.sha256', 'source-files.sha256', 'network-files.sha256', 'cudss.sha256', 'cublas.sha256'):
    subprocess.run(['sha256sum', '-c', str(BASE / name)], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
rows = []
for target in ('arch_cuda_backend', 'ARCH', 'arch_cuda_single_level_validation'):
    lines = (BASE / (target + '-child.log')).read_text().splitlines()
    assert not any(line.startswith('FAILED:') for line in lines)
    assert any('MEMORY_GUARD_RESULT' in line and 'guard_stopped=False' in line
               and 'stop_reason=none' in line for line in lines)
    for row in metrics.parse_commands(lines):
        assert row['exit_code'] == 0
        rows.append(dict(target=target, **row))
heavy = [row for row in rows if Path(row['translation_unit']).name.startswith('custom_audit')
         and Path(row['translation_unit']).suffix == '.cu']
expected = {f'custom_audit{n}_{eos}.cu' for n in (150, 200)
            for eos in ('idealgasview', 'helmeosview', 'tabular3deosview', 'tabular4deosview')}
assert len(heavy) == 8 and {Path(row['translation_unit']).name for row in heavy} == expected
identity = provenance.capture(source_root=ROOT, build_dir=BASE / 'release',
                              arch=BASE / 'release/bin/ARCH',
                              checkpoint_validator=BASE / 'release/arch_cuda_single_level_validation')
OUT.mkdir()
for path in BASE.iterdir():
    if path.is_file():
        shutil.copy2(path, OUT / path.name)
for name in ('CMakeCache.txt', 'compile_commands.json', 'build.ninja'):
    shutil.copy2(BASE / 'release' / name, OUT / name)
shutil.copy2(Path(__file__), OUT / Path(__file__).name)
record = dict(status='passed', scope='clean full Release build and linkage; not a numerical/runtime qualification',
              clean_build_complete=True, qualified_runtime=False,
              instrumented_cxx_cuda_compile_calls=len(rows),
              uninstrumented_scope='SuiteSparse C compiler calls have no per-command launcher metrics',
              original_source_commit=identity['source']['commit'],
              provenance=identity, heavy_routes=sorted(heavy, key=lambda r:r['translation_unit']),
              compile_metrics=rows,
              peak_command_rss_kib=max(row['peak_rss_kib'] for row in rows))
(OUT / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
with (OUT / 'loader.log').open('w') as stream:
    subprocess.run(['ldd', str(BASE / 'release/bin/ARCH')], stdout=stream,
                   stderr=subprocess.STDOUT, check=True)
assert 'not found' not in (OUT / 'loader.log').read_text()
archive = ROOT / 'build/large-clean-build-evidence-v1.tar.zst'
assert not archive.exists()
subprocess.run(['tar', '-I', 'zstd -T2 -3', '-cf', str(archive), '-C', str(BASE), OUT.name], check=True)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
print(json.dumps(dict(status='passed', instrumented_calls=len(rows),
                      peak_command_rss_kib=record['peak_command_rss_kib'], heavy=heavy,
                      archive=str(archive), bytes=archive.stat().st_size, sha256=digest), indent=2))
