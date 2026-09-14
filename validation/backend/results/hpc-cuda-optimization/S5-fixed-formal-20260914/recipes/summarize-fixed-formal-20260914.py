"""Generate a compact table from archived qualified samples, never pilots."""
import argparse
import json
import math
from pathlib import Path
import statistics

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('directory', type=Path)
all_modules = ['diffusion_rkl1', 'diffusion_rkl2', 'burn_be_nr', 'burn_bd', 'burn_ros4']
all_modules += [f'coupled_{ode}_{diff}_all_transport'
                for ode in ('be_nr', 'bd', 'ros4') for diff in ('rkl1', 'rkl2')]
p.add_argument('--modules', nargs='+', choices=all_modules)
a = p.parse_args()
modules = a.modules or all_modules
assert len(set(modules)) == len(modules)
print('# 同一数学修复基线与 S5 候选：正式计时汇总\n')
print(f'本表覆盖 {len(modules)}／11 模块；仅汇总已完成的所列模块，不替代未列模块的验收。\n')
print('口径为 ARCH 启动到退出，含初末 I/O，不含事后数值资格检查；不是纯 kernel 或冷缓存计时。')
print('各配置一次预热、五次交替正式样本；CPU 基线取本轮 1/8/16 线程中最快的中位数。\n')
print('两侧均应用已验证的组分修复；原先失败的旧版 128 块结果不作速度分母。\n')
print('| 模块 / 块数 | 修复基线 GPU (s) | S5 GPU (s) | CPU1 (s) | CPU8 (s) | CPU16 (s) | 最快 CPU / GPU | 修复基线 / S5 GPU |')
print('|---|---:|---:|---:|---:|---:|---:|---:|')
files = []
total_lanes = total_comparisons = 0
for module in modules:
    path = a.directory / module / 'evidence/evidence.json'
    r = json.loads(path.read_text(encoding='utf-8'))
    assert r['status'] == 'passed' and not r['pilot'] and 'identity_error' not in r
    assert 'identities_after' in r and len(r['lanes']) == 108 and len(r['comparisons']) == 105
    assert all(row['status'] == 'passed' for row in r['lanes'])
    assert all(row['fields']['passed'] and row['fields']['status'] == 'pass'
               and row['workload_aligned'] for row in r['comparisons'])
    for version in ('baseline', 'candidate'):
        for section in ('artifacts', 'artifact_observation', 'source', 'build', 'execution_environment'):
            assert r['identities_before'][version][section] == r['identities_after'][version][section]
    assert {case['id'] for case in r['cases']} == {f'{module}_b{n}' for n in (8, 32, 128)}
    total_lanes += len(r['lanes'])
    total_comparisons += len(r['comparisons'])
    files.append(path.relative_to(a.directory).as_posix())
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
            assert statistics.pstdev(samples) == row['population_stdev']
            assert key not in medians
            medians[key] = median
        old = medians['baseline', 'cuda', 8]
        gpu = medians['candidate', 'cuda', 8]
        cpu = [medians['candidate', 'cpu', t] for t in (1, 8, 16)]
        values = [old, gpu, *cpu, min(cpu) / gpu, old / gpu]
        print(f'| {module} / {count} | ' + ' | '.join(f'{v:.6f}' for v in values) + ' |')
print('\n最快 CPU / GPU 大于 1 表示 GPU 更快；低于 1 的行保留，不作加速声明。')
assert total_lanes == len(modules)*108 and total_comparisons == len(modules)*105
print(f'全部 {total_lanes} 次运行、{total_comparisons} 次结果比较；样本范围、标准差、命令、物理参数、原始指标及身份在下列 JSON 中。\n')
for name in files:
    print(f'- [{name}]({name})')
