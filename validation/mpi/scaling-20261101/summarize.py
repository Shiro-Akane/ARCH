"""Generate a compact result record from preserved server-side measurements."""
import json
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[3]
EVIDENCE = Path("/home/ubuntu/projects/ARCH-mpi-evidence-scaling-20261101")
CONTROL = Path("/home/ubuntu/preservation/arch-mpi-scaling-20261101")
cpu = json.loads((EVIDENCE / "cpu-summary.json").read_text())
cross = json.loads((EVIDENCE / "cross-summary.json").read_text())
receipt = json.loads((CONTROL / "manifest-receipt.json").read_text())
assert cpu["complete"] and cross["complete"]

cases = []
for case in cpu["cases"]:
    profiles = []
    for sample in case["samples"]:
        if sample["phase"] != "profile":
            continue
        ranks = []
        for rank in sample["metrics"]["ranks"]:
            observation = rank["mpi_profile"]
            mpi_seconds = sum(item["seconds"] for item in observation["operations"].values())
            ranks.append({
                "rank": rank["rank"],
                "mpi_seconds": mpi_seconds,
                "driver_nonoutput_seconds": rank["driver_nonoutput_seconds"],
                "operations": observation["operations"],
            })
        profiles.append({"ranks": sample["ranks"], "rank_measurements": ranks})
    cases.append({
        "case": case["name"],
        "root_shape": case["shape"],
        "steps": case["steps"],
        "cells": case["samples"][0]["comparison"]["cells"],
        "comparisons": len(case["samples"]),
        "statistics": case["statistics"],
        "diagnostic_profiles": profiles,
    })

record = {
    "candidate_commit": "b94b187ddf9ae8c9c5ab014b745647f2a07bb062",
    "baseline_commit": "5dc6d7bad3fcf37c1aa261d66ec1e8dcf6c5e21d",
    "candidate_binary_sha256": "4753d151ba64a7f36989b285647fa43447cf386430fb8fd9c8ac9e3c1e8e5f0b",
    "performance_host": "gen-989544 / 150.65.181.189",
    "protocol": cpu["protocol"],
    "cases": cases,
    "cpu_comparisons": 42,
    "cross_comparisons": [
        {key: case[key] for key in ("name", "shape", "steps", "ranks", "hosts", "comparison")}
        for case in cross["cases"]
    ],
    "cross_host_tests": cross["host_tests"],
    "cpu_audit": {
        key: value for key, value in json.loads((EVIDENCE / "cpu-audit.json").read_text()).items()
        if key != "details"
    },
    "environment_comparison": json.loads((EVIDENCE / "environment-comparison.json").read_text()),
    "evidence": receipt,
    "production_code_changed_this_round": False,
    "temporary_transport_authorization_revoked": True,
    "limitations": [
        "Only 4-vCPU single-host strong scaling; no 32-core scaling result.",
        "Two-host jobs establish correctness, not network performance.",
        "Saved initial/final outputs are bitwise checked; internal steps are not all serialized.",
        "PMPI samples include waiting; logical payload bytes are not wire traffic.",
        "Summed per-rank peak RSS is not simultaneous aggregate peak.",
        "Existing AMR compatibility remains the last planned phase, not implemented here.",
    ],
}
destination = REPOSITORY / "docs/development"
(destination / "MPI-Scaling-20261101.validation.json").write_text(json.dumps(record, indent=2) + "\n")

table = ["| 算例 | rank | 时间中位数 (s) | 五次范围 (s) | CV | 加速比 | 效率 | rank 峰值 RSS 合计 (MiB) |",
         "|---|---:|---:|---:|---:|---:|---:|---:|"]
for case in cases:
    for rank, stats in case["statistics"].items():
        table.append(
            f"| {case['case']} | {rank} | {stats['median_seconds']:.4f} | "
            f"{stats['min_seconds']:.4f}–{stats['max_seconds']:.4f} | {100*stats['cv']:.2f}% | "
            f"{stats['speedup']:.3f} | {100*stats['parallel_efficiency']:.1f}% | "
            f"{stats['median_sum_rank_peak_rss_mib']:.2f} |"
        )
report = """# MPI 单机扩展与双机正确性结果（2026-11-01）

本轮完成 4-vCPU CPU 服务器上的固定网格强扩展测量和两台服务器的真实 MPI 验证。
四进程相对单进程：二维加速 2.968 倍，三维加速 3.393 倍。
单机 42 组、双机 4 组保存输出与独立 main 串行基线逐位一致；双机 2/4 rank host 单元测试通过。
这些结论限定于下述算例、主机及计时口径。

## 实现与环境

- 候选实现 b94b187；独立串行基线 main 5dc6d7b。使用此前验证的 Release 二进制，不重新改变求解算法。
- 正式性能主机 gen-989544 / 150.65.181.189：4 vCPU，Xeon Gold 6338，约 57 GiB 内存。
- 双机另一端 gpu-273312 / 150.65.181.188：32 vCPU，同一 CPU 型号，约 114 GiB 内存。
  两台虚拟机资源配置不同；GPU 主机有其他训练与排队任务，故只用于短时正确性验证。
- Open MPI 5.0.11。两端 MPI、PRRTE、PMIx、HDF5 和 libstdc++ 动态库 SHA256 一致。
  候选程序 SHA256：4753d151ba64a7f36989b285647fa43447cf386430fb8fd9c8ac9e3c1e8e5f0b。
- 每 rank 绑定不同 CPU 核，OMP/BLAS 线程数为 1；实际进程亲和性与退出码全部检查通过。
- 服务器旧源码保持原样：CPU 旧工作区仍为干净的 29972a1；当前验证程序独立保存于证据目录。

## 协议与数值验证

二维平滑波：16×8 个根块，32,768 cells，112 步。
三维平滑波：8×2×2 个根块，131,072 cells，32 步。
两者均为固定 Cartesian 网格、HLLC/MUSCL/RK3、周期边界、纯流体 CPU 计算。
8 步 pilot 用于选步数；正式 1/2/4 rank 每组预热一次并重复五次，运行顺序预先交错。
额外的 PMPI 诊断每个进程数各一次，不参与正式统计。

每次保存初始和最终快照，合并各 rank 分片并验证网格无重复或缺失，
按全局坐标重排后逐位比较保存字段、坐标、单元测度及时间。
42 组单机比较包括 6 组预热、30 组正式重复、6 组通信诊断。
全部 46 次单机启动（另含 2 个 pilot、2 个基线）的实际结束步数与计划一致。
二维、三维的最大密度变化分别为 0.02761175、0.01578684，确认场有实际演化。

## 单机性能

主指标 = max_rank(driver_seconds - output_seconds)。
该区间包含驱动内的计算、通信、等待和少量设置，不能称为纯计算时间。
速度以同网格 MPI-ON 单 rank 为分母；CPU 阶段计时另存于机器可读结果。
串行和单 rank 强制写最终 checkpoint，多 rank 不写，因此端到端时间不用于此处加速比。

""" + "\n".join(table) + """

CV 是五次样本标准差除以均值。RSS 列是每次各 rank 峰值之和的五次中位数，
不是同时发生的总内存峰值，也不能当作网格内存池本身的占用。
二维双进程的波动达到 6.64%，保留全部样本，未删除所谓离群值。

## 通信观察与下一步决定

四进程诊断中，二维各 rank 的 MPI 调用墙钟约 0.015–0.446 s，
三维约 0.027–0.398 s，存在明显差异。主网格分区块数相等；
这些时间同时包含同步等待，当前证据不能把差异归因于网络带宽。
每 rank 的 Allgather 调用次数为 3×步数，Sendrecv 为 6×(步数+1)，与现有 RK3/CFL 路径一致；
单 rank 均为零。诊断前后保存输出继续逐位一致。

本轮保留已有阻塞交换实现。下一步先在安静资源窗口检查绑核/调度与 rank 等待分布，
再决定是否需要批量传输或非阻塞重叠；不凭单次 MPI 墙钟比例增加抽象层。
32 核扩展尚未执行，不能从本次 4-vCPU 结果推断 32 核或集群性能。

## 两台服务器验证

使用实际两个主机运行 2 rank（每机 1 个）与 4 rank（每机 2 个）。
Open MPI 通过所选 150.65.181.0/24 网络使用 TCP；记录实际主机名和绑核。
MPI probe 验证 rank 总和，已有 mpi_fixed_grid 单元测试验证原生交换与归约。

二维 SmoothAdvection：5×3 根块、3,840 cells、8 步、周期边界；
三维 Sedov：5×3×2 根块、122,880 cells、8 步、outflow。
四组应用比较全部与独立串行 main 逐位一致，包含 Nx=5 对 2/4 rank 的不均匀分区。
这些作业只支持跨主机正确性结论；没有据此报告网络加速比或带宽。

初期 rankfile 参数、SSH 动态库环境以及输入配置校验失败单独保留在证据目录，
未进入通过数量或性能样本。最后使用成功验证的 ppr 映射；
系统 SSH 启动时清除工具链 LD_LIBRARY_PATH，MPI 进程继续使用一致工具链。
临时服务器间密钥及 CPU 授权已撤销，其他项目任务与用户密钥保持原样。

## 原始证据与范围

两台服务器分别保存同名目录：
/home/ubuntu/projects/ARCH-mpi-evidence-scaling-20261101。
CPU 主机保存完整单机原始数据；GPU 主机保存跨机器完整分片、两端环境以及单机摘要/哈希清单。
只在服务器之间传输，没有下载源码、数据、二进制或完整日志到 Windows。
文件清单位于各自服务器的
/home/ubuntu/preservation/arch-mpi-scaling-20261101/evidence-manifest.json。

""" + "\n".join(
    f"- {host}：{value['files']} 文件、{value['bytes']} 字节；清单 SHA256 {value['sha256']}。"
    for host, value in receipt.items()
) + """

清单不包含其自身；CPU 证据目录中的清单副本也显式排除，避免循环哈希。
完整 CPU 样本数据约 1.038 GiB，在本轮 1.5 GiB 预算内。
Git 只保存脚本、文档和小型 JSON；本轮没有新增 LFS 数据文件。

本轮生产代码零改动。与 main 核对，数学库、物理、transfer/refinement/storage 均无差异。
MPI 继续沿用现有内存池与 GhostExchange，不另建 MPI 资源管理层。
对现有 AMR 的 MPI 支持位于计划最后，本轮没有实现 AMR 新功能。

[后续计划](MPI-NextSteps-20261101.zh-CN.md)；
[机器可读结果](MPI-Scaling-20261101.validation.json)；
[复现协议](../../validation/mpi/scaling-20261101/README.md)。
"""
(destination / "MPI-Scaling-20261101.zh-CN.md").write_text(report)
print(json.dumps({"cases": len(cases), "comparisons": 46, "report": str(destination / "MPI-Scaling-20261101.zh-CN.md")}))
