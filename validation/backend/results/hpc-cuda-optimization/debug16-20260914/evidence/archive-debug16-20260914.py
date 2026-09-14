"""Archive completed bounded Debug evidence and exact linked artifacts."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path('/home/ubuntu/projects/ARCH-s5-indicator-20260914')
ORIGINAL=Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
source=ROOT/'build/debug16-20260914-v2'
out=ROOT/'build/debug16-evidence-v1'
compact=out/'compact'
compact.mkdir(parents=True,exist_ok=False)
def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while chunk:=stream.read(1024*1024): h.update(chunk)
    return h.hexdigest()
subprocess.run(['sha256sum','-c',str(source/'artifacts.sha256')],cwd=ROOT,check=True)
subprocess.run(['sha256sum','-c',str(source/'source-files.sha256')],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
for path in source.iterdir():
    if path.is_file(): shutil.copy2(path,compact/path.name)
for name in ('CMakeCache.txt','compile_commands.json','build.ninja'):
    shutil.copy2(source/'debug'/name,compact/name)
for name in ('replay-s5-debug16-20260914.sh','replay-s5-debug16-preflight-v1.sh','debug16-preflight-v1.log'):
    shutil.copy2(ORIGINAL/'build/p12-20260914'/name,compact/name)
shutil.copy2(Path(__file__),compact/'archive-debug16-20260914.py')
events={}
for which in ('before','after'):
    events[which]={k:int(v) for k,v in (line.split() for line in (source/f'{which}-memory.events').read_text().splitlines())}
    assert int((source/f'{which}-memory.max').read_text())==16*1024**3
    assert int((source/f'{which}-memory.swap.max').read_text())==0
    assert all(value==0 for value in events[which].values())
assert int((source/'after-memory.swap.current').read_text())==0
assert 'unavailable' in (source/'after-memory.peak').read_text()
rows=[]
targets=[]
for name in ('arch_cuda_backend','ARCH'):
    log=(source/f'{name}.log').read_text()
    assert '\tExit status: 0' in log and not re.search(r'^FAILED:',log,re.M)
    target_rss=re.findall(r'Maximum resident set size \(kbytes\): (\d+)',log)
    wall=re.findall(r'Elapsed \(wall clock\) time \(h:mm:ss or m:ss\): ([\d:.]+)',log)
    assert len(target_rss)==len(wall)==1
    targets.append(dict(target=name,peak_rss_kib=int(target_rss[0]),elapsed=wall[0],exit_code=0))
    for match in re.finditer(r'^ARCH_COMPILE_METRIC elapsed_seconds=([\d.]+) peak_rss_kib=(\d+) exit_code=(\d+) command=(.*)$',log,re.M):
        seconds,rss,rc,command=match.groups()
        assert int(rc)==0
        rows.append(dict(target=name,elapsed_seconds=float(seconds),peak_rss_kib=int(rss),exit_code=int(rc),command=command))
assert rows and any('Tabular4D' in row['command'] and 'Aprox' in row['command'] for row in rows)
report=dict(status='passed',scope='clean serial builtin CUDA Debug archive and ARCH executable under a dedicated cgroup; not a physical 16-GiB machine and not runtime timing',
    source_head=(source/'source-head.txt').read_text().strip(),
    source_overlay_sha256=sha(source/'source-tracked.patch'),
    source_scope='S5 indicator candidate before the subsequent shared composition fixes',
    memory_max_bytes=16*1024**3,swap_max_bytes=0,swap_current_after_bytes=0,
    aggregate_cgroup_peak_bytes=None,aggregate_peak_reason='memory.peak unavailable on Linux 5.15',
    events=events,targets=targets,compile_calls=len(rows),
    compile_metrics=sorted(rows,key=lambda row:row['peak_rss_kib'],reverse=True))
(compact/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
with (compact/'hardware-after-build.log').open('w') as stream:
    for command in (['uname','-a'],['nvidia-smi'],['/home/ubuntu/projects/.envs/arch/bin/nvcc','--version'],['/usr/bin/g++-11','--version']):
        stream.write('COMMAND '+repr(command)+'\n'); stream.flush()
        subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,check=True)
files=sorted(p for p in compact.rglob('*') if p.is_file())
(compact/'compact-files.sha256').write_text(''.join(f'{sha(p)}  {p.relative_to(compact)}\n' for p in files))
raw=ROOT/'build/debug16-build-v1.tar.zst'
small=ROOT/'build/debug16-build-compact-v1.tar.zst'
assert not raw.exists() and not small.exists()
subprocess.run(['tar','-I','zstd -T2 -3','-cf',str(raw),'-C',str(ROOT),
    str(compact.relative_to(ROOT)),str((source/'debug/bin/ARCH').relative_to(ROOT)),
    str((source/'debug/libarch_cuda_backend.a').relative_to(ROOT))],check=True)
subprocess.run(['tar','-I','zstd -T2 -3','-cf',str(small),'-C',str(out),'compact'],check=True)
archives=[dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p)) for p in (raw,small)]
(out/'archives.json').write_text(json.dumps(archives,indent=2)+'\n')
print(json.dumps(dict(summary={k:v for k,v in report.items() if k!='compile_metrics'},top_compile=report['compile_metrics'][:5],archives=archives),indent=2))
print('DEBUG16_ARCHIVE_PASS')
