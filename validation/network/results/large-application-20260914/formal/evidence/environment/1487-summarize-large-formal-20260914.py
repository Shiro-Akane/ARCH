"""Recompute a six-case table only from a completed formal record."""
import argparse
import importlib.util
import json
from pathlib import Path

spec=importlib.util.spec_from_file_location('archive_formal',Path(__file__).with_name('archive-formal-timing-20260914.py'))
archive=importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('evidence',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
r=json.loads(a.evidence.read_text(encoding='utf-8'))
archive.check_report(r,True)
for section in ('source','build','artifacts','artifact_observation','execution_environment'):
    assert r['identity_before'][section]==r['identity_after'][section]
assert all(row['metadata']['step']==20 and row['metadata']['time']==1e-10 for row in r['lanes'])
assert all(row['trajectory']['max_species_evolution']>1e-9 for row in r['lanes'])
lines=['# 150／200 核素完整应用：正式计时','',
    '同一合格 ARCH 二进制、真实 Helm EOS；CPU 使用 KLU，CUDA 使用 cuDSS。',
    '服务器为 Xeon Gold 6338 的 32 vCPU 虚拟机与 H100-20C／20 GiB vGPU；这是分配到的虚拟 GPU，不是完整独占 H100 整卡。',
    '固定原输入：1D、2 个初始块、32 个物理单元，20 宏步至 1e-10；不外推至大规模全网格。',
    '每配置一次预热、五次交替正式样本。计时为程序启动到退出，含初末输出，不含事后资格检查；不是纯 kernel／稳态／冷 OS 缓存计时。','',
    '| 网络／ODE | CPU1 (s) | CPU8 (s) | CPU16 (s) | CUDA / Host8 (s) | 最快实测 CPU / CUDA | CPU8 / CUDA |',
    '|---|---:|---:|---:|---:|---:|---:|']
for case in r['cases']:
    name=case['id']
    values={(v['backend'],v['threads']):v['median'] for v in r['statistics'] if v['case']==name}
    assert set(values)=={('cpu',1),('cpu',8),('cpu',16),('cuda',8)}
    cpu=[values['cpu',t] for t in (1,8,16)]
    gpu=values['cuda',8]
    label=case['plan_policy']['network'].replace('custom:audit','')+' / '+case['plan_policy']['ode'].upper()
    lines.append('| '+label+' | '+' | '.join(f'{v:.6f}' for v in [*cpu,gpu,min(cpu)/gpu,cpu[1]/gpu])+' |')
lines += ['',
    '比值大于 1 表示 CUDA 更快；小于 1 表示 CUDA 更慢。最快 CPU 仅指本轮 1／8／16 线程中的最快中位数。',
    '全部 144 次运行、138 次字段及宏步／regrid 工作量比较通过，前后身份不变。每条轨迹均检查实际组分演化、finite、metadata、边界及原闭合预算；没有按零燃烧的快路径计时。',
    '这里的通过不消除非均匀 32／33 单元 BE 长轨迹的原 1800 秒超时，也不构成独立弱反应能量／反应率 oracle 或 sanitizer 资格。',
    '', '## 样本离散性','',
    '| 算例 | 配置 | 五次样本 (s) | 最小–最大 (s) | 总体标准差 (s) |',
    '|---|---|---|---|---:|']
for row in r['statistics']:
    lines.append('| '+row['case']+' | '+row['backend']+' t'+str(row['threads'])+' | '+
        ', '.join(f'{v:.6f}' for v in row['samples'])+' | '+f'{row["minimum"]:.6f}–{row["maximum"]:.6f} | {row["population_stdev"]:.6f} |')
assert not a.output.exists()
a.output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('LARGE_FORMAL_SUMMARY_PASS',str(a.output))
