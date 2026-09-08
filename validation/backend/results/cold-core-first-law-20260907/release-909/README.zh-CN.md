# 保留优化的核心构建参考

英文原文：[README.md](README.md)。英文版是规范文本。

[五阶段记录](evidence.json)已通过候选源码
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`
的冷构建、无改动、增量与并发对照检查。这项结果确认受测构建任务通过；
整体发布和运行时容量仍需单独验收。

## 范围与配置

受测目标是 `ARCH`，包含内置网络、生成的 `audit31` 与 `weak_urca` 路径、
CPU KLU、CUDA cuDSS 及所需依赖。虽然配置启用了测试，测试可执行文件不属于
本次计时目标。冷构建前，280 个已配置编译输出均不存在；这个数字用于确认冷态，
并不表示全部 280 个输出都属于 `ARCH`。

参考树与独立测量树均使用 Ninja、Release、OpenMP、CUDA 架构 86 和 GCC 12
宿主编译器，并禁用编译缓存。重型任务池始终为 2，总并发上限为 4，只有串行
对照使用总并发 1。每阶段前后，完整的规范化 `ARCH` 命令与重型任务池分配均
与参考构建一致，保留原有 `-O3` 和 LTO 优化链接。

## 测量结果

时间包含构建命令和内存保护工具的开销。内存列统一使用 KiB。自有进程 RSS 是
保护工具对本次进程树采样的总量，不是最低内存要求；可用内存与 swap 是
Linux／WSL 系统级观测值。

| 阶段 | 总并发 | 耗时（s） | 自有进程峰值 RSS | 最低可用内存 | 初始 → 峰值 swap |
|---|---:|---:|---:|---:|---:|
| `ARCH` 冷构建 | 4 | 2213.445 | 3763288 | 3439116 | 125264 → 202600 |
| 无改动 | 4 | 1.012 | 5760 | 7125000 | 184680 → 184680 |
| Ideal/iso7 路径重编译及链接 | 4 | 27.323 | 1093016 | 6515332 | 184424 → 184424 |
| Ideal/iso7 + Helm/iso7 串行重编译及链接 | 1 | 45.320 | 1035936 | 6538696 | 184424 → 184424 |
| 同一对路径并行重编译及链接 | 4 | 31.312 | 1018000 | 6491080 | 184424 → 184424 |

五个阶段均正常结束，未触发保护停止。冷构建约耗时 36.9 分钟，自有进程峰值
RSS 为 3.589 GiB，观测到的 swap 增长为 75.5 MiB。JSON 记录还保留了每条编译
命令的耗时和最大 RSS。

增量测量只重编译一个紧凑的生成分发路径，不代表修改共同数学头文件后所有依赖
项的重建时间。双路径对照在两个并发设置下完成相同工作，均包含未改变的最终
优化链接，耗时下降 30.9%。这个结果适用于受测路径组合，不能推广为其他编译
单元的加速保证，也不证明其他并发数不可能更快。

重型任务 2、总并发 4 是实测的中等配置参考。可用内存较少时降低并发数，
大内存系统可另行测量更高并发；运行时网格与网络容量单独验收。

## 复现

按[网络准备步骤](../../../../network/README.zh-CN.md#复现这些记录)生成 `audit31` 和
`weak_urca`，然后配置参考构建树与全新的独立测量树，两者使用相同的编译器、
优化、求解库、网络包和重型任务池配置。为每棵树设置独立的
`ARCH_RUNTIME_OUTPUT_DIRECTORY`。先构建参考树的 `ARCH`，不要构建测量树中的
任何目标。[测量脚本](../replay.py)会拒绝已有编译输出或命令不一致的构建。

为收集每条命令的 RSS，两棵树都需要使用包含以下内容的同一份 CMake 项目
include 文件。它只包装编译命令，不改变数值优化标志：

```cmake
set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE
    "/usr/bin/time -f 'ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C'")
```

配置两棵树时，将这个文件的绝对路径传给 `CMAKE_PROJECT_INCLUDE`。若两棵树
分别命名为 `build/core-reference` 与 `build/core-cold`，从仓库根目录执行：

```bash
CCACHE_DISABLE=1 OMP_NUM_THREADS=4 python3 -B \
  validation/backend/results/cold-core-first-law-20260907/replay.py \
  --reference-build build/core-reference --build-dir build/core-cold \
  --incremental-source build/core-cold/generated/cuda_dense_burn/ideal_iso7.cu \
  --comparison-source build/core-cold/generated/cuda_dense_burn/helm_iso7.cu \
  --output-dir validation/backend/results/cold-core-first-law-20260907/my-run \
  --jobs 4 --min-available-mib 1536 --max-swap-growth-mib 256
```

脚本为每个阶段调用已有的内存与压力保护工具，仅改变指定构建生成文件的时间戳。
请使用新的结果目录，测量期间不要同时运行其他重型编译或 GPU 验收。
[完整命令](preflight/measurement/normalized-commands.txt)和[evidence.json](evidence.json)
保留实际测量配置及依赖身份。
