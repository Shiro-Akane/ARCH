"""Generate a compact table from archived qualified samples, never pilots."""
import argparse
import json
import math
from pathlib import Path
import statistics

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
a = p.parse_args()
print('# 燃烧 / 扩散正式计时汇总\n')
print('口径为 ARCH 启动到退出，含初末 I/O，不含事后数值资格检查；不是纯 kernel 或冷缓存计时。')
print('各配置一次预热、五次交替正式样本；CPU 基线取本轮 1/8/16 线程中最快的中位数。\n')
print('| 模块 / 块数 | 旧 GPU (s) | 新 GPU (s) | CPU1 (s) | CPU8 (s) | CPU16 (s) | 最快 CPU / GPU | 旧 GPU / 新 GPU |')
print('|---|---:|---:|---:|---:|---:|---:|---:|')
files = []
for module in ('diffusion_rkl1', 'diffusion_rkl2', 'burn_be_nr', 'burn_bd', 'burn_ros4'):
    path = a.directory / ('formal-' + module + '-v1.json')
    r = json.loads(path.read_text())
    assert r['status'] == 'passed' and not r['pilot'] and 'identity_error' not in r
    assert 'identities_after' in r and len(r['lanes']) == 108 and len(r['comparisons']) == 105
    assert all(row['status'] == 'passed' for row in r['lanes'])
    files.append(path.name)
    for count in (8, 32, 128):
        name = f'{module}_b{count}'
        medians = {}
        for row in (v for v in r['statistics'] if v['case'] == name):
            key = row['version'], row['backend'], row['threads']
            samples = [v['arch_wall_seconds'] for v in r['lanes'] if
                       (v['case'], v['version'], v['backend'], v['threads'], v['phase']) ==
                       (name, *key, 'measured')]
            warmups = [v for v in r['lanes'] if
                       (v['case'], v['version'], v['backend'], v['threads'], v['phase']) ==
                       (name, *key, 'warmup')]
            assert len(samples) == 5 and len(warmups) == 1 and samples == row['samples']
            assert all(math.isfinite(v) and v > 0 for v in samples)
            median = statistics.median(samples)
            assert median == row['median'] and min(samples) == row['minimum'] and max(samples) == row['maximum']
            assert key not in medians
            medians[key] = median
        old = medians['baseline', 'cuda', 8]
        gpu = medians['candidate', 'cuda', 8]
        cpu = [medians['candidate', 'cpu', t] for t in (1, 8, 16)]
        values = [old, gpu, *cpu, min(cpu) / gpu, old / gpu]
        print(f'| {module} / {count} | ' + ' | '.join(f'{v:.6f}' for v in values) + ' |')
print('\n最快 CPU / GPU 大于 1 表示 GPU 更快；低于 1 的行保留，不作加速声明。')
print('全部 540 次运行、525 次结果比较；样本范围、标准差、命令、物理参数、原始指标及身份在下列 JSON 中。\n')
for name in files:
    print(f'- [{name}](timing/{name})')
