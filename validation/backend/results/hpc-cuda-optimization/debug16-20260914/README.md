# S5 内置 CUDA：16 GiB 受限 Debug 构建

完整 `arch_cuda_backend` archive 和 `ARCH` 链接通过，串行 `--parallel 1`。
这是 **S5 指标候选、尚未包含后续组分修复** 的构建证据，不是整个优化项目或运行时安全验收。

## 资源与范围

独立 transient user scope 强制 `memory.max=17179869184`、`memory.swap.max=0`；
只限制本次构建进程，没有改变服务器全局配置或停止他人任务。
开始／结束的 `memory.events` 所有计数均为 0，结束时 `memory.swap.current=0`。
这证明受限于 16 GiB 且无 swap 的构建可完成，不冒充在物理 16 GiB 机器上实测。

Linux 5.15 未提供 `memory.peak`，故 cgroup 总峰值记为 unavailable/null，不能填 0。
以下为 `/usr/bin/time` 的单调用 RSS，并非所有进程同时驻留量之和。

| 项目 | 最大 RSS（KiB） | 当次墙钟 |
|---|---:|---:|
| backend 完整构建 | 4,004,472 | 57:58.75 |
| ARCH 目标构建／链接 | 1,245,948 | 2:18.43 |
| 最重 TU：Tabular4 Hydro | 4,004,472 | 941.75 s |
| Tabular4 + aprox21 | 2,842,648 | 204.05 s |
| Tabular4 + aprox19 | 2,518,840 | 202.12 s |

期间服务器还运行其他独立构建和数值任务，墙钟不作为独占编译性能基准。
78 次编译调用全部成功；原完整命令、逐 TU 指标和目标级日志见 `evidence/`。

实际配置为 Debug、NVCC 12.8.93、GCC 11.4、sm90，沿用现有 Debug ptxas `-O1` 和严格浮点。
内置全部 EOS/network 路线；原 P0 协议的 KLU/cuDSS/custom network 为 OFF。
没有删去失败 TU，也不以此宣称 150/200 自定义网络的 Debug 已通过。
首次预检因 `memory.peak` 不可用而退出，尚未启动编译；其脚本和失败日志亦保留。

## 身份、原始包与本地备份

真实 checkout 为 `32cc416220a69cad5ab12030ba7b26db360bcd22` 加记录的 S5 overlay；
不以文档提交号替代实际源码身份。补丁、全源文件 SHA、CMakeCache、完整命令及产物 SHA 均在 `evidence/`。
二进制和 archive 原物保存在服务器，同时纳入以下已下载且双端 SHA 一致的原始包：

- 完整包 `debug16-build-v1.tar.zst`：126,740,428 bytes；SHA-256 `923bce4ba28d34ee2772308b0e60af3b62a888ad20ca9f355c9e7ae76dbac221`。
- compact `debug16-build-compact-v1.tar.zst`：84,700 bytes；SHA-256 `21f9feb0cb27e5c66f066a4360e1df128b6405b081ca619d67ddd3c1c7a8f514`。
- 服务器目录 `/home/ubuntu/projects/ARCH-s5-indicator-20260914/build/`；本地目录 `C:/tmp/ARCH-perf-20260909/build/`。

未用本次构建解除 vGPU 对 compute-sanitizer 的调试限制，也未把后续源码修改自动算作已验证。
