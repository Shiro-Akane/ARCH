"""Summarize the complete S5 formal matrix; never promote pilots or partial runs."""
import argparse
import json
import math
from pathlib import Path
import statistics

MODULES = ('diffusion_rkl1', 'diffusion_rkl2', 'burn_be_nr', 'burn_bd', 'burn_ros4') + tuple(
    f'coupled_{method}_{order}_all_transport'
    for method in ('be_nr', 'bd', 'ros4') for order in ('rkl1', 'rkl2'))
KEYS = {('baseline', 'cpu', 8), ('baseline', 'cuda', 8),
        ('candidate', 'cuda', 8), *(('candidate', 'cpu', t) for t in (1, 8, 16))}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def rows(directory):
    results = []
    for module in MODULES:
        report = json.loads((directory / f'formal-{module}-v1.json').read_text())
        require(report['status'] == 'passed' and not report['pilot']
                and 'identity_error' not in report and 'identities_after' in report,
                'unqualified formal report: ' + module)
        require(len(report['lanes']) == 108 and len(report['comparisons']) == 105
                and all(r['status'] == 'passed' for r in report['lanes']),
                'incomplete formal matrix: ' + module)
        require(all(r['workload_aligned'] and r['fields']['passed'] for r in report['comparisons']),
                'unqualified work/numerical comparison: ' + module)
        for count in (8, 32, 128):
            name = f'{module}_b{count}'
            medians = {}
            for row in (r for r in report['statistics'] if r['case'] == name):
                key = row['version'], row['backend'], row['threads']
                selected = [r for r in report['lanes'] if
                    (r['case'], r['version'], r['backend'], r['threads']) == (name, *key)]
                samples = [r['arch_wall_seconds'] for r in selected if r['phase'] == 'measured']
                require(len(samples) == 5 and sum(r['phase'] == 'warmup' for r in selected) == 1
                        and samples == row['samples'] and key not in medians
                        and all(math.isfinite(v) and v > 0 for v in samples), 'invalid sample inventory')
                value = statistics.median(samples)
                require(value == row['median'] and min(samples) == row['minimum']
                        and max(samples) == row['maximum']
                        and statistics.pstdev(samples) == row['population_stdev'], 'inconsistent summary statistics')
                medians[key] = value
            require(set(medians) == KEYS, 'missing or unexpected timing configuration')
            old = medians['baseline', 'cuda', 8]
            gpu = medians['candidate', 'cuda', 8]
            cpu = [medians['candidate', 'cpu', t] for t in (1, 8, 16)]
            results.append((name, [old, gpu, *cpu, min(cpu)/gpu, old/gpu]))
    return results


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    args = p.parse_args()
    qualified = rows(args.directory)  # Fail before printing a misleading partial success table.
    print('# S5 全物理正式计时汇总\n')
    print('ARCH 启动到退出，包含初末 I/O；不是纯 kernel、冷缓存或已认证稳态计时。')
    print('旧 GPU 是已经通过数值回归并完成性能测试的燃烧／扩散联合批量版本，不是优化前的 S4。')
    print('每配置一次预热、五次交替正式样本；最快 CPU 取本轮 1/8/16 线程中位数的最小值。\n')
    print('| 模块 / 初始块数 | 旧 GPU (s) | 新 GPU (s) | CPU1 (s) | CPU8 (s) | CPU16 (s) | 最快 CPU / GPU | 旧 GPU / 新 GPU |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|')
    for name, values in qualified:
        print('| ' + name + ' | ' + ' | '.join(f'{v:.6f}' for v in values) + ' |')
    print('\n最快 CPU / GPU 大于 1 表示 GPU 更快；低于 1 的行保留。耦合行包含真实动态 AMR，不能将初始块数当最终工作量。')
    print('共 1188 次运行、1155 次数值／工作量比较；保留原始样本及其波动，不据单次快样本宣称收益。')
