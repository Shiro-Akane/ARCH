# S1–S4 服务器数值验证结果

本轮开始于 2026-09-13。用户允许同步 S4 到独立目录构建、验证；随后明确要求先完成数值验证，性能计时稍后再做。

- 被测源码：`c44a183c03a1692388ec56e3fdfec1ab3e412221`，生产代码最后变更为 S4 `1a850c35`。
- 服务器：GPU-273312，H100-20C vGPU，20 GiB；CUDA 12.8.93、GCC 11.4。
- 独立源码目录：`/home/ubuntu/projects/ARCH-hpc-s4-validation-20260913`，detached checkout；旧实验目录和双方 main 未修改。
- 构建与原始输出：该目录下 `build/s4-validation-20260913/`。
- 本轮集成及所列数值阶段通过，共 121 组 checkpoint 对比；安全检查受 vGPU 平台限制，性能计时按用户要求未运行。不是完整安全验收或正式 Release，没有新的加速比。S5 不在本轮范围。

## 构建口径

Release、sm_90、OpenMP；使用旧性能基准相同的 GCC/NVCC、HDF5、HighFive 和严格浮点选项。KLU/cuDSS 均关闭，与无 burn 的 Sedov 性能基准保持一致，不代表删去内置 burn/EOS 编译单元。

构建全程 `--parallel 1`。禁用 ccache 以记录实际编译，不把缓存命中当编译成本。每条 C++/CUDA 编译命令通过 GNU time 记录 max RSS；完整 archive/应用构建同时使用项目原有内存守护器（保留 16 GiB 可用内存，swap 增量最多 256 MiB，启用压力保护）。单命令 max RSS 与进程树采样 RSS 分开解释。本机 114 GiB RAM 的 Release 构建不能证明 16 GiB 机器的 clean Debug 资格。

## 已有结果

| 检查 | 实际状态 |
|---|---|
| 初始版本和数据 | 双端固定分支及初始 clean checkout 为 c44a183c；LFS 展开、EOS 表哈希保存；后续仅修正一个测试文件，完整补丁见 evidence/test-fixture-fix.patch |
| 架构审计 | 退出码 0 |
| Linux 工具测试 | 320 项，319 通过、1 跳过；跳过的是未安装可选 mold 的链接器检查，默认链接器 LTO 检查通过 |
| 聚焦编译 | IdealGas Hydro、AMR exchange、Hydro Host control 三个对象均成功；总计约 70.8 秒，无 swap 增长 |
| 完整 CUDA archive | 成功，所有内置 EOS/network TU 均保留；串行守护构建 3630.971 秒，进程组采样峰值 RSS 4144196 KiB（约 3.95 GiB），swap 未增长，未触发保护停止 |
| ARCH 和验证程序 | 编译/链接成功；串行守护构建 237.847 秒，进程组采样峰值 RSS 1465564 KiB，无 swap 增长或保护停止；新旧运行库解析路径相同 |
| 首轮 24 项合同/设备测试 | 23 通过、1 失败：多块 Hydro 的 EOS 负例未实际触发非有限 EOS 返回值；原失败记录保留 |
| 修正后合同/设备测试 | 第一次复跑 24/24、38.95 秒；最终 LF 源码重新编译后再次 24/24、36.84 秒，均无跳过项 |
| 单块/整波/尾波应用控制 | 5/5 用例通过；10 组 checkpoint 对比，实际块数为 1/1024/1025，层级固定为 0 |
| 原 Cartesian AMR 矩阵 | 10/10 用例、27 组 CPU/GPU checkpoint 对比通过 |
| 原曲线坐标矩阵 | 24/24 用例、48 组 CPU/GPU checkpoint 对比通过；包含柱/球坐标、1D/2D/3D、扩散及 Hydro–扩散耦合 |
| 三维动态 regrid | 1/5/10/20/40/41/80 步的 7 组对比通过；20→40 细化、40→41 粗化、41→80 再细化，完整 8 子块关系由原检查器确认 |
| 持续复用 | Hydro 观察点延至 100/500 步、RKL2 五 stage 延至 25/100 步；两项共 11 组对比通过，原输入和预算不变 |
| Restart | SmoothAdvection、BurnGradient 各 12 次运行及 9 组对比，共 18 组通过；覆盖 CPU↔GPU 和同后端、两种 checkpoint 相位及 ENUC 连续性 |
| Compute Sanitizer | 实际 memcheck 返回 86，明确报告 GPU debugging features are disabled；安全未验收，后续 racecheck 未执行 |
| 性能 | 用户明确要求先完成数值验证，计时稍后再做；本轮不运行 pilot、线程扫描或 API profiling |

聚焦编译的逐命令最高 RSS 约 775 MiB。原始日志及机器可读表见 `evidence/focused-build.log`、`evidence/focused-build.time`、`evidence/focused-compile-memory.csv`。

完整 archive 的最重已完成单命令为 Tabular4D Hydro：820.73 秒、max RSS 4118132 KiB（约 3.93 GiB）。此数字与上表进程组采样峰值的采样口径不同，不混用。系统 I/O full 压力曾短时达到 55.74%，但没有持续触发停止，memory full 为 0，swap 输入/输出增量为 0。

## 执行顺序与证据要求

1. 完整 CUDA archive、ARCH 与本轮测试目标编译/链接，保留第一条失败命令；不跳过失败 TU。
2. 24 项聚焦合同/设备测试：包含真实 GPU、多块 Hydro/扩散/burn、AMR exchange、regrid migration/transaction、资源退休、EOS 错误和六项 Host 合同。CTest skip 不算通过。
3. 补充 1/1024/1025 块单层应用控制，验证整波/尾波及 Euler/RK2/RK3；继承原比较和守恒预算，检查实际 checkpoint 块数。它们不替换原 AMR 或性能算例。
4. 原 Cartesian AMR 十项矩阵、受影响的曲线坐标矩阵、持续 refine/coarsen/re-refine，以及 smooth/burn 四方向 restart；不调整原容差和物理输入。
5. 对真实新 backend 运行现有 Compute Sanitizer。vGPU 若仍禁止调试，则单列环境受阻，不能宣称内存安全通过。
6. 后续空闲窗口再执行原两档 Sedov CPU8/GPU pilot 与 1/2/4/8/16 线程扫描；本轮按用户最新要求暂停这一项。正式方案仍为每档 1 次 warmup 和 5 次测量，计时与编译、其他作业、profiling 分离，同轮新 CPU 对新 GPU，旧 GPU/新 GPU 比值另列。

本轮调度脚本位于服务器 `build/s4-validation-20260913/verify_s4.py`。每个 phase 需要全新的输出目录，保存完整命令、stdout/stderr、源码/二进制身份及最终状态；全部科学判断委托项目现有验证器。

## 首轮失败与最小修正

首轮 CTest 在 35.55 秒内完成，23/24 通过。`cuda_multiblock_hydro` 原始报错为 `Hydro batch hid the failing block EOS status`；仅增强诊断后定位到 Euler、块 701、CFL interior，实际没有抛异常。

原因是测试向 IdealGas 注入 NaN 能量，但原有共享 `max(0, pressure)` 会返回有限压力；该负例并未建立它想验证的 EOS 失败。仅修改 `test_cuda_multiblock_hydro.cu`，使用能穿过原压力下限的正无穷能量，并先断言共享 EOS 叶函数确实返回非有限压力。保留块号匹配、错误后清除旧 latch 和全部原检查，增加失败路径诊断；未改生产代码、物理公式、依赖或容差。

修正后的单独 GPU 测试及完整 24 项均通过，覆盖三种积分器、首末块、CFL/ghost stage 及恢复复用。服务器测试源码相对 c44a183c 的这一补丁由后续 provenance 单独记录；ARCH、archive 和 checkpoint validator 本身未重编，身份不重标为新生产代码。复跑后只将 Windows 传输带入的测试文件 CRLF 换行格式归一为 Linux LF，并归档精确的 19 行补丁；后续数值阶段记录归一后的源码身份。

最终又按 LF 文件重编测试并重跑完整 24 项，见 `contracts-final/`。架构审计最终退出码 0，工具测试最终为 320 项、319 通过/1 跳过，见 `evidence/*-final.log`。这一步没有改变 ARCH 或 backend archive 的哈希。

## 数值口径与限制

121 组对比分别为尾波 10、Cartesian 27、曲线坐标 48、三维动态 7、持续复用 11、restart 18。全部使用既有比较器和各算例原有容差；新增尾波输入和延长观察点不替换原矩阵。CPU/CUDA 共享数学一致性不等于独立物理精度验证。

`max_field_normalized` 的定义是绝对差除以该字段的 checkpoint 峰值，**不是误差预算占比**。例如柱坐标三维 RKL1 第 5 步该值为 0.632836，但该 checkpoint 的最大绝对差仅 5.551115123125783e-16；比较器按 `atol + rtol * field_peak` 判定，原 `atol=5e-12, rtol=1e-8` 未改变。近零字段的相对/峰值归一化差不能单独解释成精度失败，也不把通过描述为逐位一致。

服务器上其他作业仍在运行。曲线矩阵末段与隔离目录中的 restart、生命周期数值检查有部分重叠；运行时间和 trace 中的 wall time 仅为日志，不能作为公平性能实验。没有终止其他作业，没有启动 S5。

memcheck 原始日志保存在 `sanitizer/memcheck-arch_cuda_multiblock_hydro/sanitizer.log`：`ERROR SUMMARY: 1 error` 来自 vGPU 禁止 GPU 调试。工具同时打印的 `0 bytes leaked` 不构成内存安全通过。未绕过平台配置；完整 memcheck/racecheck、精确设备内存峰值、16 GiB clean Debug、S4 公平线程扫描仍待后续验收。本轮未重跑大网络/cuDSS 专项。

## 产物身份与证据

| 产物 | SHA-256 |
|---|---|
| ARCH | a7b085c7c29f5b942b3b2d1110982a8df18d88c49de19100f39fde5794cfcd96 |
| checkpoint validator | 2a3492b170f00fbd196659e1f83c8e84e5c45a483790831fcbee0a48b5913f85 |
| CUDA archive | b54d8d9c8a8dc8082e304fda221a918709fba92ea82a9668abe950519e5e85e4 |
| 最终多块 Hydro 测试 | b7d97a72327a1b6f6bad879ebd28e2af94acdcf396ab8e617c722aad626cce8a |

各 phase 的 `status.json`、原始 JSON 报告、`.par`、命令日志及重放脚本随本目录进入 Git；首轮失败和后续成功均保留。大型 checkpoint 与完整 build/binary 的压缩归档保存于服务器和本地 ignored build 目录，传输后核对 SHA-256。归档回执另附，不将数据包写入普通 Git blob。双方固定分支仍为 `codex/hpc-cuda-optimization`；不改两个 main，不创建验收 tag。

完整备份现已落在本地 `C:/tmp/ARCH-perf-20260909/build/s4-validation-20260913-evidence.tar.zst`，服务器保留同名归档及原始 build 树。整包 1,236,911,980 字节，SHA-256 为 `ffa2287b0e61c5b9f7090138add48e52b41bc454ba07db872103d1b421b0a493`；10 个分片和合并后整包均已在本地验证。解压后的完整 tar 为 8,018,995,200 字节，与原 gzip 包的解压内容哈希完全相同。旧 gzip 本地片段已明确保留为 `.partial`，不是完整备份。详见 [归档回执](evidence/archive-receipt.json) 和 [分片清单](evidence/archive-parts.sha256)。请解压到新的空目录，勿覆盖既有验证现场。

本目录的局部 `.gitattributes` 对原始证据禁用换行转换；541 个原始文件已逐一核对 Git 暂存 blob 与下载字节一致。这只约束本轮证据，不修改全项目源码换行规则。源码修改和本报告的最终发布提交可晚于被测 c44a183c；不得据此重标被测二进制的生产源码身份。

## 暂记工程观察

IdealGas 和 Tabular3D Hydro 对象文件分别由旧版约 4.29/7.94 MB 增至 8.45/15.73 MB，约两倍。新旧入口实例化的编译成本值得后续单独处理；对象文件体积不是运行 VRAM 或速度。当前不据此改物理公式、删入口或改变优化编译选项。
