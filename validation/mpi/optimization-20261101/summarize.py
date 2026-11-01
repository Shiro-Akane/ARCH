"""Summarize preserved MPI optimization evidence without changing raw data."""
from pathlib import Path
import json, statistics, hashlib

P = Path(__file__).resolve().parents[3]
E = Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
C = Path("/home/ubuntu/preservation/arch-mpi-optimization-20261101")
cpu = json.loads((E / "cpu-paired-summary.json").read_text())
expanded = json.loads((E / "expanded-summary.json").read_text())
cross = json.loads((E / "cross-summary.json").read_text())
assert cpu["complete"] and expanded["complete"] and cross["complete"]

def micro_summary(samples):
    output = {}
    for sample in samples:
        values = [row["seconds"] / row["iterations"] * 1e6 for row in sample["samples"]]
        output.setdefault(str(sample["ranks"]), {})[sample["version"]] = {
            "median_us": statistics.median(values), "min_us": min(values),
            "max_us": max(values), "n": len(values),
        }
    for values in output.values():
        values["speedup"] = values["before"]["median_us"] / values["after"]["median_us"]
    return output

def core_observation(case):
    steals, unaccounted = [], []
    for sample in case["samples"]:
        if sample["phase"] != "measured":
            continue
        for rank in sample["metrics"]["ranks"]:
            process = rank["process"]
            for name, before in process["core_ticks_before"].items():
                after = process["core_ticks_after"][name]
                delta = [b - a for a, b in zip(before, after)]
                tick = process["ticks_per_second"]
                steals.append(delta[7] / tick)
                busy = sum(delta[i] for i in (0, 1, 2, 5, 6)) / tick
                used = rank["resources"]["user_seconds"] + rank["resources"]["system_seconds"]
                unaccounted.append(max(0, busy - used))
    return {
        "max_rank_interval_steal_seconds": max(steals),
        "max_rank_interval_unattributed_busy_seconds": max(unaccounted),
        "note": "Unattributed busy time includes launch helpers/kernel work/other processes; not an exclusive-host certificate.",
    }

paired = []
for case in cpu["cases"]:
    rows = []
    for ranks, stats in case["statistics"].items():
        rows.append({
            "ranks": int(ranks), **stats,
            "after_change_percent": 100 * (stats["after"]["median"] / stats["before"]["median"] - 1),
        })
    paired.append({"name": case["name"], "root_shape": case["shape"], "steps": case["steps"], "rows": rows})
arrival = json.loads((E / "arrival-analysis.json").read_text())
expanded_rows = [
    {"name": case["name"], "root_shape": case["shape"], "steps": case["steps"],
     "statistics": case["statistics"], "core_observation": core_observation(case)}
    for case in expanded["cases"]
]
record = {
    "base_commit": "9d3914a68d6db72c06212f8c1a528d477087e97b",
    "candidate_release_sha256": hashlib.sha256((E / "bin/ARCH-after").read_bytes()).hexdigest(),
    "production_files": ["src/driver/DriverUtils.h"],
    "change": "Three typed CFL Allgathers replaced with one explicitly encoded 12-word uint64 Allgather; native reduction semantics unchanged.",
    "cpu_microbenchmarks": micro_summary(cpu["microbenchmarks"]),
    "expanded_microbenchmarks": micro_summary(expanded["microbenchmarks"]),
    "paired_application_results": paired,
    "arrival_analysis": arrival,
    "expanded_application_results": expanded_rows,
    "expanded_conditions": {
        "shared_host": True, "cores": expanded["allocated_cores"],
        "output_medium": expanded["output_medium"], "warmups": 1, "measured_repetitions": 5,
        "interpretation": "Within-run fixed-grid scaling only; not comparable as before/after to previous disk-output experiments.",
    },
    "validation": {
        "previous_suite_comparisons": 108, "expected_rejections": 20,
        "ctest_items": 10, "additional_8_16_rank_host_tests": 4, "two_host_unit_tests": 2,
        "cpu_paired_and_diagnostic_comparisons": 71, "expanded_comparisons": 60,
        "two_host_comparisons": 4, "total_saved_output_comparisons": 243,
    },
    "archive_verification": json.loads((E / "expanded-archive-verification.json").read_text()),
    "evidence_manifests": json.loads((C / "manifest-receipt.json").read_text()),
    "remaining_scope": "AMR compatibility is deferred; requested first two steps completed.",
}
D = P / "docs/development"
(D / "MPI-Communication-20261101.validation.json").write_text(json.dumps(record, indent=2) + "\n")

micro_table = ["| rank | 前态 (μs/调用) | 优化后 (μs/调用) | 调用加速比 |",
               "|---:|---:|---:|---:|"]
for ranks in ("2", "4"):
    row = record["cpu_microbenchmarks"][ranks]
    micro_table.append(f"| {ranks} | {row['before']['median_us']:.3f} | {row['after']['median_us']:.3f} | {row['speedup']:.3f} |")
paired_table = ["| 算例 | rank | 前态 (s) | 优化后 (s) | 耗时变化（正值为变慢） |",
                "|---|---:|---:|---:|---:|"]
for case in paired:
    for row in case["rows"]:
        paired_table.append(f"| {case['name']} | {row['ranks']} | {row['before']['median']:.6f} | {row['after']['median']:.6f} | {row['after_change_percent']:+.2f}% |")
scaling_table = ["| 算例 | rank | 中位数 (s) | 五次范围 (s) | CV | 对同网格单 rank 加速比 |",
                 "|---|---:|---:|---:|---:|---:|"]
for case in expanded_rows:
    for ranks, row in case["statistics"].items():
        scaling_table.append(f"| {case['name']} | {ranks} | {row['median']:.4f} | {row['min']:.4f}–{row['max']:.4f} | {100*row['cv']:.2f}% | {row['speedup']:.3f} |")
shares = [x["arrival_share"] for x in arrival["cases"]]
report = f"""# MPI 通信优化与扩展结果（2026-11-01）

用户要求的前两步已完成：定位 rank 等待的主要机制、扩大至 16 rank，并落实经过验证的通信优化。
仅修改 DriverUtils.h 的 MPI 数据传输部分；没有改数学库、物理模块、AMR 算法或 MemoryPool，
也没有增加 MPI 管理器、资源层或自定义归约操作。

## 为什么有较大的 MPI 等待

在空闲的 gen-989544（4 vCPU）上，固定二维网格与输入，正常/反向/轮转绑核各运行三次。
较大的等待没有稳定跟随一个 rank 或 CPU 核；不能把原来一次采样的差异当作固定分区瓶颈。

进一步记录同一主机 CLOCK_MONOTONIC 下的每次 Allgather 进入和退出时刻，
按调用序号对齐四个 rank，分解：
到达差 = 最晚进入 - 最早进入；
完成尾段 = 最晚退出 - 最晚进入。
四次诊断中，到达差占这些调用时间跨度之和的 {100*min(shares):.2f}%–{100*max(shares):.2f}%。
这表明观测到的主要时间花在等待其他 rank 到达；不是网卡带宽测量，
也没有进一步证明到达差具体来自虚拟机调度、缓存还是局部计算完成时刻。
首次调用贡献约 7%–57%，因此启动/输出影响只能解释一部分等待。

基于此证据，本轮保留 GhostExchange 的既有阻塞交换。
没有贸然加入计算重叠或非阻塞状态管理。

## 生产改动

每次 CFL 原本发送 value、逻辑键、计数，分为三次 Allgather 和三个接收 vector。
现在明确编码成十二个 uint64 word，一次 Allgather、一个接收 vector。
每 rank 的逻辑载荷仍为 96 字节，减少的是集合通信调用和临时分配次数。

double 与有符号键使用 bit_cast，避免浮点数值转换和 C++ padding；
原生 combine_state/finalize_reduction 与按 rank 顺序合并规则保持原样。
测试覆盖负零、NaN、正负无穷、空局部分区、全局空拒绝，
以及九个逻辑键分量分别决胜，包括负坐标和高位 Morton 编码。

## 归约微基准与完整模拟必须分开看

CPU 主机调用生产函数，预热 1000 次，每个样本连续调用 20000 次，共五次样本；
样本边界同步不计入循环时间，取所有 rank 的最大耗时。

""" + "\n".join(micro_table) + """

共享 32-vCPU 主机上，以相同核心映射重复前后对比，2/4/8/16 rank 的归约调用也均更快；
详细分布见 JSON。本轮没有声称整场模拟获得同样的加速比。

完整算例在空闲 CPU 主机交错运行 before/after，每版本预热一次、五次正式重复：
二维 16×8 根块、64 步；三维 8×2×2 根块、32 步。
主指标仍为 max_rank(driver_seconds-output_seconds)，包含驱动内通信与等待。
以下是中位数，不删除任何较慢样本。

""" + "\n".join(paired_table) + """

两进程完整模拟出现约 0.4%–0.7% 的小幅变慢；四进程变化较小且部分样本波动明显。
因此不能宣称本轮稳定提高了这些完整算例的总速度。
保留这项改动的理由是已验证的归约调用成本下降、两次多余集合调用和临时分配被移除，
同时位级数值契约完全保留。它针对通信本身，不能替代后续对计算负载与调度的研究。

## 扩大进程规模

gpu-273312 上使用 0–9、16、17、18、20、22、23 这些核心，覆盖 1/2/4/8/16 rank。
每组一次预热、五次重复；二维 32×8 根块（65,536 cells）、64 步，
三维 32×1×1 根块（131,072 cells）、32 步。
两台服务器 CPU 型号相同，但虚拟机配置不同，本节主机仍有其他任务。

其他项目下载使该节点磁盘低于原预留值，故本节所有输出统一写服务器内存盘，
每次验证后完整无损压缩并直接保存到 CPU 服务器。
压缩和传输发生在应用计时之后；此处所有 rank 使用相同输出介质。
这些数字是本轮共享主机条件下的固定网格扩展结果，
不能与上一轮磁盘输出直接作优化前后对比，也不能外推为独占节点或 32 rank 结果。

""" + "\n".join(scaling_table) + """

进程绑定、每核 busy/steal 计数和其他进程快照均保存。
计数不能证明 CPU 核被独占；存在波动的样本仍全部保留。
部分加速比高于理想线性值时，不据此宣称超线性算法收益。

## 正确性验收与保存材料

- Debug/Release 原有应用比较共 108 组、预期拒绝 20 项全部通过。
- CTest 共 10 项；额外 Debug/Release 8/16 rank host 测试 4 项；双机 host 测试 2 项通过。
- CPU 前后配对与等待诊断 71 组、扩展测量 60 组、优化后双机 4 组全部逐位一致。
- 合计 243 组保存输出比较，检查字段、坐标、测度、时刻与分片完整性。
  此数包含前态对照和预热，不代表 243 个独立物理算例，也不代表保存了每个内部状态。
- 新增多 rank 诊断确认 Allgather 次数精确由 3×步数变为步数；
  既有 Sendrecv 次数保持不变，数学表达式和归约语义不变。
- 候选 Release SHA256：""" + record["candidate_release_sha256"] + """。

完整原始材料保存在两台服务器的
/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101。
扩展测量的每次完整目录保存在 CPU 主机 expanded-archives 下，
expanded-archive-index.json 记录压缩包和每个原始成员文件的 SHA256。
归档逐成员验证通过后，才清理本轮服务器内存盘的临时副本。
临时传输授权撤销；不下载源码、二进制或实验目录到 Windows，不新增 LFS 数据。

文件清单与最终提交回执位于各自服务器的
/home/ubuntu/preservation/arch-mpi-optimization-20261101。
机器可读结果包含清单 SHA256、归档核验统计和全部测量边界。

本轮止于已验证的 MPI 通信优化。现有 AMR 的 MPI 兼容支持仍位于计划最后，尚未实施。
[后续计划](MPI-NextSteps-20261101.zh-CN.md)；
[机器可读结果](MPI-Communication-20261101.validation.json)；
[复现协议](../../validation/mpi/optimization-20261101/README.md)。
"""
(D / "MPI-Communication-20261101.zh-CN.md").write_text(report)
print(json.dumps({"comparisons": 243, "report": str(D / "MPI-Communication-20261101.zh-CN.md")}))
