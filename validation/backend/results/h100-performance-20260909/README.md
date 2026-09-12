# H100 性能对比与大网络历史记录

2026-09-12 状态更新：以下正文保留原 9 月 9 日观测与当时的进行中描述。
目前 focused 与完整 ARCH 构建均已退出 0，但旧续跑流程中断，不能据此宣称大网络验证通过。
当前恢复、复测和优化状态统一见 [S0 记录](../hpc-cuda-optimization/S0/README.md)。旧实验身份与失败记录不变。

本轮基线来自朋友 `Shiro-Akane/ARCH main` 的
`0266d96f20b184d4b17ebc6a066ac3b9021f1642`。本轮没有修改生产数学或后端实现。
原工作区和用户仓库的 main 均保留；本地使用独立分支
`codex/friend-main-performance-20260909`。

## 性能实验

复用维护目录的原始 `run_sedov_amr_timing.py`，不是另写一个简化算例：
2D Sedov、HLLC/PPM/RK3、两级 AMR、regrid 间隔 2、终止物理时间 0.02，
双方均设置 8 个 OpenMP 线程。两档规模各预热一次、正式运行三次，交替顺序，
端到端计时包含初始化和首尾输出，但不包含结果验证。

| 基础块数 | 朋友 CPU / CUDA 中位数 | 本服务器 CPU / CUDA 中位数 | 本服务器 CUDA / CPU |
|---|---|---|---|
| 4 × 4 | 12.011 / 39.670 s | 13.953 / 17.890 s | 1.282 |
| 8 × 8 | 54.186 / 185.632 s | 70.622 / 87.765 s | 1.243 |

全部 16 次运行（含预热）和 16 项跨后端/重复检查通过。小档 354 步、叶块
40 → 88、运行期拓扑变化 3 次；大档 711 步、叶块 88 → 208、拓扑变化 12 次。
最大绝对场差为 9.513e-24；近零场的相对误差不单独作为判据。
未使用性能阈值判定物理测试。
原场量预算 `rtol=1e-8, atol=1e-11`、守恒预算 `rtol=2e-12, atol=2e-10`
均保留；同物理时间、接受步数和逐次 regrid 工作量还须一致。

服务器为 H100-20C vGPU（20 GiB，不能当成完整裸机 H100）、Xeon Gold 6338
虚拟机，32 vCPU / 114 GiB RAM；8 线程不宣称为已确认的 8 个独占物理核。
使用 GCC 11.4 / CUDA 12.8 / SM90，朋友记录为 i7-10700 / RTX 3060 Ti、
GCC 12 / CUDA 12.3 / SM86。双方均为 Release，但不是只改变 GPU 的受控硬件实验。
朋友原记录的源码身份与当前 main 也分别保留，不能将其旧结果重标为新提交。
本轮 Sedov 构建关闭未被该算例使用的 KLU/cuDSS；大网络使用独立启用 provider 的构建。

完整 ARCH、CUDA archive 和同构建检查点比较器均已成功生成：1133.190 s，
内存保护器记录进程树峰值 RSS 6022376 KiB，无新增 swap、无 guard stop。
这是 Release 构建观测，不是 16 GiB Debug 构建的验收证据。

原始 [timing-report.json](timing-report.json) 保留各次时间、容差、物理输入、
checkpoint/源码/构建身份和 regrid 序列。运行期 regrid 事务中位数分别为
小档 CPU 1.559 s / GPU 1.571 s，大档 CPU 8.056 s / GPU 8.325 s。
GPU regrid 约占端到端时间的 8.8% / 9.5%；不能用它解释全部慢速，也不能把
端到端减去 regrid 后的余额称为纯 Hydro 时间。

独立 CUDA API 观测及按方向/大小细分的复测均通过检查点复核；其计时不进入
基准中位数。下面采用最终 [profile-report.json](profile-report.json)：

| API | 小档次数 / API 内耗时 | 大档次数 / API 内耗时 |
|---|---|---|
| cudaLaunchKernel | 599832 / 1.836 s | 2903079 / 8.645 s |
| cudaMemcpyAsync | 168360 / 6.746 s | 682938 / 33.007 s |
| cudaMemsetAsync | 523350 / 1.489 s | 2655170 / 7.282 s |
| cudaStreamSynchronize | 162209 / 1.462 s | 799439 / 7.570 s |
| cudaMalloc + cudaFree | 各 39504 / 合计 0.465 s | 各 176226 / 合计 0.941 s |

观测只包裹原 API，保持参数/返回值、不额外插入事件或同步。这些是 Host API
内的包含等待的耗时，不是 kernel duration，也不是可以全部删除的纯开销。
观测到的嵌套 API 次数为零，API 错误为零。详细观测进程总耗时为 18.482 / 88.135 s，
仅作诊断；不能把它们替换为未观测基准的数值。

**已确认的关键现象：小体积 GPU→CPU 回传占据大量 Host 等待。**

| 单次 ≤16 B 的 D2H 回传 | 小档 | 大档 |
|---|---|---|
| 次数 | 103486 | 522234 |
| 累计数据量 | 495736 B（0.473 MiB） | 2504712 B（2.389 MiB） |
| API 内耗时 | 6.052 s | 30.808 s |

对照：大于 16 B 的 H2D 上传累计约 868 MB / 3050 MB，API 内耗时仅 0.167 /
0.402 s。瓶颈不是仅由总拷贝字节量决定，也不能因分配次数多就优先更换分配器。
大档分配/释放合计不足 1 s，小回传约 30.8 s。

## 基于证据的下一步优化顺序（尚未实施）

1. **批量 CFL 归约与阶段状态回传。** `CudaBackendHydroControl.cpp` 的
   `compute_hydro_dt` 对每块回传 double/int 后同步，`execute_hydro_stage` 对每块
   回传 EOS 状态后同步；`Driver.h` 逐块调用。先在 Device 聚合全块结果，保留
   原共享 CFL 数学、确定性归约键、EOS 错误传播和 stage 发布边界；不能直接删检查。
   API 观测与这些调用点高度吻合，但其时间仍包含 GPU 执行/排队等待，不承诺全部消除。
2. **跨块批量 kernel / 清零。** 16×16 活跃单元的二维 face kernel 仅需约三个
   128-thread CTA；现有运行产生 60 万 / 290 万次 launch，以及大量独立 memset。
   用统一 block-view 数组批量执行，保留共享重构、通量和 AMR 数学及同样的拓扑。
3. **按 topology epoch 缓存 exchange plan 和工作区。** 现有边界路径反复构建
   map/vector/descriptor 并分配；缓存必须正确处理 slot rotation、species/layout
   身份和 regrid generation 退休。当前尚无独立 Host plan 构建计时，因此收益待测。

每个改动单独提交/复跑原对比，先验证字段、守恒、步数和 regrid 序列，再比较
三次中位数。当前不改库、物理公式、精度、容差、regrid 频率或测试输出口径。
Sedov 不启用 burn，所以 cuDSS 验证与这个 Hydro 加速比是两项独立工作。

## cuDSS 待验证项目

两个原始压力网络已生成：audit150（150 核素 / 1416 反应）、audit200
（200 核素 / 1965 反应）。独立环境使用 pynucastro 2.12.0、cuDSS 0.8.0.10，
项目原有 KLU 固定版本 v7.13.0；完整解析依赖版本在环境记录中。

服务器已通过 `cuda_cudss_sparse_solver`。这仅是 provider 测试；两个真实网络的
数学、BE_NR/BD/ROS4 轨迹、存储切换尚未执行。大网络编译已在全部性能观测结束后启动。
audit150 数学测试的 CUDA TU 已编译通过（1091.28 s，单次编译峰值 RSS
1848412 KiB），但尚不能据此宣布整个网络验证通过。
完整 ARCH/EOS 接入、长轨迹和大容量必须分别取得证据，不能用
小型 factory 测试代替；独立物理精度也不等同于 CPU/GPU 共享数学的一致性。

为覆盖容量项，仅扩展测试 harness 的存储/pool 参数，未改生产求解器。
默认 2/3 单元、误差预算和日志格式保留；九项校验器测试通过，包含原 audit31
存档兼容性和失败负例。服务器原有验证工具子集另有 73/73 通过。
四个测试文件的补丁暂存于服务器 build 目录，原版真实网络矩阵结束前不会覆盖源码。

一次性服务器续跑任务已启动，状态在
`build/large-network-20260909/campaign-status.json`。它核验原文件及暂存文件的
SHA-256，再在原版矩阵通过后应用测试扩展，重建 host harness，复跑默认矩阵，
然后检查 32/33、128/129 单元和 40 步长轨迹。单项有 2400 s 超时和内存保护；
某网络出现失败后不继续扩大它的负载。实际 pool 容量、lane 字节数和 GPU 内存
观测分开记录，不能把 requested pool 或 lane 字节数当成实测峰值。

另一份同提交、无源码改动的独立工作区正在构建含两个大网络及所有生产 EOS
路线的完整 ARCH。完整 archive/链接成功后，续跑任务将沿用维护中的 audit31/Helm
应用算例，仅替换为 audit150/audit200，保留三种 ODE、物理时间、步数和全部预算。
所有阶段都保留独立日志及 pass/fail；当前这些大网络门槛仍然是**待验收**。
续跑只执行这一次已安排的实验，不执行 Git commit/push，也不是周期任务。

设备安全检查目前有明确的平台阻塞：最初的 PyPI sanitizer 包缺少启动组件，
通过 agent-reach 的 Exa/Jina 路由查到并补齐
[NVIDIA 官方 12.8.93 完整包](https://developer.download.nvidia.com/compute/cuda/redist/cuda_sanitizer_api/linux-x86_64/cuda_sanitizer_api-linux-x86_64-12.8.93-archive.tar.xz)，
并用[官方清单](https://developer.download.nvidia.com/compute/cuda/redist/redistrib_12.8.1.json)
核对 SHA-256 `ae3574f052c0e06c95305962668eb1fe6ab571dfbb58b305fdb14d523bb1b240`。
工具现在能启动实际程序，但报告 **GPU debugging features are disabled**，
`ERROR SUMMARY: 1 error`。因此 memcheck/racecheck **未验收**，不能把工具同时打印的
`0 bytes leaked` 当成安全检查通过。未修改 vGPU/驱动的安全配置。

## 原始记录位置

服务器源码：`/home/ubuntu/projects/ARCH-perf-20260909`

- 性能构建、输入、原始输出及报告：`build/performance-20260909/`
- 大网络构建与记录：`build/large-network-20260909/`
- 隔离 Python/provider 环境：`build/network-python-20260909/`
- CMake 要求的外部生成网络根：`/home/ubuntu/projects/ARCH-large-networks-20260909`
- 独立完整应用源码：`/home/ubuntu/projects/ARCH-large-integration-20260909`
- 完整应用构建/结果：上述工作区的 `build/large-integration-20260909/`
- 一次性任务脚本：[continue_large_campaign.py](continue_large_campaign.py)
- 细分计划与边界：[large-network-plan.md](large-network-plan.md)

后续结果会更新本文件，并保存可重验的机器报告和原始日志；失败记录不会改成通过。

性能原始资料（全部 .par、CPU/GPU 标准输出、checkpoint、trace、regrid、编译
日志和配置、实际可执行文件、两轮观测及其各自的 observer 源码/二进制）已打包，
服务器和本地均保留 `build/performance-20260909-evidence.tar.gz`（约 108 MiB）。
两端 SHA-256 已核对相同：
`7eb438af5e5a47a7e15b069987e945808592150c1bbd76c5d8bc2a5b9c71648b`。
压缩包放在 Git 忽略的 build 目录；可审阅的 JSON/文本报告在本目录，不把二进制
大包直接塞入 Git 历史。本轮尚未 push，也未修改任何人的 main。
