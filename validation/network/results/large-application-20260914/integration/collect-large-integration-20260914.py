"""Capture the completed incremental integration without altering runtime inputs."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-large-application-20260914')
BASE = ROOT / 'build/p12-20260914/large-application-v2'
SOURCE = BASE / 'fixed-integration-v1'
OUT = BASE / 'integration-evidence-v1'
BUILD = BASE / 'release'
assert not OUT.exists()
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from summarize_cuda_compile_memory import parse_commands

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024*1024):
            h.update(chunk)
    return h.hexdigest()
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == '8da9b23d5597c2c8bd91d9dfa07b971d2bb91bef'
for path in ('artifacts.sha256', 'source-files.sha256', 'large-objects-after.sha256'):
    subprocess.run(['sha256sum','-c',str(SOURCE/path)],cwd=ROOT,stdout=subprocess.DEVNULL,check=True)
before = (SOURCE/'large-objects-before.sha256').read_text()
after = (SOURCE/'large-objects-after.sha256').read_text()
assert before == after and len(before.splitlines()) == 8
commands = parse_commands((SOURCE/'build-child.log').read_text().splitlines())
assert len(commands) == 34 and all(row['exit_code'] == 0 for row in commands)
guard = (SOURCE/'build-guard.log').read_text().splitlines()
assert any('MEMORY_GUARD_RESULT' in line and 'guard_stopped=False' in line
           and 'stop_reason=none' in line for line in guard)
OUT.mkdir()
for path in SOURCE.iterdir():
    if path.is_file() and path.suffix != '.zst':
        shutil.copy2(path, OUT/path.name)
for name in ('CMakeCache.txt','compile_commands.json','build.ninja'):
    shutil.copy2(BUILD/name, OUT/name)
record = dict(status='passed', scope='original clean build plus recorded incremental integration; not another clean build or runtime pass',
    source_commit='8da9b23d5597c2c8bd91d9dfa07b971d2bb91bef', clean_base='32cc416220a69cad5ab12030ba7b26db360bcd22',
    actual_compile_calls=len(commands), generated_large_objects_unchanged=8,
    max_command_rss_kib=max(row['peak_rss_kib'] for row in commands),
    commands=commands, resource_guard_lines=guard,
    provenance=provenance.capture(source_root=ROOT,build_dir=BUILD,arch=BUILD/'bin/ARCH',checkpoint_validator=BUILD/'arch_cuda_single_level_validation'),
    original_product_backup=dict(path=str(SOURCE/'base-products.tar.zst'),bytes=(SOURCE/'base-products.tar.zst').stat().st_size,sha256=sha(SOURCE/'base-products.tar.zst')))
(OUT/'summary.json').write_text(json.dumps(record,indent=2,default=str)+'\n')
shutil.copy2(Path(__file__),OUT/Path(__file__).name)
archive=ROOT/'build/large-integration-evidence-v1.tar.zst'
assert not archive.exists()
subprocess.run(['tar','--use-compress-program=zstd -T2 -3','-cf',str(archive),'-C',str(BASE),OUT.name],check=True)
print(json.dumps(dict(status='passed',compile_calls=len(commands),max_command_rss_kib=record['max_command_rss_kib'],archive=str(archive),bytes=archive.stat().st_size,sha256=sha(archive)),indent=2))
