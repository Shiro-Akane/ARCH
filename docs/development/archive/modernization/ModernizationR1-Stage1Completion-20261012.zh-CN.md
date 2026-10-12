# ARCH 现代化阶段 1：全仓现状检查与接续清单

> 已完成阶段的历史快照；状态和下一工作包以[现代化主计划](../../../../ARCH_MODERNIZATION_PLAN.md)为准。归档仅调整阅读位置与相对链接，原调查／验收身份保留。

2026-10-12，`compute/optim`。**阶段 1／R1 的静态现状检查已完成；阶段 2／R2 未启动。**
本轮只修改项目文档与索引，不修改生产源码、公式、接口、测试断言或历史科学证据。

原“R1：结构地图”在本文件中记为 **R1.1（原 R1）**，并保留其原始快照。根据用户后续完成整个第一阶段的要求，本文继续补齐了实际调用、构建归属、验证入口、目录对应关系及注释要求。

此处“完成”是指主要模块已明确职责、权威实现、调用与构建入口及验证导航，并不代表对全部公式进行逐行审查、证明所有后端完全等价或开展新的科学验收。

## 工程契约与证据身份

| 项目 | 本轮确认 |
| --- | --- |
| 调查提交 | `c5ce17a67bd8d82899ab7420fec627c3cf97cb9d`；开始时与 `origin/compute/optim` 一致、工作区干净 |
| 生产源码身份 | 与前置 `a0185bdf524d374f9c804666a98a4c76668db044` 相同；其间增加的是 R1.1 文档 |
| 工作入口 | `/home/shiroakane/.codex/worktrees/d7a6/ARCH`；主目录 `/home/shiroakane/ARCH` 保留旧执行物与依赖缓存；WSL／`work`／Node 入口见 [AGENTS](../../../../AGENTS.md) |
| 路径覆盖 | 11,078 条 Git 基线路径，115 个互斥分类组，未分类 0；本轮新增文件不反计入调查基线 |
| 生产接线 | 四种配置的 `ARCH` 依赖闭包合并后覆盖 `src/` 与 `simulation/` 全部 119 个已跟踪 `.cpp/.cu/.c` 编译单元；未归属 0，不能据此推断头文件语义或运行正确性 |
| 数学与物理 | 沿用 [项目 Skill](../../../../.agents/skills/arch-engineering/SKILL.md)、[实现所有权](../../ImplementationOwnership.md) 与现有科学契约；共享权威实现，保留有依据的后端执行差异 |
| 本轮执行边界 | 可隔离 configure 和静态审计；不编译应用／测试目标，不执行数值测试、完整 CUDA 构建、RZ 长程、GUI 原生交互或性能优化 |
| 本机资源 | 沿用 16 GB 工作站及既有资源护栏；配置耗时不是编译／运行基线，不以设置并发推断实际内存安全 |

证据层次分别为：路径分类、源码调用检查、配置后的目标／测试注册、实际运行验收。
本轮新增前三类；第四类仅引用其原有身份，详见 [前置记录](Preflight-20261012.zh-CN.md)。

## R1 完成清单

| 工作包 | 已完成的检查 | 产物与限制 |
| --- | --- | --- |
| R1.1 结构覆盖 | [x] 分支／入口；[x] 全 Git 路径分类；[x] 源码／第三方／生成／历史区域区分 | [原始地图](ModernizationR1-RepositoryMap-20261012.zh-CN.md)、[更新后的覆盖索引](ModernizationR1-RepositoryCoverage-20261012.json)；不是逐文件阅读 |
| R1.2 控制与 Hydro | [x] 参数→准备→派发；[x] CPU/GPU 重构、通量、散度、阶段更新；[x] 调度、完成状态与错误传播 | 下表的真实调用边；没有全策略组合的编译／数值证明 |
| R1.3 网格与 AMR | [x] 几何／原生 RZ 量；[x] 拓扑身份；[x] ghost／seam；[x] prolongation/restriction；[x] reflux；[x] 重网格事务与驻留 | 公共数学与后端搬运分开；未新增守恒、重启或持续执行验收 |
| R1.4 微物理 | [x] EOS 视图／表所有权；[x] 内置／生成网络；[x] ODE／NSE／能量交接；[x] DenseLU／KLU／条件 cuDSS | 包生成结构已查；没有在当前工作区生成任意 custom 网络，也没有逐行复核大型 Timmes 反应展开体 |
| R1.5 重力／扩散／边界 | [x] 物理源与 Poisson/MG；[x] Host／CUDA 执行注入；[x] RKL 系数／算子／阶段；[x] 用户物理边界与 device lowering | 明确 RZ 积分与 EOS 闭合职责不同；旧热点只引用、不诊断根因 |
| R1.6 API／I/O／Studio | [x] 16 个算例注册；[x] 9 个只读 API 入口；[x] 配置身份；[x] checkpoint／plot 发布；[x] GUI 配置／预览／构建／启动／重启／读取链 | 已找到真实 Core 调用与进程所有权；Mock、测试替身与原生桌面验收分别保留 |
| R1.7 构建／验证／资源 | [x] 四种隔离配置；[x] CMake FileAPI 目标闭包；[x] CTest 条件清单；[x] Hosted CI；[x] 现有结构审计；[x] 验证／资源／生成工具归属 | configure 与静态审计成功；测试注册不是测试通过，构建吞吐及 RAM/Swap 未测 |
| R1.8 文件／注释／接续 | [x] 目录—文件—职责对应；[x] 历史移动表核对；[x] 未来落点判断；[x] 冻结行为源码注释要求；[x] 候选及后续排程 | 扩展原有规范，不批量搬迁、改写源码注释或另建治理框架 |

## 主要模块与共享核心地图

下列路径均相对仓库；测试列提供后续导航，本轮没有运行。完整文件分类仍由覆盖 JSON 承担，不在正文列出万余个文件。

| 范围 | 权威实现与实际消费者 | 后端／职责边界 | 验证导航 |
| --- | --- | --- | --- |
| 入口／配置／算例 | [main](../../../../src/main.cpp) → [ProblemGenerator](../../../../src/interface/ProblemGenerator.h) 的 `SetupChecked` → [SolverDispatch](../../../../src/driver/SolverDispatch.cpp)；`core/config` 与 `simulation` | 已准备配置、物种来源和执行计划在进入 Driver 前确认；公开 `include/` 转发头维持算例入口 | [执行计划](../../../../tests/host/driver/test_resolved_execution_plan.cpp)、[simulation](../../../../simulation/README.md) |
| 策略与能力 | [PolicyDescriptor](../../../../src/driver/dispatch/PolicyDescriptor.h)、[BackendCapabilities](../../../../src/driver/dispatch/capability/BackendCapabilities.h) → Euler/RK2/RK3 bindings | 类型注册与已编译能力共同决定可用路由；Auto 的有记录选择不等于运行中静默回退 | [执行计划测试](../../../../tests/host/driver/test_resolved_execution_plan.cpp) |
| Driver／阶段顺序 | [DriverMacroStep](../../../../src/driver/stages/DriverMacroStep.h)、[StageScheduler](../../../../src/driver/schedule/StageScheduler.h) → 各阶段与 runtime | 共用阶段控制；Host／Device 的槽位、ghost、完成和提交由生命周期所有者管理 | [共享调度测试](../../../../tests/host/driver/test_shared_stage_scheduler.cpp) |
| Hydro 重构／通量 | `numerics/reconstruction`、`numerics/flux` → [HydroSolverImpl](../../../../src/numerics/integrator/HydroSolverImpl.h) 与 [CUDA flux binding](../../../../src/cuda/hydro/policies/HydroFluxPolicies.cuh) | PCM/MUSCL/PPM 与 VL/SW/Roe/HLL/HLLC 的公式在公共策略；CUDA 多 TU／flux family 是实例化边界 | [Hydro 测试区域](../../../../tests/cuda/README.md)、[独立 hydro 验证](../../../../validation/hydro/README.md) |
| Hydro 散度／更新 | [TimeIntegratorHelper](../../../../src/numerics/integrator/TimeIntegratorHelper.h) 的 `accumulate_cell_divergence`／`update_stage_cell` → CPU 阶段、[HydroStateKernels](../../../../src/cuda/hydro/kernels/HydroStateKernels.cuh)、[HydroStageKernels](../../../../src/cuda/hydro/kernels/HydroStageKernels.cuh) | 同一数学叶；kernel 成功、EOS 完成和状态发布有各自边界 | [Hydro leaf parity](../../../../tests/cuda/hydro/test_hydro_leaf_parity.cu) |
| 几何／状态／诊断 | [GridMetrics](../../../../src/grid/GridMetrics.h)、`data/FluidState`、`numerics/state`、`physics/diagnostics` → 初始化、Hydro、AMR、I/O | 图表、度量、状态可接受性、速度／Jeans 诊断有共同所有者；设备缓存不再定义公式 | [数学与几何检查](../../../../tests/math/README.md)、[AMR 独立参考](../../../../validation/amr/README.md) |
| 原生 RZ 的两层数学 | [RzCellAverage](../../../../src/numerics/state/RzCellAverage.h) → 初始化／源／传递；[RzNativeClosure](../../../../src/numerics/state/RzNativeClosure.h) → EOS 输入、扩散／燃烧／输出 | 前者计算 V/W 加权守恒均值，后者生成临时热力学闭合；用途不同，不能因近似命名删除其一 | [RZ 契约](../../../../validation/core_contracts/rz/README.zh-CN.md) |
| AMR 拓扑与驻留 | `amr/topology` → [StateResidency](../../../../src/driver/runtime/StateResidency.h)、[RuntimeStateTransaction](../../../../src/driver/runtime/RuntimeStateTransaction.h)、CUDA store/regrid | 拓扑 epoch、槽位与回滚是控制契约；后端管理分配／迁移／成功后发布 | [驻留测试](../../../../tests/host/driver/test_state_residency.cpp)、[AMR 验证](../../../../validation/amr/README.md) |
| ghost／seam／边界计划 | [CoordinateSeamPlan](../../../../src/amr/exchange/CoordinateSeamPlan.h)、[CoordinateSeamMath](../../../../src/amr/exchange/CoordinateSeamMath.h)、`amr/exchange/BoundaryPlan.h` → Host 与 CUDA lowering | 逻辑计划与 `cuda/hydro/boundary/BoundaryPlan.h` 的设备计划同名但职责不同；物理 callback 可有 Host 参与 | [CUDA grid 检查入口](../../../../tests/cuda/README.md)、[边界模型](../../../../src/physics/boundary/README.md) |
| transfer／reflux／refinement | [RegridTransferMath](../../../../src/amr/transfer/RegridTransferMath.h)、`amr/flux/FluxRegister.h`、[RefinementIndicatorMath](../../../../src/amr/refinement/RefinementIndicatorMath.h) → Host AMR 与 CUDA migration/flux/indicators | 限制、守恒传递、reflux 系数及修正共享；元数据遍历和 D2D 搬运属于适配器 | [AMR 测试说明](../../../../tests/host/README.md)、[动态 AMR](../../../../validation/amr/README.md) |
| EOS | `physics/eos/eosdispatch`、Ideal/Helm/Tabular3D/4D views → CPU policies、[CudaBackendFactory](../../../../src/cuda/runtime/CudaBackendFactory.cpp) | 源码共享 EOS 数学；Host 解码／补全与 device owner 上传／借用视图分开，表错误状态须由调用边界处理 | [EOS 验证](../../../../validation/eos/README.md)、[EOS host/device parity](../../../../tests/cuda/microphysics/eos/test_eos_host_device_parity.cu) |
| 网络／NSE | `physics/network/{iso7,aprox13,aprox19,aprox21,timmes_common}`、`physics/nse` → common burn solver | 保留来源与反应展开体；NSE 能力按网络 metadata 与编译类型确认，阈值未满足或投影失败仍可进入 ODE | [burn 参考](../../../../validation/burn/README.md)、[生成网络参考](../../../../validation/network/README.md) |
| ODE／能量交接 | `numerics/burnsolver/{ode,coupling}` → CPU BurnDispatch、[microphysics_api](../../../../src/cuda/microphysics/microphysics_api.h)、[SparseOdeBatch](../../../../src/cuda/microphysics/burn/SparseOdeBatch.cuh)；[DriverBurnPolicy](../../../../src/driver/stages/DriverBurnPolicy.h) 接交积分源能量 | BE-NR／BD／ROS4 共用模板；提交源积分而非用两个大 EOS 能量相减推断小释放量 | [burn mainline](../../../../tests/host/burn/test_burn_mainline_reference.cpp)、[低密度](../../../../validation/low_density/README.md) |
| 线性代数／稀疏 provider | `numerics/linalg` DenseLU/KLU、[CuDssSparseSolver](../../../../src/cuda/microphysics/linalg/CuDssSparseSolver.h) → ODE continuation | KLU 为 CPU 条件能力；cuDSS 为实际 provider 条件能力；缓存、lane、完成与原矩阵残差接受分开 | [CUDA linalg 区域](../../../../tests/cuda/README.md)、[生成 sparse 验证](../../../../validation/network/README.md) |
| 扩散／RKL | [DiffFunction](../../../../src/numerics/diffusion/DiffFunction.h)、[DiffFlux](../../../../src/numerics/diffusion/DiffFlux.h)、[DiffusionAMRStages](../../../../src/numerics/diffusion/DiffusionAMRStages.h) → DriverStages 与 [DiffusionKernels](../../../../src/cuda/diffusion/DiffusionKernels.cuh) | 系数、空间算子、应力与阶段数学共享；执行批次和边界完成属于后端／调度 | [RKL 系数测试](../../../../tests/host/numerics/test_rkl_repair_weights.cpp)、[diffusion 验证](../../../../validation/diffusion/README.md) |
| 重力物理／求解器 | `physics/gravity/{GravitySource,GravityBoundary,self}` 与 `numerics/{elliptic,multigrid}` → [GravityStage](../../../../src/driver/stages/GravityStage.cpp) | `arch_gravity_cpu` 名称不能证明实际求解永远在 Host；控制调用 `execution->numeric()`，已有 CUDA executor 提供 device 实现 | [self gravity](../../../../tests/host/gravity/test_self_gravity.cpp)、[gravity 验证](../../../../validation/gravity/README.md) |
| 重力执行 | [GravityExecution](../../../../src/physics/gravity/GravityExecution.cpp) 与 [CudaGravityExecution](../../../../src/cuda/runtime/gravity/CudaGravityExecution.cu) → workspace／准备场／Hydro 消费 | 公共任务描述、MG 控制和物理数学；Host 为默认，CUDA 按 backend 注入；不能根据测试目录名称推断设备覆盖不足 | [CUDA gravity 职责](../../../../src/cuda/runtime/gravity/README.md)、[Poisson/MG 测试](../../../../tests/host/gravity/test_poisson_multigrid.cpp) |
| 物理边界 | `physics/boundary/{UserBoundary,PhysicalBoundary,PhysicalBoundaryHandler,BoundaryFlux}` → DriverBoundary 与 CUDA boundary runtime | 注册的 callback、EOS ghost 规则、值／梯度／外向通量定义共享；gather/callback/scatter 和 native reflection 有执行差异 | [边界说明](../../../../src/physics/boundary/README.md)、[RZ 契约](../../../../validation/core_contracts/rz/README.zh-CN.md) |
| I/O／restart／publication | [DriverIO](../../../../src/driver/io/DriverIO.cpp) → `io/{chk,plot,hdf5}`；[HDF5Writer](../../../../src/io/hdf5/HDF5Writer.cpp) checked close + 同目录 atomic replacement | 先 materialize 接受态再输出；身份／几何／checkpoint 兼容性共用；原子可见不承诺断电持久化 | [checkpoint 兼容性](../../../../tests/host/io/test_checkpoint_compatibility.cpp)、[plot 发布](../../../../tests/host/io/test_plotfile_publication.cpp)、[restart 验证](../../../../validation/restart/README.md) |
| Core 只读 API | [ApplicationContract](../../../../src/api/ApplicationContract.h) → [PreviewCommand](../../../../src/api/preview/PreviewCommand.cpp) → configuration/preview/session/inspection/resources | 9 个命令：preview、capabilities、schema、inspect-config、list-cases、amr-resources、preview-amr、inspect-case、preview-session；预览准备不证明 simulation readiness | [API 契约说明](../../../../src/api/README.md)、[API 测试](../../../../tests/README.md) |
| Studio 配置／预览／构建 | [project](../../../../studio/host/project.ts) → configuration／previewRunner／buildRunner／runPreparation；`studio/src/host` 是 renderer adapter | 调用真实 Core schema/inspect/list/preview；source、binary、config revision 分开确认，不能由前端存在推断就绪 | [Studio 测试分层](../../../../studio/tests/README.md) |
| Studio 运行／重启／停止 | [RunController](../../../../studio/host/runController.ts) → [runWorker](../../../../studio/host/runWorker.ts)、[runSupervisor](../../../../studio/host/runSupervisor.ts)、runCheckpoint | 独立 xterm handoff，已保存输入副本、binary/checkpoint 身份复核、持有进程与 stop 请求；存储的 running 记录不等于活进程 | [运行 worker 测试](../../../../studio/tests/host-run-worker.test.ts)、[supervisor 测试](../../../../studio/tests/host-run-supervisor.test.ts) |
| Studio plot 读取／展示 | [plotfileMetadataWorker](../../../../studio/host/plotfileMetadataWorker.ts) → plotfileMetadata → `studio/src` plot adapters | 固定 Host HDF5 reader，受限 slice/point/overview；读取和展示不执行 Core、不产生科学参考 | [Host plotfile 审计](../../../../studio/tests/host-plotfile-audit.test.ts)、[plot 契约](../../../../studio/docs/contracts/PLOTFILE_AUDIT_API.md) |
| 生成／复现／资源工具 | `tools/network`、`tools/validation_provenance.py`、`tools/run_memory_guarded.py`、`tools/check_ci_results.py` → CMake／validation／CI | 复用生成、进程护栏和身份 helper；独立 reference 保持独立，不能从 production helper 构造 expected 值 | [tools](../../../../tools/README.md)、[network generator](../../../../tools/network/README.md)、[Hosted CI](../../../../.github/workflows/README.md) |

### 代表调用的独立源码证据

以下行号绑定调查提交 `c5ce17a67`，来自实际源码读取，与 FileAPI 的“119 个编译单元有归属”是两类证据。它们证明列出的静态调用，不证明所有运行组合等价。

| 操作 | Host 调用位置 | CUDA 调用位置／共同所有者 |
| --- | --- | --- |
| 重构策略 | [FluxSweep](../../../../src/numerics/flux/FluxSweep.h) 第 179 行 → [AMRInterfaceReconstruction](../../../../src/numerics/reconstruction/AMRInterfaceReconstruction.h) 第 97／109 行的 `ReconstructPolicy::run_species/run` | [HydroReconstructionPolicies](../../../../src/cuda/hydro/policies/HydroReconstructionPolicies.cuh) 第 33／61／116 行调用 PCM／Muscl／PPM；共同策略在 [Reconstruction](../../../../src/numerics/reconstruction/Reconstruction.h) |
| Riemann 通量 | [FluxSweep](../../../../src/numerics/flux/FluxSweep.h) 第 201／233 行调用 `FluxPolicy::compute_face_flux` | [HydroFluxPolicies](../../../../src/cuda/hydro/policies/HydroFluxPolicies.cuh) 第 38 行调用 `OriginalFlux<PCMReconstruction>::compute_face_flux`；这里 PCM 仅命名通量类，输入已经按所选策略重构 |
| 散度累积 | [TimeIntegratorHelper](../../../../src/numerics/integrator/TimeIntegratorHelper.h) 第 501 行调用 `accumulate_cell_divergence` | [HydroStateKernels](../../../../src/cuda/hydro/kernels/HydroStateKernels.cuh) 第 139 行调用同一 `TimeIntegration::accumulate_cell_divergence` |
| 阶段更新 | [HydroSolverImpl](../../../../src/numerics/integrator/HydroSolverImpl.h) 第 117 行进入 `perform_stage_update`；[TimeIntegratorHelper](../../../../src/numerics/integrator/TimeIntegratorHelper.h) 第 633 行调用 `update_stage_cell` | [HydroStageKernels](../../../../src/cuda/hydro/kernels/HydroStageKernels.cuh) 第 44 行调用同一 `TimeIntegration::update_stage_cell` |
| RKL 阶段数学 | [DiffusionAMRStages](../../../../src/numerics/diffusion/DiffusionAMRStages.h) 定义公共阶段算术及 Host 调度 | [DiffusionKernels](../../../../src/cuda/diffusion/DiffusionKernels.cuh) 第 475／531 行直接调用 `apply_first_rkl_stage_cell`／`apply_recursive_rkl_stage_cell`；没有把 Host 调度整体称作设备实现 |

## 构建、依赖与验证入口

已用现有 HighFive／SuiteSparse 缓存离线配置，四个构建目录均在 `/tmp/arch-stage1-20261012-*`，不改主目录旧缓存。
共同条件为 Release、GCC/G++ 12、Ninja、testing ON、OpenMP/KLU ON；CUDA 使用本机 12.3 工具链，`native` 在本机解析为 `86-real`。
这不是发布所需架构列表。CMake 的小型编译器／IPO 探针已执行，应用与测试目标没有编译。

| 配置 | CMake 目标总数（含依赖／测试） | CTest 注册 | 配置结果及条件 |
| --- | ---: | ---: | --- |
| CPU | 96 | 76 | 成功；68 数值／API + 8 tooling wrapper |
| CUDA，默认查找 | 187 | 140 | 成功；未发现默认搜索路径下的 cuDSS，不能表述为本机没有该依赖 |
| CUDA，显式本机 `CUDSS_ROOT` | 207 | 143 | 成功；`arch_cuda_sparse_provider` 与 16 个内置 network×EOS sparse owner 进入依赖闭包，新增 3 个条件测试 |
| CPU + Studio | 100 | 76 | 成功；新增 assets/runtime/arch-studio/package 四个目标；只检查 Node，不执行 npm ci/build/Electron 安装 |

四份 CTest JSON 各有 20 项能解析执行命令，其余多数依赖尚未编译的 C++/CUDA 可执行文件；注册存在不等于执行成功，也不能把尚未生成 executable 当成科学测试失败。
cuDSS 增加的精确测试名及所有编译单元 owner 见 [构建索引](ModernizationR1-BuildInventory-20261012.json)。

`CMakeLists.txt` → build options/application → Host tests → optional CUDA/tests → dependencies → deferred I/O tests → optional Studio。
`arch_build_contract` 传播公共头、生成 registry、特征宏、严格浮点选项；`arch_diffusion_math`、`arch_gravity_cpu`、`arch_driver_runtime`、`arch_solver_dispatch` 各有明确 owner。
CUDA 对象目标、重编译队列和阶段屏障是现有内存／实例化控制措施，不能只为减少文件数合并。
HDF5/HighFive、OpenMP、SuiteSparse/KLU、CUDA/cuDSS、Node/Electron 分别按对应条件启用；外部安装／缓存不是仓库内可任意整理的源文件。

现有 [结构审计](../../../../tools/audit_architecture.py) 本轮执行退出 0：检查已编码的共享实现、头文件层与目标接线约束；不是数学证明。
[Hosted CI](../../../../.github/workflows/ci.yml) 为 main PR 配置 Tooling、CPU Release、Studio 与汇总门；CPU 用内存护栏构建、验证注册／JUnit 结果并检查 low-density。
本轮没有触发或重跑这些 job。Hosted CI 没有 GPU 验收，CUDA 与科学长程依然使用已有专项出口。
后续优先补将被修改的路径，不重新建设完整 CI 平台。

## 文件排布核验与项目要求

- 当前一级源码模块、既有布局职责目录与 CMake 编译归属一致；`include/` 的转发头、API 根公开声明、`tools/` 的稳定 CLI/import 入口保留有依据。
- `simulation` 的目录名不总等于运行 case ID：已从实际注册核对 16 个 ID，不能按文件夹数推断支持范围。
- 三方来源的 EOS／Timmes/NSE 适配代码仍是维护中的生产源码；保留来源行号、声明与许可证。它们不同于外部库缓存，也不同于可重新生成的薄绑定 TU。
- `validation/**/results` 多为版本证据，但其中也有活跃的重放／分析脚本；不能整体当作生成垃圾删除。历史报告、候选 API 示例与冻结夹具维持各自身份。
- 2026-09-22 的布局表共 294 项，293 个目标仍存在；缺失的 `SparseBeNrBatch.cuh` 在现有 ownership 中已说明退役。旧表不改写，本轮用此处解释其历史身份。
- 旧布局中的“Poisson/MG 未实施”、文件数量及授权描述属于当时快照，现有构建已包含 multigrid；恢复上下文应读当前计划与 ownership，不能用旧快照作当前门禁。

已将未来落点判断加入 [ImplementationOwnership](../../ImplementationOwnership.md#directory-and-file-placement)，把冻结行为源码的注释要求加入 [CommentAndDocumentationStyle](../../CommentAndDocumentationStyle.md#comments-in-behavior-frozen-source)，并在 AGENTS 留下简短入口。
新增文件先找权威所有者，核对调用、include/import、构建与测试落点；不因超过 8 个文件或文件较长就拆目录。
注释说明真实职责、输入→处理→发布、数学单位／坐标／符号和资源生命周期；声明头不能写成已执行请求处理。
行为冻结与字节冻结分开：获授权的纯注释改善仍会改变摘要／构建身份；冻结证据保持原文，通过维护中的说明补充。本轮没有改写任何源码注释。

## 后续候选与优先级预判

候选是阶段 2／3 的输入，不是已批准的实现包；科学阻断项沿用原出口，不被现代化检查“通过”覆盖。

| ID | 已知证据与判断 | 下一步／阶段 | 风险与验证代价 |
| --- | --- | --- | --- |
| C01 共享核心基线 | 已证实 Hydro 重构→通量→散度→更新的共享边；适合作为最小代表切片 | 阶段 2 选 CPU/GPU 同输入，记录独立参考与 backend parity；不先改算法 | 数学风险高，相关 leaf/短程验收中等成本 |
| C02 RZ source/boundary 成本 | 旧记录定位主要时间在源／边界，未证明缓存根因；部分长程仍未完成 | 阶段 2 先核对旧身份与短有界计时；阶段 3 再排序，阶段 7 才考虑优化 | 高风险／高成本；禁止默认重开 5-cross 长程 |
| C03 Cellular 速率差异 | 保留分支 `origin/codex/cellular-burn-rate-audit-20261008` 的 `110e769dd` 只加报告；实际复现身份为 `ee091b35c`，不是报告分支源码 | 阶段 2／3 核对 [报告提交](https://github.com/Shiro-Akane/ARCH/commit/110e769dd) 的适用范围与原筛查失败；不合并未审实现、不放宽门槛 | 数值判断高风险；短精确同时间参考优先，长期影响未证实 |
| C04 构建吞吐／依赖 | 现有目标闭包、重 TU、阶段屏障已定位，尚无当前 cold/incremental/RAM 测量 | 阶段 2 采集；阶段 3 按收益选，阶段 5 局部处理 | 中等风险；完整 CUDA 冷编译成本高，统一重负载排程 |
| C05 目录导航／注释 | 已见 API 根声明的通用 Workflow 重复；逻辑/device BoundaryPlan 同名但职责不同；gravity README 可改善入口 | 阶段 3 判断维护收益；阶段 5 做局部文档／注释包，默认保留合理位置 | 低至中风险；检查真实调用和摘要失效，不要求全量回归 |
| C06 可选能力／生成网络 | cuDSS 默认与显式根的注册不同；当前内置 custom 根没有已选网络包 | 阶段 2 记录所选能力身份；涉及该路径时才验证代表 generated/sparse route | 条件路由风险高；已有 generator/fixture/manifest 优先复用 |
| C07 生命周期／耦合出口 | 状态、ghost、regrid、restart、GUI handoff 已有 owner；当前完整 Native RZ／持续耦合等出口仍有缺口 | 阶段 2 分类旧证据；阶段 4 仅补首批改动所需的保护 | 高风险；端到端成本较高，不能用 Mock 或短仪表化替代科学轨迹 |
| C08 不值得修改的部分 | 本轮没有发现可据此直接删除的 RZ 双层数学、公开转发头、独立 oracle 或 CUDA 实例化层 | 保留；只有新的具体证据才重新打开 | 避免无收益迁移和测试自证 |

## 尚未完成的验证与有意保留范围

| 范围 | 当前边界 | 接续条件 |
| --- | --- | --- |
| 全策略／全几何／全 EOS 组合 | 查了注册、代表调用、生成模板及构建闭包，没有穷举运行等价性 | 首批修改触及的组合先做风险选择；不要为“全仓”机械构造笛卡尔积 |
| 大型上游反应展开体／表／冻结数据 | 已识别来源、owner 与消费者；未逐行读 `.inc`、原始 EOS/H5/checkpoint 或重算参考 | 只有出现具体科学问题或修改对应来源时深入 |
| 数值、性能、编译、内存四类现代化基线 | 尚未测量；旧结果不自动重标为当前提交通过 | 阶段 2 建立固定身份的代表清单 |
| RZ 长程、持续耦合、fresh restart、Native Device／GUI | 原完成／失败／中断／后置状态见前置和 release 记录，本轮不重跑 | 依据原出口与首批改动的需要单独排程 |
| generated/custom 与 provider 运行 | 本轮只读结构与条件配置；没有新包、provider 运行或 cache 压力验收 | 选择代表包和可用依赖后沿用既有验收 |
| 历史万余路径、候选及本机 build/output | 路径已分类，未逐个阅读或清理；未合并唯一报告分支保留 | 有消费／修改需求时定点追踪，不能按目录名批量删除 |

阶段 1 所需的静态模块范围没有待分类区域。上表是检查深度与后续验收清单，不能记成已通过，也不阻止在明确边界下结束现状检查。

## 可预见的后续工作安排

以下是接续顺序；本轮在 R1 停止。每行开始时再按证据建立具体工作包，避免预建空实现任务。

| 顺序／阶段 | Codex | W1：算法／架构 | W2：构建／验证 | W3：独立审查 | 放行条件 |
| --- | --- | --- | --- | --- | --- |
| R2.1 基线身份与代表集 | 冻结来源、已接受门槛、输入／执行物身份和资源预算 | 挑 Hydro／微物理／AMR／重力的最小有代表性切片；区分独立参考与后端比较 | 核对可复用旧证据、所需依赖、产物路径与短程命令 | 检查代表性和缺口，尤其 Cellular 与 RZ 的适用身份 | 基线清单可复现，无放宽标准或重复旧长程 |
| R2.2 数值与接口基线 | 统一运行排程、判定科学结果 | 核对守恒量、源能量、热力学及已接受差异 | 执行相关现有测试／短轨迹，记录真实通过／失败／未执行 | 核对公平输入、时间与 oracle 独立性 | 已选范围有有效原始证据，失败与后置分列 |
| R2.3 运行／编译／内存基线 | 串行安排重负载，保留可复用执行物 | 说明工作量是否等价及热点假设边界 | 有界采集端到端、cold/incremental、峰值 RAM/Swap；复用资源 guard | 判断比较可信度与干扰 | 四类基线齐备；没有证据的指标保持未知 |
| 阶段 3 审计与排序 | 决定修改／保留／延后和唯一写入范围 | 追查选中重复／边界问题，区分结构与算法变化 | 用测量选构建／依赖／运行热点及验证缺口 | 比较局部方案、现状与复杂度代价 | 每个首批包有证据、收益、风险、验证成本和回退 |
| 阶段 4 最小保护 | 选首批改动对应的验证层，不先铺平台 | 复用或补真实独立 oracle／不变量 | 补必要短程、接口或条件 CI 入口 | 确认能够抓住预期回归 | 保护就绪才放行相关高风险实现 |
| 阶段 5 开发效率 | 统一集成与身份更新 | 局部降低无效耦合；落实有收益的文件／注释维护 | 改实测构建瓶颈、依赖或常用入口 | 检查维护收益及资源代价 | 独立小包验收，收益不足则保留 |
| 阶段 6 核心重构 | 每个共享组件指定唯一写入者 | 一次归并一个确定重复／边界问题，保持数学行为 | 接口冻结后接 CPU/GPU 并验证受影响路径 | 检查新重复、过度抽象和数值漂移 | 对应数值与性能非回归出口通过 |
| 阶段 7 运行优化 | 管理有界尝试、停止与回退 | 只对测得热点提出局部方案 | 测传输、分配、同步、布局／调度与端到端效果 | 独立比较速度、内存、数值和复杂度 | 有依据的收益才采用；无收益可结束 |
| 阶段 8 收敛 | 复查原目标与剩余风险，交付 | 更新共享 owner 与有意保留边界 | 做最终相关回归和四类基线对照 | 审核全覆盖索引、未决项与复现入口 | 目标达成即停止，不以改动量衡量完成 |

下一轮最合适从 **R2.1：基线身份与代表集** 开始。共享核心优先选受能力检查支持的 Cartesian Hydro 重构→通量→散度→阶段更新；然后按候选证据选择 RKL、burn/energy、AMR transfer/reflux、gravity execution，而非同时重构所有模块。

## 证据与协作索引

- 仓库内：[战略计划](../../../../ARCH_MODERNIZATION_PLAN.md)、[路径覆盖](ModernizationR1-RepositoryCoverage-20261012.json)、[构建／测试索引](ModernizationR1-BuildInventory-20261012.json)、[旧证据与停止状态](Preflight-20261012.zh-CN.md)。
- 本机原始产物：`/mnt/c/Users/26428/.codex/visualizations/2026/10/11/01a12b15-a669-76a0-b053-6fd9e79a6b50/stage1-20261012/`；保存四份 configure 命令／日志／receipt、FileAPI 图、CTest JSON、结构审计和 worker 完整交付／失败记录。它不是可搬运依赖，也不是科学通过标志。
- DeepSeek 进行了 Hydro、AMR/state/I/O、微物理、gravity/diffusion/boundary、布局／注释五项有边界的只读调查；Codex 独立核对关键源文件、构建闭包与路径后仅采用相应证据。三项首交付格式不合格，各作一次原任务的文本返修；原失败保留，超长但可读的返修不再为风格重做。
- 执行成功与独立验收分开；未证实的全路由等价、缓存根因、任意 generated 网络就绪、按测试目录名推断覆盖、以注释数量推断质量等判断未纳入事实。
- 最后一次 DeepSeek 文本核对确认所给配置／计数一致，同时要求区分构建归属与调用证据；已补充上面的函数／行号表。该审查本身没有本地工具，不能代替 Codex 的源码核对。其首轮因输出 token 上限返回空交付，一次原任务返修后完成；Gemini 仅润色两段已冻结的说明，经摘要及语义核对采用。
