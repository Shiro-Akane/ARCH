# ARCH：HPC＋CUDA 性能优化计划表

日期：2026-09-12。状态：S0 实施中，已采纳返送意见。第 9 节修订优先于前文旧约定；实际进度见 [S0 记录](../../validation/backend/results/hpc-cuda-optimization/S0/README.md)。生产优化尚未开始。

## 1. 结论与范围

朋友提出的是聊天中的方向建议，并非一份已经确定的技术方案。本计划将它解释为：在现有 C++／OpenMP／CUDA 架构中，优化任务粒度、数据驻留、同步、内存复用和 CPU 调度。

**建议采用这个方向，但优先优化现有执行层，不另建物理实现，不默认迁移框架。第一轮是“少回传、少等待、批量执行”，不是先换库或重写 kernel 数学。**

已检查朋友最新 main：`7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866`。相对实测基线 `0266d96f20b184d4b17ebc6a066ac3b9021f1642`，更新为文档／授权说明；本次核对的 `src/`、`cmake/`、`CMakeLists.txt`、`tests/`、`tools/`、`cases/` 没有差异。旧实验仍保留旧提交身份，不能改标为新提交结果。

HPC 不是一个需要安装的底层库。当前已有 Host 管理 AMR、Device 执行计算的基础；先把这套基础用好，再由测量决定是否需要更大架构变化。

## 2. 为什么按这个顺序做

来自[原始性能报告](../../validation/backend/results/h100-performance-20260909/timing-report.json)和[独立 API 观测](../../validation/backend/results/h100-performance-20260909/profile-report.json)：

| 观测 | 小档 Sedov：4×4 基础块 | 大档 Sedov：8×8 基础块 | 对计划的影响 |
|---|---:|---:|---|
| CPU 8 线程端到端中位数 | 13.953 s | 70.622 s | 同构建、同输入的比较基准 |
| CUDA 端到端中位数 | 17.890 s | 87.765 s | CUDA 分别慢约 28.2%／24.3% |
| ≤16 B 的 GPU→CPU 回传次数 | 103,486 | 522,234 | 优先批量归约／状态回传 |
| 上述回传累计数据量 | 0.473 MiB | 2.389 MiB | 不能把问题简单归因于传输总量 |
| 上述回传 API 内耗时 | 6.052 s | 30.808 s | 很多时间花在调用及其等待路径上 |
| kernel launch 次数 | 599,832 | 2,903,079 | 跨块批量执行具有直接测量依据 |
| malloc＋free API 内合计耗时 | 0.465 s | 0.941 s | 分配次数虽多，不应抢在小回传之前 |
| GPU regrid 事务时间／端到端时间 | 约 8.8% | 约 9.5% | 不能将全部慢速归因于 AMR 重划分 |

API 内耗时含等待，不是纯 kernel 时间，也不是可以全部消除的开销；不能将这几项直接相加推算理论加速比。当前对“提交／同步瓶颈”的判断有实测和调用点支持，但 GPU 执行时间、Host 调度时间还需进一步拆分。

服务器是 H100-20C **vGPU**、20 GiB 显存、32 vCPU；不能当成完整裸机 H100，8 线程也不等于已确认的 8 个独占物理核。朋友使用 RTX 3060 Ti／i7-10700，两台机器的绝对时间不构成只改变 GPU 的受控实验。

当前代码已经按字段提供 Device views；不应把“重新改成 SoA”当作未经检查的默认任务。更直接的问题是每次提交处理的块数太少，以及结果消费点过密。

## 3. 中文实施计划表

下列为建议顺序，全部尚待实施。每一项单独形成可开关、可回退的改动及验证记录；“目标”不是已取得的收益。

| 编号／优先级 | 工作内容 | 主要改动位置 | 完成与验收标准 | 前置条件 |
|---|---|---|---|---|
| A0／P0 | 冻结基线、补齐计时与续跑状态 | 验证脚本、运行记录、Host 调度计时 | 保留原两档结果；明确构建、汇总、测试三个状态；可恢复已编译产物；计时能区分初始化、推进、regrid、I/O，且区间不重复计入 | 无；只做最小诊断与工具修复 |
| A1／P0 | 批量 CFL 归约和 Hydro EOS 状态回传 | `Driver.h`、`CudaBackendHydroControl.cpp`、后端批量接口 | 对应控制量从逐块下载／等待变为每次全局 CFL 查询或阶段完成边界下载／等待；跨块归约保留确定性键、无效值处理和 EOS 错误传播；完整通过原对比 | A0 |
| A2／P0 | Hydro、边界和清零跨块批量执行 | CUDA Hydro／boundary launch 层、Device block-view 列表 | 同一路线、布局与阶段的多块共享一次或少量 launch；kernel 数不再因逐块 Host 调用等比例增长；不遗漏尾批、空批和粗细界面；reflux 时序不变 | A1 的完成／错误语义稳定；可先只批量一个算子 |
| A3／P1 | 缓存 AMR exchange 计划、复用工作区 | `Driver.h` 中计划构造、CUDA exchange runtime、资源 owner | 拓扑不变时复用拓扑计划；slot 每次正确重绑定；跨 refine／derefine、restart、species/layout 变化不复用旧 handle；稳态路径不反复重建相同计划和 scratch | A0；与 A2 协调接口，分别验收 |
| A4／P1 | 有界缓冲池和少量 pinned staging | `DeviceAllocation.h` 的调用方、backend 资源 owner | 优先复用已有分配；需要时再试 `cudaMallocAsync`；锁页缓冲只覆盖必要回传，记录容量和寿命；反复 regrid／故障退出无增长或悬空访问 | A1、A3；分配及拷贝仍有可测成本 |
| A5／P2 | 对稳定拓扑区间试用 CUDA Graph | 后端提交层、Graph owner／失效规则 | 只捕获无需 Host 中途决策的设备子图；更新 dt／slot 参数；拓扑或资源世代变化时验证、更新或重建；包含建图成本后仍改善端到端时间；保留普通 CUDA 路径 | A1–A4 已形成稳定边界，且 launch 开销仍显著 |
| B1／P1 | CPU 端 HPC 调度和线程布局 | Host block 遍历、计划构造、OpenMP 路径 | 线程数 1／2／4／8／16 扫描，记录亲和性／可见 NUMA；消除重复 map/vector 构造；只对合法独立工作并行化；CPU 与 CUDA 均用优化后的公平基线重测 | A0；不可与正式 GPU 计时争用机器 |
| B2／P2 | kernel 内存访问、寄存器和计算优化 | 实测热点 kernel 及共享数学调用方式 | 有 profiler 证据后才调整线程块、访存、局部 scratch、融合范围；记录寄存器／spill／带宽等可用指标；不能只凭 GPU 利用率决定改动 | A2 后重新定位热点；有相应 profiling 权限 |
| C1／独立 P1 | 收尾大网络验证与编译组织 | cuDSS 验证流程、逐 TU 构建记录 | 先恢复已有 audit150／audit200 产物的验证；区分编译耗时、内存和真实运行问题；再分析重复实例化与拆分；不减少核素、删除 EOS 路线或改反应数学 | 不阻塞已通过的无 burn Sedov 优化；不能当作已通过 |

第一轮建议只交付 **A0＋A1＋A2**，A3 可分开跟进。A4／A5 必须根据第一轮复测后的剩余热点决定，不能因为名称“更底层”就直接提前。

## 4. 实现时必须保住的边界

### A1：合并控制量，不删除错误检查

当前[时间步／阶段路径](../../src/cuda/runtime/hydro/CudaBackendHydroControl.cpp)逐块下载 `double/int` 后 `quiesce()`，[Driver](../../src/driver/Driver.h)逐块调用这些接口。建议分成“提交一批 → Device 归约 → 在必需的 Host 消费点完成一次等待”。

必须保留同一套 CFL 数学、NaN／无效状态处理、空集合语义及确定性排序。异步化后不能把“已排入 stream”标记为“已经完成”：只有获得正确的 completion/fence 证据后，才允许发布下一阶段、使用新状态或回收资源。发生 EOS 错误时，不得继续推进或写出正常 checkpoint。

批量小传输、减少 Host/Device 往返符合 NVIDIA 的建议；真正异步拷贝还要满足 pinned buffer 等条件，但仅把现有栈变量替换成 pinned buffer 并继续逐块等待，并不能消除这些数据依赖。[CUDA 12.8.1 内存与传输建议](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)

### A2：改变提交粒度，不改变数学网格

按 EOS／通量／重构／stage／布局分组，在同一个 launch 中索引多个 AMR 块。第一步保留现有每块存储，用 descriptor/view 列表批量访问；不要同时重写布局、数学、AMR 规则和分配器。

这不是把物理 block 16×16 改大，也不是减少 refinement。必须保持原面的重构、粗细界面退化规则、stage 权重和每方向 flux scratch 覆盖前的 reflux registration；不能用跨阶段融合省掉必要的 ghost exchange。

### A3／A4：缓存拓扑，不缓存过期状态

区分可复用的拓扑连接与每步变化的 slot、版本及数据指针。缓存至少受 topology epoch／fingerprint、handle generation、维度与布局、species 和相关路线约束。`Current/Next/Scratch` 轮换时可复用连接关系，但必须正确重绑定 view；regrid/restart 后重建或拒绝失效缓存，旧资源依 fence 退役。

不缓存需要按当前状态重算的物理量。先做有界预分配／复用；异步内存池是候选优化，不是必需的全局替换。异步分配和释放仍须满足 stream 顺序与跨 stream 依赖。[CUDA stream-ordered allocator](https://docs.nvidia.com/cuda/archive/12.8.1/cuda-runtime-api/group__CUDART__MEMORY__POOLS.html)

### A5：Graph 后置，有收益再保留

Graph 适合重复执行的设备工作流，可以降低重复提交开销；但捕获中的 stream 不能被 Host 同步，结构变化也受更新规则约束。因此，不能直接给当前包含频繁 `quiesce()` 的整段时间循环套 `BeginCapture/EndCapture`。先建立 A1 的批量边界，再尝试稳定 topology epoch 内的 Hydro 子图。Host 的 AMR 决策、checkpoint／restart 和错误消费点留在明确边界外。[NVIDIA CUDA Graph 说明与捕获限制](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)

只采用实际 CUDA 12.8 环境支持的接口；不用新文档中的新版本能力替代兼容性测试。Graph 失效时可以显式回到普通 CUDA 提交路径，不能静默回退 CPU。建图、更新、重建时间和额外显存都要纳入验收。

## 5. 每个优化的统一验收表

| 类别 | 必须保留／记录的条件 |
|---|---|
| 工作量 | 先复跑原 4×4／8×8 Sedov：相同输入、AMR 规则、物理终止时间、接受步数及 regrid 序列；原最大叶块数与演化记录可重验 |
| 数值 | 原场量 `rtol=1e-8, atol=1e-11`；守恒 `rtol=2e-12, atol=2e-10`；质量、动量、总能量、ρX 全部检查；不能放宽预算换性能 |
| 容错 | EOS 失败、NaN、空批、尾批、陈旧 handle／epoch、分配失败都 fail-closed；不得从失败的设备阶段发布正常状态 |
| AMR／restart | 三种 slot；refine／derefine／拓扑不变；Euler／RK2／RK3 与各方向；影响公共 exchange／资源层时扩展到 RKL、CPU↔GPU restart、restart 后 regrid |
| 计时 | 同一构建跑 CPU 与 CUDA；原首尾输出保留；检查点比较不计入运行时间；先保持每档预热 1 次、正式 3 次的历史口径，候选定版扩为至少 5 次交替运行，报告各次值、中位数和离散性 |
| 干扰 | 正式计时不与编译、cuDSS 压测或其他 GPU 作业重叠；记录硬件/vGPU、driver、编译选项、线程亲和性、源码与输入 hash |
| 诊断 | 单独记录 Host 计划耗时、相关 scalar D2H 次数、sync、launch、分配、峰值内存和 regrid；API 观测／profiler 运行不混入未插桩中位数 |
| 扩展规模 | 原两档稳定后再补更大规模、不同 active-block 数以及 1D／2D／3D，寻找 CPU/GPU 交叉点；不得只展示 GPU 占优的大算例而隐藏原两档回归 |
| 安全 | vGPU 不允许 sanitizer 时保持“未验收”；需要支持调试的 GPU 环境补 memcheck／racecheck。普通数值测试不能替代内存安全验收 |

性能指标分开报告：

- **GPU 自身改善**＝旧 CUDA 时间／新 CUDA 时间；衡量本次优化。
- **CPU/GPU 加速比**＝同轮 CPU 时间／CUDA 时间；大于 1 才表示 GPU 更快。
- **第一里程碑目标**：原两档在正确性通过的前提下达到不慢于 8 线程 CPU。按现有中位数静态计算，CUDA 时间分别需下降约 22.0%／19.5%；这只是目标，不是收益保证。CPU 端也优化后，必须用新的公平对照重新计算。
- 不预先承诺“3 倍／10 倍”；若收益处于运行波动内，增加重复或退回假设，不选择性保留快样本。

## 6. 暂不加入第一轮的事项

| 方向 | 当前决定 | 重新考虑的条件 |
|---|---|---|
| MPI／多 GPU／跨节点 | 延后；它解决扩展能力，不能自动修复当前单 GPU 的逐块等待 | 单 GPU 执行层稳定；有容量或强／弱扩展需求及通信基线 |
| Kokkos／AMReX／RAJA 等整体迁移 | 不默认引入，也没有对这些框架做本轮选型评估 | 明确的多厂商／分布式需求，以及迁移成本和数学一致性专项评估 |
| Tensor Core／混合精度／fast-math | 不进入当前性能修复 | 独立数值分析、误差预算与科学验证授权 |
| 常驻 kernel、复杂多 stream 流水线 | 暂后置，避免引入更复杂的设备调度／生命周期 | 批量执行与 Graph 后仍有提交瓶颈，并有并行重叠的实测空间 |
| 更换 EOS、网络、求解器或降低 AMR／输出频率 | 不用作当前加速手段 | 属于另一个科学／算法任务，不能冒充同输入优化 |
| cuDSS 大网络性能 | 单独记录和验收 | audit150／audit200 真实轨迹及容量先通过；本轮 Sedov 不启用 burn |

## 7. 2026-09-12 大网络状态核对（避免计划基于过期状态）

只读核对发现：

- 两个大网络的 focused targets 已完成编译／链接，构建日志末尾退出码为 0，耗时 **2:52:33**。
- 含两个大网络的完整 CUDA archive 和 ARCH executable 已生成；完整构建日志退出码为 0，耗时 **4:39:06**。
- 然而，[续跑状态](../../validation/backend/results/h100-performance-20260909/campaign-status-20260912.json)为 `failed`，后续命令列表为空、两个网络均无四步通过证据。控制脚本把缺少后置标记文件解释为“完整构建未成功”；这不能推翻已有编译／链接证据。
- 汇总器的只读解析本次能成功读取 focused 日志，但原流程具体在哪一步、因何中断仍需排查；不能据此断言是编译错误、OOM 或网络物理失败。
- 下一步应检查已有二进制、依赖及源码/构建身份，修复续跑状态区分，再从缺失验证处恢复。**不要未经检查重新编译数小时。**
- vGPU 的 `GPU debugging features are disabled` 限制仍单列；没有新的 sanitizer 通过证据。

本节证据来自[focused 完成日志](../../validation/backend/results/h100-performance-20260909/focused-build-completed.log)和[完整应用完成日志](../../validation/backend/results/h100-performance-20260909/integration-build-completed.log)。旧报告中的“进行中”是当时快照，不作为当前状态。

本次仅形成计划并保存只读核对结果，未修复续跑器或启动大网络重跑。官方资料通过 agent-reach 检索核对，主要用于确定 Graph 的前置条件、pinned buffer 的适用范围及异步分配的寿命约束；具体收益仍以 ARCH 的复测为准。

## 8. 分阶段交付与双仓库同步约定

根据用户要求，后续每个阶段验收完成后，先在本地 commit，再把同一个交付提交 push 到用户和朋友仓库的固定分支，无需每阶段再次确认。这里“本地保存”是 commit；push 的目标是两个远端仓库。未验收的工作区修改不作为阶段完成版本发布。

### 8.1 固定分支与共同基线

| 位置 | 本地 remote 名称 | 固定交付分支 |
|---|---|---|
| 本地工作仓库 | 不适用 | `codex/hpc-cuda-optimization` |
| 用户 `Arsenic-er/ARCH` | `personal` | `codex/hpc-cuda-optimization` |
| 朋友 `Shiro-Akane/ARCH` | `friend` | `codex/hpc-cuda-optimization` |

每一阶段都更新这同一条分支，不另建一套交付分支。不直接更新两方 `main`，也不复用旧 `e3-audit`。本地 `origin` 指向另一个本地仓库，不是这两个 GitHub 交付目标，不能依靠默认 `git push` 选择目标。

2026-09-12 只读检查：用户 `main` 为 `e12b96c15e022691d788f4b67faba4dc06da342e`，朋友 `main` 为 `7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866`；两者不同，不能视为已同步。当前本地实验分支仍在 `0266d96f20b184d4b17ebc6a066ac3b9021f1642`。拟以已核对的朋友 `main@7d4448a9` 建立共同交付基线，保留旧实验的原始提交身份。

固定分支在本次检查时于三处均不存在；S0 整理时再次 fetch 和检查，保护现有未提交修改后再建立。若期间出现同名分支，先核对其历史和用途，不覆盖。GitHub API 显示当前账号对两个仓库均有 push 权限，但尚未实际推送，也未设置或验证分支保护规则。

### 8.2 阶段计划表

所有阶段均沿用第 4、5 节的数学、容差、同输入计时及安全约束。表内是交付门槛，不是已经取得的成果。

| 交付阶段 | 对应任务 | 本阶段交付内容 | 发布门槛 |
|---|---|---|---|
| S0：共同基线与验证流程 | A0 | 基线身份、原实验报告与输入索引；区分构建成功、汇总成功、验证通过；恢复可续跑的记录流程 | 保存已有结果和待验证修改；验证续跑状态及失败路径；能可靠报告 pending/failed/passed，不把已编译误判为已验证 |
| S1：批量控制量回传 | A1 | CFL 归约与 Hydro EOS 状态批量下载／等待 | 原两档 CPU/CUDA 对比通过；确定性、空批、无效值与错误传播测试通过；相应小回传／等待次数下降，并报告端到端收益及波动 |
| S2：批量设备执行 | A2 | Hydro、边界、清零的跨块批量提交 | 尾批、粗细界面、stage/reflux 顺序回归通过；相关 launch 次数下降；同输入复测，不改变网格或物理工作量 |
| S3：AMR 计划与内存复用 | A3；按测量决定 A4 | 稳态 topology plan／scratch 复用、有界缓冲池；有必要才引入 pinned staging 或异步分配 | slot 轮换、连续 refine/derefine、restart、过期 handle 和失败退出验证通过；记录内存峰值及多轮增长；受影响的内存安全检查通过 |
| S4：CPU 调度与公平对照 | B1 | Host 调度优化、线程数与亲和性实验；重算 CPU/GPU 比值 | 1/2/4/8/16 线程结果和环境齐全；用同轮优化后的 CPU 与 CUDA 比较，不能沿用较慢的旧 CPU 作对照 |
| S5：条件性深度优化 | A5、B2 | 剩余热点支持时，分别试验 Graph 或 kernel 调优 | 有热点证据；计入建图／更新成本后收益稳定；全部相关正确性、安全门槛通过；无收益则记录拒绝／延期，不把试验分支当成有效优化 |
| V1：大网络独立验收 | C1 | audit150／audit200 的原始四步、容量、长步和完整应用验证记录；编译成本单列 | 逐项给出真实运行结果、输入、依赖身份和未完成项；缺少 sanitizer 权限明确阻塞相应安全验收，不假报完整通过 |

第一轮按 **S0 → S1 → S2** 逐阶段交付。S3、S4 随后推进；S5 不预先承诺必须引入新机制。V1 的科学与运行验收独立，不阻塞无 burn 的 Sedov 阶段；正式计时期间不运行大网络压力测试或重型编译。

S3 内 A3、A4，以及 S5 内 Graph、kernel 调优，保持独立可回退提交和独立测量；一个大阶段需要多个可用交付点时可用 `S3a/S3b` 等子阶段，仍推送同一固定分支。若可选部分延期，只发布实际通过的范围，不能把未做部分标成完成。

### 8.3 每阶段保存什么

阶段记录建议放在 `validation/backend/results/hpc-cuda-optimization/<阶段>/`，大网络专项继续使用其验证目录并在这里建立索引。记录至少包括：

- 基线提交、受测源码提交／tree hash、构建选项、硬件／driver／CUDA／compiler、输入文件及其 SHA-256。
- 改动范围、全部测试命令和退出状态、容差、CPU/GPU 原始样本、中位数与离散性、守恒／场量误差、API 计数和内存数据；失败与未测项目不得删除。
- 相对上一阶段和初始基线的比较、保留／撤销／延期决定、已知限制和下一阶段入口。
- 大型 checkpoint、HDF5、二进制和原始日志包保留在明确的归档位置，记录摘要及可恢复路径；不把它们无筛选地塞入普通 Git 历史。

区分“受测代码提交”与“交付提交”：可先冻结代码运行测试，再增加报告的文档提交。报告标明真实受测 SHA，并检查后续提交仅增加记录、未改变源码／构建／测试输入。不要在提交内容中伪造该提交自身的 SHA；最终交付 SHA 由 Git、阶段 tag 和推送后的回执标识。

### 8.4 同步流程和冲突处理

1. 阶段开始时 fetch `personal` 和 `friend`，核对固定分支以及朋友 `main` 的变化。相关上游修改需先整合、再重新冻结基线；不在一组正式测量中途悄悄换版本。
2. 本地保存代码与测试记录，完成本阶段所有适用验收。正确性失败、所需安全检查无法执行、结果无法复现时，状态保留为失败／待验证／受阻，不发布“已验收”标记。已知 vGPU sanitizer 限制必须保留，不能以普通数值测试代替；不受该限制的 S0 文档／流程检查可独立验收。
3. 发布前再次 fetch 两端。目标远端分支必须不存在，或其 tip 是待交付提交的祖先。发现他人新提交，先整合并复测受影响范围；禁止强推、覆盖历史或对已发布历史做 rebase。
4. 使用显式目标推送同一个本地提交，例如 `git push personal HEAD:refs/heads/codex/hpc-cuda-optimization` 与 `git push friend HEAD:refs/heads/codex/hpc-cuda-optimization`。分支发布只允许 fast-forward；并发更新导致拒绝时回到第 3 步。
5. 每阶段建立不可移动的 annotated tag，例如 `hpc-cuda-s0`、`hpc-cuda-s1`，标明实际交付范围并同步到两端。标签若已存在先核验，不能强行移动；修订版使用新标签。
6. 读取两个远端分支 tip 和阶段 tag 所指向的 commit，核实均为预期交付 SHA。只有本地、用户远端、朋友远端一致，才报告“本阶段已验收并同步”。报告包含阶段、SHA/tag、测试摘要、收益与遗留项。
7. **两个仓库之间的 push 不具有跨仓库原子性。** 若只成功一边，标记“部分同步”，保留成功结果并安全补推另一边，不强制回滚。若任何一端已有新修改，先整合复测；对齐后再进入下一阶段的代码改动。

阶段状态区分为：未开始、进行中、待验证、验收通过／待同步、部分同步、已验收并同步，以及失败／受阻／延期。性能试验没有收益可以形成透明的试验记录，但不能宣称优化成功。

固定分支本身不能阻止多人同时修改。建议团队把它当作统一交付入口：其他合作者从最近阶段 tag 开发功能分支，通过审查后整合；开始工作前 fetch 并核对交付 SHA。这是待团队采用的协作约定，不表示已替朋友配置权限或取得其承诺。本轮不修改任何仓库的分支保护设置。

### 8.5 本次落地状态

本轮仅更新这份阶段／同步计划并核对仓库身份和权限，尚未创建固定分支、commit 或 push，S0 也尚未完成。现有四个测试框架文件的修改仍待真实运行验证，予以保留，不混入“已完成优化”提交。后续按上述门槛实施和逐阶段同步。

## 9. 返送意见后的执行修订（2026-09-12，替代冲突的旧约定）

本节采纳用户提供的 CudaOptimizationHandoff.md。第 8.5 节为修订前快照，不是实时状态。本计划只安排实施，不替代既有 CudaReleaseStandard、Validation 和各模块科学预算。

| 项目 | 当前执行约定 |
|---|---|
| S0/A0 | 增加两档基线复现、可访问证据索引、四个工具文件差异和负向控制、最小 batch 完成／错误草案、MPI／物理接口边界；所需负责人确认单列 pending，不代签 |
| MPI | 不由 CUDA 优化侧实施，但其他负责人可从共同主线并行开发；不要求等待整个 CUDA 优化完成 |
| S3 | 提前约定 topology、storage generation、slot/view、实际依赖的几何／布局／species／策略失效；未来 rank 归属变化也需重新绑定，不宣称当前已有 partition epoch |
| S4/B1 | 必做公平 CPU 对照和线程扫描；CPU 算法／OpenMP 大改非必做，有实测需要才独立审查，并补新 CPU 对照 |
| S5/A5 | Graph 仍为条件性试验；不强制先采用 A4 异步分配器。包含建图、更新、重建、元数据传输和显存成本 |
| V1 名称 | 改为“大网络专项”，避免与冻结 V1 混淆；不阻塞无 burn Sedov，不争用正式测速资源 |
| 同步 | 开发快照、待验收代码可以 commit/push 给其他机器补测；只有证据完整才能标记已验收。保留同名固定分支和双端 SHA 核对，不改 main/冻结 V1、不强推 |
| 标签 | 仅按需为已验收开发里程碑创建不可移动 tag；待测使用提交 SHA。不是 public Release，不自动触发 Release/DOI |
| 状态 | 分列正确性、性能、安全、集成和同步；两端 SHA 一致只证明同步，不证明验收通过 |

第 5 节数字预算及相同步数/regrid 要求仅用于原两档 Sedov；其他自适应物理沿用现有 manifest、独立参考与源项／边界通量预算。burn 各个 rho X 不要求分别不变；严格 restart 约束不放宽。

S1 先在 backend 内批量提交，在原 Host 消费／发布边界完成，不先重写全面异步 scheduler。合法局部空批不贡献候选；最终无有效全局候选仍按共享 Error 契约拒绝。错误传输和资源安全清理不能随等待一起删掉。

复用现有 tests/tools、provenance、runner、sanitizer 和 qualifier。比较器、容差、reference 或通过条件变化先独立审查，再生成受影响结果。CPU CI 通过不是 GPU 验收；覆盖按当前 inventory，不写死历史数量。

维持现有浮点选项、Release LTO、31 个完整 ODE 方程的 DenseLU 分界、provider 拒绝契约和 NVCC 12.0+ 文档范围。新机制不悄悄提高最低版本；不为硬件或算例写数学特例。

CUDA 分支是该工作线的交付入口，主线是审查后的集成入口。公共接口草案先返送共同确认；每日轻量对齐是团队建议，尚未建立自动任务或替其他负责人确认。历史文档中的发布授权不适用于本轮 main 合并或正式 Release。
