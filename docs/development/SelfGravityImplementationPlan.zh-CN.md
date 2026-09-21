# 自引力、可维护性重构与 GUI 协作实施计划

| 项目 | 当前记录 |
|---|---|
| 文档状态 | P0/P1 CPU 与相关 GPU 门槛通过；P1.5 N0–N5 已验证并推送；P2 单层 CPU Poisson/MG、解析验证和封包回归已通过 |
| 文档版本 | 1.1，2026-09-22，接续 P2 单层 CPU Poisson/MG 与初步解析验证 |
| 审查基准 | 远端 `main`：`01cc4f723e674d47fe23850e7c0fef221e92e98c`，2026-09-21 已重新拉取并核对 |
| GUI 契约基准 | `codex/studio-core-ui-contracts`：`d98f6f6e853ccb23eaa916ab1d20356019087622`；完整继承其 6 个 Core API 提交 |
| 文档所在工作区 | `/home/shiroakane/ARCH`，`physics/selfgravity`；P1 实现提交 `95b858fc9f97147a8bced5146dd229dcc3d08df4` |
| 目的 | 在实施、复查、接续任务和合并时，统一约束模块归属、功能保持、生命周期、数值方案与验收范围 |
| 当前改动授权 | P1.5 验证并推送后，按新授权完成 P2 原型、简单解析验证和物理分支推送；不越过 P3/P4 的 AMR/能量耦合门槛。沿用 CPU 优先、受影响 CUDA 最后集中验证及不得降低测试标准的要求 |

本文是本功能的实施参考，不表示 self-gravity 已可用，也不替代现有
[实现归属](ImplementationOwnership.md)、[项目验收要求](CudaReleaseStandard.md)、
[测试指南](../../tests/README.md)和[验证记录](../../validation/README.md)。
用户后续明确指令优先；据此调整计划时，应同步记录受影响的约束、阶段和验收。
文中的新文件名与接口名是建议落点，语义边界具有约束性；合理更名应更新本文，不能借更名改变职责。

P1 历史基线见 [P1 交接记录](SelfGravityP1Handoff.zh-CN.md)；本轮实际实现与验收见 [P1.5 实施记录](P1_5ImplementationReport.zh-CN.md)。
P1 的准备接口和字段身份不代表已有 Poisson/MG 或可运行自引力，`gravity_type=self` 继续明确拒绝。

[低密度与近真空可靠性修复计划](LowDensityRobustnessPlan.zh-CN.md)定义新增 P1.5 的参数语义、
唯一实现归属、保守通量/可追踪兜底、CPU/CUDA 验收及 P2 接续门槛。
P1.5 不新增物理自由度或算法微调项，优先复用 `sml_rho/min_eint/max_eint/smallx/smallt` 等既有语义；
其第 3.4–3.5 节记录现有物理配置清点和后续新增参数的准入规则。
它属于数值行为修复，不得并入 P1 的“行为保持”历史验收。

[物理配置与旧 EOS 退役清单](ParameterRetirementAudit.zh-CN.md)补充全部 90 个标准键的处置、
5 个拟删标准键、2 个未接入成员、3 个既有时间步键的标准登记，以及 EOS/配套设施清理范围。
按最新用户要求，明确退役项不保留旧输入、旧接口或旧表兼容；保留特性的科学验收标准不变。
此决定覆盖本文及历史审查中针对这些项目的“默认保留兼容”建议；删除已按 P1.5 退役清单实施，历史清点文字不代表现行代码仍保留这些实现。

[main 全仓可维护性审查清单](MainMaintainabilityAudit.zh-CN.md)是本文第 12 节的组成部分，
列出全部命中行数阈值的代码、密集目录、来源数学豁免、疑似死代码和依赖证据。
后续涉及这些文件的修改必须同时查询清单对应的 L/S/F/DC/DU 条目。

**维护性第一：相关且共同变化的内容放在一起。** `>600` 行、`<60` 行、目录直接文件数 `>8`
只触发检查，不产生拆分、合并、删除或新建目录的义务。第 4 节文件布局及清单中的“优先”都是候选，
实施者须说明它能减少哪种理解或修改成本；无法证明收益时保留现状。第 10 节规定与 GUI 的共同接口及接续顺序。

## 1. 每次开始和结束工作时如何使用本计划

1. 开始前核对工作分支、目标 main、GUI/CUDA 契约提交和本阶段基准。阅读第 2 节硬约束、当前阶段、第 5 节接口契约、第 10 节协作门槛及相关决策项。
2. 写明本次对应的阶段编号、约束编号、拟修改文件，以及明确不属于本次阶段的内容。核对已有工作，避免重做已通过的检查。
3. 依赖某项数值选择前，先完成第 9 节对应决策记录。常规实现选择可由实现者在既定范围内确定；待决策项不自动构成额外的用户审批要求。
4. 修改完成后检查 diff 是否跨越归属边界，执行本阶段受影响的构建、数值与生命周期检查。
5. 在第 10 节更新状态、源码/构建/输入身份和证据位置。未执行、不可用、失败、通过必须分别记录。
6. 阶段交接时说明已完成项、剩余项、当前限制和下一个允许进入的阶段。下游原型可先写，但不能在前置契约未通过时标成可用能力。

改变硬约束、物理支持范围、方程/边界语义、完成语义或验收预算时，先更新本计划的决策记录，再进行依赖该变化的工作。不得为消除测试失败而静默放宽预算。

## 2. 必须保持的约束

| 编号 | 约束 | 直接检查方式 |
|---|---|---|
| SG-01 | 保持一个 CPU/CUDA 共用的顶层 Driver 和物理阶段顺序。 | 无第二套 backend 专属主循环；阶段轨迹比较。 |
| SG-02 | 重网格/Driver 行为不变的提取与新增引力物理分开提交、分开验收。 | 每阶段 diff 与证据标明重构或新增行为。 |
| SG-03 | 全域 Poisson 求解每个必要的 Hydro 阶段准备一次，各 patch 只消费已发布字段。 | 求解调用次数、阶段输入版本、patch executor 调用路径。 |
| SG-04 | 引力字段必须匹配输入状态、拓扑、存储与算子/边界版本，且已完成。 | 过期视图、错误 slot、退休 storage 和未完成结果的拒绝测试。 |
| SG-05 | 物理、离散算子、MG 算法、执行后端、运行调度分别归属明确模块。 | 依赖审查；公共数学无 CPU/CUDA 重复实现。 |
| SG-06 | AMR 求解必须处理整个复合域的粗细层耦合与通量一致性。 | composite residual、界面误差和跨层解析/参考对照。 |
| SG-07 | MG 粗层及标量工作空间由求解器独立管理。 | 不依赖已退休的 hydro 父块，不扩充 fluid/species 传输计数来夹带 phi。 |
| SG-08 | 维持现有完成凭证、事务发布、回滚与资源退休规则。 | token、失败注入、重网格和 restart 检查。 |
| SG-09 | CUDA 正常求解中密度与 MG 中间数组留在设备，执行按层/批组织。 | 传输、分配、launch、同步计数及调用路径。 |
| SG-10 | 首版主求解路线是 matrix-free 几何/复合 MG；KLU/cuDSS 为可选粗层或参考 provider。 | 无全域 `N*N` 存储；不将可选库变成基础 MG 的强制依赖。 |
| SG-11 | 首版不加入 FFT、Green FFT、MPI、多 GPU、相对论或粒子引力。 | 配置和 capability 不暴露未实现路线。 |
| SG-12 | 自引力的时间离散、动量与能量耦合必须单独定义并验证。 | 源项推导、时间收敛、力对称性和带正确边界项的能量预算。 |
| SG-13 | 支持情况必须按几何、维度、边界、AMR、积分器、backend 的组合声明。 | 不支持的组合在启动解析阶段报错；不静默换物理模型。 |
| SG-14 | CUDA“可运行”“数值通过”“取得加速”分别记录。 | 同误差预算的端到端 CPU/GPU 比较，含适用范围与退化案例。 |
| SG-15 | 沿用 ARCH/GUI Core 的全系统 CGS，包括 IdealGas；自引力使用现有 G 常数，不新增用户单位模式或隐式换算。 | phi 为 cm²/s²、g 为 cm/s²、rho 为 g/cm³；单位元数据与物理输入一致。 |
| SG-16 | 合法低密度状态在 EOS、重构、通量、CFL、源项、AMR 和诊断之间使用一致语义；物理 floor、算法容差和无效状态分开。 | P1.5 的 NV 检查与跨模块状态契约；无隐藏密度停用线。 |
| SG-17 | 显式状态修复必须记录质量、动量、组分与能量变化；数值通过与 floor/模型域敏感性分别报告。 | 接受态预算、restart 累计、失败/回退记录；不能用人工修复掩盖自引力守恒误差。 |
| SG-18 | 物理控制量优先复用既有参数/模型接口；派生量与方法内部保护不自动成为新输入。 | P1.5 不新增物理自由度/算法微调项；允许将 3 个既有时间步键补入标准 Advanced。未来新增物理参数说明独立含义、实际消费者和既有接口不能表达的理由。 |
| SG-19 | 明确退役的配置、旧 EOS 与独占设施整套收敛，不维持新旧双路线。 | 按退役清单 PR/ER/CR 实施；已退役 Core 键和旧规范化表明确拒绝；保留 case 扩展、native EOS、来源数学及其科学验收。 |

## 3. 目标范围与首版边界

本计划的目标是非相对论 Newtonian 气体自引力：质量源来自当前待求解阶段的流体密度，
通过 Poisson 方程得到势与加速度，并进入已有流体时间推进。粒子质量、外加点质量、
外引力与自引力叠加等扩展不默认包含在首版中。

实施采用以下能力顺序；前一项通过不代表后一项已支持：

| 能力 | 本计划安排 |
|---|---|
| Cartesian 单层网格，周期边界及制造解 Dirichlet 边界 | 首个 CPU 求解验证范围；覆盖 1D/2D/3D 的适用方程 |
| 多块同层与静态 AMR composite solve | CPU AMR 数值验收前必须完成 |
| 动态 regrid、reflux、restart 与 Hydro 阶段耦合 | 完整 CPU 自引力闭环的必需项 |
| 三维孤立系统的有限域边界近似 | 首版应用能力目标；需独立给出边界方法、截断误差与测试 |
| 单 GPU CUDA 的同等物理能力 | 独立的正确性和性能阶段；未通过的组合保持不可选 |
| 圆柱/球坐标、强各向异性、任意混合 BC、时间子循环 | 后续扩展；不能从已有 Hydro 支持情况推定引力也支持 |
| FFT/Green FFT、AMG、大型全域稀疏直接求解 | 本计划不实现；有测量或应用需求后再单独立项 |

Cartesian 1D/2D 方程对应其平移不变的物理假设，不等同于球对称或轴对称引力。
制造解使用的给定 Dirichlet 边界也不等同于真实孤立系统边界。流体的 outflow/reflecting
边界不能直接转换成引力边界；两者分别配置与解析。

## 4. 代码基石与文件分工

### 4.1 已核对的基准事实

- [Driver](../../src/driver/Driver.h) 在审查基准上为 1499 行；统一控制流已存在，但状态同步、AMR 事务、物理阶段和 IO 混在同一函数。
- [StageScheduler](../../src/driver/schedule/StageScheduler.h) 已共享 Euler/RK2/RK3、RKL 阶段与发布规则；CPU 积分器和 CUDA 执行器均经过这些规则。
- [IGravityPolicy](../../src/physics/gravity/IGravityPolicy.h) 是 Host patch 接口；[工厂](../../src/physics/gravity/GravityDispatch.h)和[能力检查](../../src/driver/dispatch/capability/BackendCapabilities.h)仍拒绝 Self 路线。
- [AMR tree](../../src/amr/topology/AmrTree.h)在重网格中退休/释放块；[粗细层传输](../../src/amr/exchange/CoarseFineCellPlan.h)使用 `6 + species_count`，不能直接充当 MG 标量接口。
- [SparseWrap](../../src/numerics/linalg/SparseWrap.h)的 `SparseMatrixData<N>` 分配 `N*N` 个索引；[CsrMatrixView](../../src/numerics/linalg/CsrMatrixView.h)仍为固定模板维数。
- [CsrPattern](../../src/numerics/linalg/CsrPattern.h)已有运行时 extent 和稀疏结构；[CuDssSparseSolver](../../src/cuda/microphysics/linalg/CuDssSparseSolver.h)已有运行时维度、设备数据和因子生命周期能力。应按职责复用，不能把它们与整个燃烧求解栈绑在一起。
- [Application.cmake](../../cmake/project/Application.cmake)不会自动收集新增的 `src/driver/*.cpp`；必须明确加入构建目标。

这些是源码事实，不是本轮数值正确性或性能验收。实现开始时如 main 已变化，更新基准并复核这些入口。

### 4.2 Driver 拆分落点

下列文件以 `src/driver/` 为根给出逻辑职责名称；第 12 节 R3 和审查清单 F 项给出二级目录候选。
落地时统一确定完整路径，再更新所有调用方，不同时维护两套实现。新增文件按阶段创建，不一次生成空壳。

| 文件 | 职责与边界 |
|---|---|
| `Driver.h` | 保留启动、主循环、物理顺序、重网格与输出时机；以能顺着主循环理解一次宏步为目标，不预设最终行数。 |
| `DriverRuntime.h/.cpp`，新增 | 唯一持有运行层 registry、ledger、clock、backend、handles、storage generations；不接管所有物理算法。 |
| `DriverBoundary.cpp`，P1 已落地 | 实现 Runtime 的 boundary 成员；分开 `ensure_fluid_ghosts(slot)` 与 `materialize_current_for_host()`，普通 device 消费不能意外触发 D2H。不另建透传声明头。 |
| `DriverRegrid.cpp`，P1 已落地 | 实现 Runtime 的 regrid 成员；协调 prepare/migrate/publish/rollback/retire，保留 AMR 模块对树与迁移数学的所有权。 |
| `DriverIO.h/.cpp`，新增 | 输出前的可见性、检查点运行相位、运行记录；继续使用已有 `io` reader/writer。 |
| `DriverStages.h`，P1 已落地 | 集中 EOS 模板相关的 CFL、Hydro/RKL/Burn 执行绑定和可复用 scratch；共有一个 Burn 半步执行体，保留 First/Second 归约身份。共同依赖/生命周期的阶段绑定暂不再拆成多个小头。 |
| `DriverBurn.h`，保留 | 继续拥有 Host patch/cell traversal，调用公共 `DriverBurnPolicy`；不混入运行层 backend 协调。 |
| `DriverControl.h`，保留 | 裁决增长限制及输出对齐；具名候选值由 `DriverStages` 返回，控制器现有语义不变。 |
| `GravityStage.h/.cpp`，P4 再创建 | 将真正的全域引力服务绑定到 P1 准备契约、发布、失效和其他显式消费点；P1 不生成空服务。 |

`DriverBurnPolicy.h` 保持公共 cell 规则；`DriverUtils.h` 不继续接收运行管理逻辑。
`StageScheduler.h` 只增加窄的通用准备契约；其自身拆文件可另做维护阶段，不与首次物理接入混改。
不得通过大量相互捕获的回调、`.inc` 片段或一个万能 Context 来隐藏原有依赖。

优先扩充已有职责匹配的 `DriverBurn`、`DriverControl` 等辅助文件；表中多个逻辑角色如确实共同变化、
依赖和生命周期相同，可以放在同一实现文件。模板/ABI/编译隔离需要时仍保留小声明或独立 TU。
GUI 分支已提取 `InitializeRootState`、`BindRefinementThermodynamics` 和 `InitialConservedState`；
P1 接续这些共享入口，不从旧 main 再提取一套。`BCHandler` 同时被初始 AMR 预览消费，
整理 `DriverUtils` 时让它依赖公共边界规则，不能强迫预览构造完整 DriverRuntime 或 CUDA backend。

### 4.3 自引力与数值模块落点

| 目录/拟新增文件 | 唯一负责的内容 | 不应依赖的内容 |
|---|---|---|
| `src/grid/ScalarFieldView.h`、`MeshHierarchyView.h` | 标量布局、几何、cell/face 索引与只读层次视图 | EOS、gravity 模型、具体 MG 算法 |
| `src/amr/EllipticMeshAdapter.h/.cpp` | 从已发布 AMR topology 建立求解器所需的邻接、覆盖掩码、粗细面映射 | Poisson 常数、源项时间积分 |
| `src/numerics/elliptic/EllipticSolveTypes.h`、`EllipticBoundary.h` | 求解状态、容差、边界类型/数据的通用契约 | 物理模型选择、全局 SimConfig |
| `src/numerics/elliptic/CellCenteredOperator.h`、`CompositeOperator.h/.cpp` | `apply`、residual、梯度/通量、粗细层离散一致性 | 重网格控制、流体存储 owner |
| `src/numerics/multigrid/MGHierarchy.h/.cpp`、`MGTransfer.h`、`MGCycle.h` | 辅助粗层、限制/延拓、平滑和 V-cycle/FAC 组织 | 引力常数、EOS、物理边界选择 |
| `src/numerics/multigrid/HostMGExecutor.h/.cpp`、`MGBottomSolver.h` | CPU 执行与粗层求解契约 | CUDA 运行时头文件、burn 状态 |
| `src/physics/gravity/GravitySolveTypes.h`、`GravityBoundary.h/.cpp`、`SelfGravity.h/.cpp`、`GravitySource.h` | 质量 RHS、物理 BC、引力字段、源项数学与物理诊断 | Driver 主循环、独立复制的 MG 实现 |
| `src/cuda/elliptic/`、`src/cuda/multigrid/` | 共享离散规则的 GPU stencil/transfer/reduction kernels | 另一套物理公式 |
| `src/cuda/runtime/gravity/CudaBackendGravity.h/.cu`、`multigrid/CudaMGExecutor.h/.cu` | 设备存储、层/批执行、stream 与完成状态 | CPU patch 虚接口的设备调用 |
| `src/numerics/linalg/`，按需扩充 | 可选动态 CSR/CSC、粗层或参考 provider 适配 | MG 与 gravity 生命周期 |
| `cmake/project/Application.cmake`、`cmake/cuda/CudaBackend.cmake`，必要时 `cmake/SelfGravity.cmake` | 显式编译和可选依赖接入 | CPU-only 构建对 CUDA/cuDSS 的强制依赖 |

首版只实现常系数 Poisson 所需能力。接口应允许其他线性椭圆算子接入，但不提前实现
辐射、FLD、非线性 FAS 或通用插件框架。复用通过通用算子与 MG 接口完成；现有 RKL
扩散不因本计划而强制改写为隐式求解。

## 5. 必须先建立的接口与生命周期

### 5.1 输入、所有权与输出

以下为跨阶段语义契约。P1 冻结 `ScalarFieldView`、密度依赖/字段身份和 Hydro 准备请求的 C++ 声明；
`MeshHierarchyView`、算子/求解报告和实际 phi/g 消费视图随 P2/P3/P4 的首个消费者落地，避免无实现的通用接口空壳。
后续新增声明仍须满足本表，不得将 P1 的版本标记误当成已具备几何、BC 或数值算子。

| 接口对象 | 最少应表达的内容 |
|---|---|
| `MeshHierarchyView` | dimension、geometry、active cells、volume/face metrics、邻接/覆盖、topology epoch；不拥有 hydro 数组。 |
| `ScalarFieldView` | 数据地址、长度/stride、有效区与 ghosts、cell/face 位置、Host/Device、storage generation；非 owning view。 |
| `GravitySolveRequest` | 全域 stage density 视图、输入 slot/版本、阶段时间、G 与单位约定、物理 BC 与版本、求解配置、执行上下文。 |
| `EllipticOperator` | `apply`、`residual`、所需的边界/梯度操作；通过显式输入工作，不搜索 Driver 全局状态。 |
| `SolveReport` | 收敛/失败原因、循环次数、最终物理残差、范数/容差、相容性修正、完成凭证；无“提交即成功”。 |
| `GravityFieldStamp/View` | 输入依赖、拓扑 epoch、引力存储 generation、算子/BC 版本、求解精度记录与完成状态，提供 phi/g 的只读消费视图。 |

Runtime 拥有运行资源；Gravity service 拥有引力字段；MG executor 拥有残差/校正/粗层工作空间。
不能让 Boundary、Regrid、Gravity 各自拥有可独立修改的 active-block 真相。
跨操作缓存应保存稳定 owner 与版本，不长期保留已交换 ledger、临时 span 或退休块指针。
各块状态版本不一致时，记录完整输入依赖或明确拒绝，不能用首块版本代表全域。

### 5.2 阶段准备位置

```text
重网格/初始态准备与必要的显式引力消费
  → 计算宏步 dt
  → Burn(dt/2) → Diffusion(dt/2)
  → Hydro 的每个 RK 阶段：
      验证 descriptor.input_slot 的可读性
      → 获取该阶段全域 rho
      → GravityStage 检查/求解/发布 phi 与 g
      → CPU patch 循环或 CUDA batch 消费匹配的场
      → 若能量方案需要：基于候选密度完成势场与引力功修正
      → 验证候选态及显式修复预算
      → 发布流体结果并按既有规则完成边界
  → 槽位轮换与最终 reflux
  → Diffusion(dt/2) → Burn(dt/2)
  → 推进时钟
```

CPU 的 Euler/RK2/RK3 和 CUDA batch 必须共享这个准备契约。仅在
`integrator_solve()` 前计算一次 phi 不足以支持 RK 中间态。
准备点位于输入验证之后、executor 开始之前，并在 CPU OpenMP patch 循环之外。
现有 `ScopedStageBinding` 可以携带窄的阶段服务绑定；不能把物理场变成全局单例，
也不能假设 OpenMP 工作线程自动继承调用线程的 thread-local 绑定。

上述候选态完成点为 P4 的规划，不是 P1 已实现能力。与质量通量一致的守恒引力功可能需要
候选密度对应的势场；阶段前 prepare 不能单独完成这种能量耦合。评估复用现有
`BeforePublish` 完成点，并在 D-03 冻结 RK 权重、前阶段功的扣除/替换及最终 reflux 处理。
必要求解次数由一致离散决定，不以“每阶段只能求解一次”作为优化约束。

每次从当前阶段密度求解是首版基准路线。预测势、跨阶段外推或减少求解次数属于后续
优化，需独立证明时间精度与能量预算。引力 BC 若随时间/密度变化，必须按阶段更新。
初始细化或 restart 恢复结束后，先取得有效的接受态密度，再准备首次需要的引力场。
初始输出早于 CUDA backend 构造的现有顺序必须显式处理，不能读取尚未建立的 device 视图。

### 5.3 失效与失败规则

| 事件 | 字段及结构缓存的处理 |
|---|---|
| Hydro 输入密度改变或 slot 物理映射改变 | 重新验证依赖，旧字段不直接作为本阶段结果；允许用作初值。 |
| Burn/Diffusion 推进流体状态版本 | 首版保守失效；以后仅在具备 density revision 或明确密度不变证明时复用。 |
| 最终 reflux | 旧场不能代表已校正密度，下一次消费前重建有效性。 |
| regrid 成功 | 使旧 topology/storage 视图失效；新 MG 结构可在下次求解前延迟构建。 |
| regrid 无拓扑变化 | 结构缓存可保留；字段仍检查输入和 BC 版本。 |
| regrid 失败 | 不发布一半的新流体/引力状态；保持旧接受态可用或按既有失败策略退出。 |
| restart | 从恢复密度重新建立有效场；持久化的 phi 若存在，默认只作 warm start。 |
| 求解不收敛、非有限值、CUDA/库错误 | 返回失败，不发布未完成解，不继续应用旧场伪装成功。 |
| 输出、诊断或重力步长限制需要最终态字段 | Driver 显式准备后交给消费者；IO 内部不隐藏地启动物理解算。 |

首版不在重网格事务的 noexcept 发布区构建 MG、分配内存或求解。
旧设备视图只有在消费者完成后才可退休；任何缓存借用了旧存储都必须遵守同一完成边界。
若引力失败后要重试整个宏步，必须先具备完整状态回滚；当前计划不默认提供该能力。

## 6. 数值契约

### 6.1 方程、边界与零空间

物理约定为 `laplacian(phi) = 4*pi*G*rho_source`、`g = -grad(phi)`。
数值实现可统一使用 `A = -laplacian`、`b = -4*pi*G*rho_source`，但 apply、residual、
粗层求解、梯度和源项必须使用同一符号。D-01 的单位部分已确定为 CGS：G 直接使用 `arch::constants::gravity::cgs::gravitational_constant`（6.67430e-8 cm³/(g·s²)），rho 为 g/cm³、长度为 cm、时间为 s；phi 为 cm²/s²（erg/g），g 为 cm/s²，Poisson RHS/物理残差为 s⁻²。MG 可以在内部做明示的数值缩放，但对外恢复 CGS，不形成第二套用户单位制。

- 周期域使用体积加权平均 `rho_source = rho - mean_V(rho)`；仅统计有效复合域，不能重复计入被细层覆盖的区域。
- 周期势固定体积加权零均值；RHS 相容性、残差与校正的零空间处理贯穿各层。记录被移除的均值，不把不相容 RHS 默默伪装成原方程已解。
- Dirichlet 物理解与误差校正方程的边界分别处理；校正使用对应的齐次边界条件。
- 三维孤立域优先评估多极展开给出的有限边界值；展开中心、阶数、源到边界距离和误差预算必须冻结。固定 `phi=0` 外边界不能未经验证就称为孤立边界。
- 纯 Neumann/混合边界若日后启用，单独定义通量相容性与规范；不默认从周期实现推导支持。

### 6.2 AMR 与 MG

- `phi` 采用 cell-centered 存储作为首版方案；面梯度/通量来自相同离散算子，供粗细层守恒与 g 重构使用。
- AMR levels 与 MG coarsening levels 分开编号和管理。MG 的辅助粗层、覆盖掩码与体积由自身拥有，不要求 hydro 保留非活动父块状态。
- 构造面系数、restriction、prolongation 和 coarse-fine coupling 时使用已有几何度量的权威实现，避免重新定义体积/面积。
- RHS/质量聚合保持体积一致性；带符号的 residual/correction transfer 不能直接复用面向流体正性/组分的限幅器。
- 首版采用全域复合几何 MG/FAC 类校正流程。各 patch 独立求解并交换一次 phi 不构成 composite solve。
- 粗细层耦合要进入算子和复合残差，并验证法向通量匹配。流体 reflux 与椭圆算子的界面校正是不同职责。
- 优先从 matrix-free stencil、V-cycle、适合 CPU/GPU 的加权 Jacobi 原型开始；实际平滑参数、粗化终止与 bottom solver 在 D-02 冻结并用收敛数据确认。
- 粗层算子采用再离散还是 Galerkin 方案须明确记录，不能混合使用不一致的残差和校正定义。CG/PCG 只有在相应加权内积下的对称正定条件得到确认后才可启用。

### 6.3 收敛、力与能量

使用有效复合域体积范数，例如 `norm_V(q) = sqrt(sum(V*q*q)/sum(V))`。
首版残差停止条件定为 `norm_V(b - A*phi) <= max(atol, rtol*norm_V(b))`；
规范、非齐次边界对 b 的处理和具体容差在 D-04 冻结。零 RHS 必须有有效的绝对容差规则。
最终残差由原始复合算子重算，不能只采用平滑器、预条件或库内部的变换残差。
达到最大循环数但未满足条件视为失败；数值残差小也不等于势/力离散误差已通过。

源项方案必须定义动量更新、引力功、RK 权重以及密度通量与能量更新的关系。
已有 external-gravity 的 `rho*v dot g` 路径不能单独充当自引力能量一致性的证明。
对适用的封闭/周期测试，检查带 `1/2 * integral(rho*phi)` 的总能量预算；
孤立有限域/开放流体边界必须计入适用的边界通量并固定势规范。
同时检查净自力、动量漂移、时间收敛和 AMR 界面误差。具体离散推导在 D-03 完成。
耦合时间阶数按实际分裂、扩散与源项离散共同确定；选择 RK3 不能直接证明完整分裂系统达到三阶。

P1.5 将 floor、能量裁剪等人工修复纳入独立预算。P4 必须在同一接受态上区分物理源项、
边界通量、人工质量/能量变化和剩余守恒误差；reflux 或修复改变密度后，仅重新求解 phi
不能代替相应的能量同步。默认质量源包含接受态的全部流体密度，不隐式扣除 density floor。

### 6.4 KLU/cuDSS 的复用边界

基础 MG 不要求新增大型全域稀疏矩阵。可以复用现有库发现/链接、CSR/CSC 验证、
资源 RAII、完成检查和因子复用机制，作为小型参考问题或 MG bottom solver。
`SparseMatrixData<N>` 的 `N*N` 索引表不得用于全域 Poisson。

确有粗层需要时，再引入运行时维度的稀疏 view/owner，明确索引宽度与溢出拒绝。
Poisson 的矩阵类型、规范固定、排序、缩放、原始残差和缓存标识必须单独确定；
不能照搬燃烧 Jacobian 的 cell token、重试规则或缓存容量。
缺少 KLU/cuDSS 时，基础 CPU/GPU MG 应仍有其已验证的默认 coarse 路线。

## 7. CUDA 与性能约束

1. 在 P1 定义 Host/Device view 和生命周期，P2–P5 建立 CPU 数值参考，P6 完成 CUDA 验收。CPU 优先不意味着可以先写一套无法映射到 device 的容器 API。
2. device rho、phi、residual、correction 与粗层工作数组常驻设备；主机保留拓扑控制元数据。初始化、明确 Host 输出和已声明的 coarse 策略之外的传输必须说明原因。
3. 按 MG level/同类块批执行 stencil 与 transfer。粗层适当聚合小块，避免每个 patch 每个 sweep 发射独立小 kernel。
4. 缓存 hierarchy、工作数组和编译后的交换计划，按 topology/storage/operator/BC 类型变化重建；仅 RHS 或边界数值变化不应无条件重建全部结构。
5. 保留现有同步完成契约。MG 内部可在同一 stream 上连续执行，在残差判定与发布等必要边界确认完成；禁止将 Pending 工作标成 Complete。
6. 残差尽量在 device 归约；检查频率由求解器内部执行策略管理，首版不开放新的用户微调键，最终必须真正满足停止条件。不能为了减少同步而固定循环后无条件接受。
7. 优先验证普通批量执行，再按 profile 引入 kernel fusion、图捕获或更复杂调度；不得同时放宽精度、换物理或改变 CPU 参照。
8. 小规模粗层是否转 CPU，由含传输/同步成本的测量决定，明确作为 provider 选择记录；禁止隐藏逐周期往返。显式 CUDA 请求不静默退到整套 CPU 求解。
9. CPU-only 构建不包含 CUDA runtime 头文件；Host 声明与 `.cu` 实现保持编译隔离。只拆头文件不视为完成编译依赖优化。

性能验收必须同时记录 cold setup、稳定拓扑重复 solve、regrid rebuild 和带 gravity 的完整宏步。
CPU/GPU 使用相同输入、边界、精度与相当的初值策略；CPU 线程数经过合理扫描，不能只挑单线程作参照。
统一包含与排除 IO 的计时口径，记录 kernel、H2D/D2H 字节、同步、迭代数、内存峰值及缓存重建次数。
至少重复测量并报告中位数与波动；用于宣称加速的预先选定目标案例需要取得超出噪声的 `T_cpu/T_gpu > 1`。
小网格或粗层占优的退化点必须保留。若目标案例仍负加速，P6 的正确性可通过，性能项保持未通过，不能将整体目标标成完成。

## 8. 分阶段改动与验收门槛

| 阶段 | 允许的主要改动 | 必需交付与退出条件 |
|---|---|---|
| P0 基准与决策 | 实施工作区、输入/证据索引、本计划记录 | 锁定 main/GUI/CUDA 工作提交，确认未提交改动；按第 10.4 节整合共享 Core 基线，按阶段冻结决策与预算。 |
| P1 Driver 与契约 | 第 4.2 节文件、构建接入、窄阶段准备入口、标量接口声明 | 先 IO/Burn，后 Runtime/Boundary/Regrid，再 Hydro/Diffusion；no-op prepare 通过 none/external 回归。主循环顺序、slot、reflux、重启相位与资源语义不变。 |
| P1.5 低密度可靠性修复 | [专项计划](LowDensityRobustnessPlan.zh-CN.md)的 LD-01–14、共享状态语义、正性/失败与修复报告；退役清单 PR/ER/CR 与已有控制登记 | 完成 N0–N5：独立参考、低密度/近真空、尺度、守恒、保留 EOS 域、AMR/restart、CPU 后集中 CUDA 验收；不凭 floor 或后端相等证明正确。旧路线拒绝与当前能力覆盖分别核对。精确真空/dual-energy/通用重试单列扩展。 |
| P2 单层 CPU 椭圆/MG | grid view、elliptic、multigrid、独立小测试 | 周期/制造解 Dirichlet、零空间、残差、transfer 和失败路径通过；不提前启用 self 生产路由。 |
| P3 CPU composite AMR | EllipticMeshAdapter、覆盖/邻接、粗细算子与独立 MG 层次 | 多块与静态 AMR 解、界面通量、复合残差和内存生命周期通过。 |
| P4 CPU 自引力闭环 | GravityStage、SelfGravity、源项、阶段时间、capability/配置、IO/restart | RK 阶段密度匹配，动态 regrid/reflux/restart、引力步长、动量/能量/时间精度通过；只启用已验证组合。 |
| P5 孤立边界与支持矩阵 | GravityBoundary、三维有限域验证、参数与诊断 | 边界误差可控，扩域/展开阶数敏感性通过；周期与孤立的物理语义分别明确。 |
| P6 CUDA 正确性与性能 | CUDA field store、MG executor、backend 绑定、按需粗层 provider | 同一物理支持矩阵逐项验证，生命周期检查通过；性能另有独立结论，满足第 7 节门槛。 |
| P7 文档与交付 | 用户配置、能力表、IO 契约、已有归属文档、验证汇总 | 文档只声明实测能力；冻结最终源码/构建/输入身份，无未说明失败项，完成每项端到端验收。 |

P1 保留 `resume_after_regrid`、首步 burn 限制、`dt_old`、两半步归约标识、ENUC、输出编号与终态强制输出语义。
重网格事务先整段提取，不能在同一阶段顺手改写数学或异常路径。
P2/P3 的数学组件可以独立验证；self capability 仍保持拒绝，直到相应完整组合通过 P4。
新增数值修复基线须先通过 P1.5 再进入 P2 的正式构建验收；P1.5 的通过范围和支持边界以本轮实施记录及实际验收证据为准。
P5 前可单独报告周期范围完成，但不能宣称首版全部范围完成。

### 8.1 检查矩阵

| 编号 | 必需检查 | 通过判据 |
|---|---|---|
| V-01 构建/归属 | CPU-only、相关 CUDA 构建、公共数学归属及依赖 | 新 `.cpp/.cu` 真正进入目标，无 CPU 强依赖 CUDA，无重复公式。 |
| V-02 重构回归 | none/external；Euler/RK2/RK3；RKL1/RKL2；Burn；AMR/restart | 同 backend/构建条件比较接受场、拓扑、控制状态与阶段轨迹；纯搬迁不改变运算顺序，争取精确一致，计时与来源元数据单独处理。 |
| V-03 算子/边界 | 解析/制造解、常数与线性场、周期零模、非齐次 BC | 由独立定义计算期望值；符合固定符号、规范、通量与残差规则。 |
| V-04 空间收敛 | 平滑 Cartesian 均匀网格及 AMR manufactured solution | 至少三档分辨率；均匀二阶方案进入渐近区后 phi/g 观察阶数目标不低于 1.8；AMR 界面/全域阶数按 D-02/D-04 明确，不能仅看单一全局范数掩盖界面问题。 |
| V-05 MG 收敛 | 不同块数、层数、网格尺寸、零/非零初值 | 真实 residual 达标，迭代退化与内存增长受记录；最大循环、NaN、相容性失败被拒绝。 |
| V-06 阶段/场版本 | 每个 RK 输入 slot；失败、过期、reflux、重网格 | 正确字段被消费；不发生逐 patch 全域 solve；无 stale pointer/错误发布。 |
| V-07 物理耦合 | 周期线性 Jeans 模式或其他独立时间参照；对称自力；适用能量预算 | 达到预先冻结的时间阶数、动量与能量预算；不得仅用 CPU/GPU 互相一致证明物理正确。 |
| V-08 AMR/restart | refine/coarsen/no-change；事务失败；连续与恢复轨迹 | accepted 状态一致，失败不半发布，场有效性正确，重启按定义收敛到同一误差预算。 |
| V-09 孤立边界 | 三维解析/独立参考源、边界阶数与计算域大小变化 | 单独区分离散误差与有限边界误差，力方向/远场趋势正确。 |
| V-10 CUDA | 与独立参考和 CPU 对照、GPU 内存/竞争检查、device residency | 相同物理预算通过，未完成工作不发布，无未说明传输和资源错误。 |
| V-11 性能 | 小/中/大规则网格；不同 AMR 占比/深度；重复求解与 regrid | 第 7 节规定的端到端加速、适用范围与退化点均有最终构建证据。 |
| V-12 GUI/Core 兼容 | 受影响的命令/响应、配置目录、初始场/AMR、session、单位/来源与资源提示 | 第 10.3–10.5 节契约保持；API 能编译且响应含义正确，不能只以 Git 无冲突判通过。 |
| V-13 低密度前置基线 | P1.5 的 NV-01–12、策略迁移、修复预算和有效域 | 科学/后端/失败/性能结论分开，记录实际通过组合；前置验收未完成不得称已进入合格 P2 基线。 |

先复用现有 `test_shared_stage_scheduler`、`test_state_residency`、`test_compute_backend`、
`test_topology_transaction`、boundary/reduction、CUDA regrid 测试及
[AMR restart 工具](../../tools/validate_cuda_amr_restart.py)。新增测试建议放入
`tests/math/test_elliptic_operator.cpp`、`tests/host/test_multigrid.cpp`、
`tests/host/test_self_gravity_lifecycle.cpp`、`tests/cuda/test_cuda_multigrid.cu`；
物理验证输入和结果放在新的 `validation/gravity/` 子域并接入现有验证索引。
具体构建目标名在创建时记录，不把尚不存在的命令写成已执行。

缺少 GPU 或某依赖时记录“未执行/环境不可用”，该能力不能标通过。
复用历史证据必须保留原源码/二进制身份；新修改由针对性检查覆盖，不机械重复无关的大型验证矩阵。

## 9. 实现前的决策记录

这些项目是待完成的技术工作，不代表必须逐项向用户提问。实现者应在默认范围内给出选择、推导、反例与验证预算。

| 编号 | 需要冻结的决定 | 本计划默认方向 | 最迟完成时间 | 当前状态 |
|---|---|---|---|---|
| D-01 | G/单位、维度含义、质量源、phi/g 位置、边界键与错误处理 | 沿用 GUI configuration v2 的 CGS 和 Core G 常数；气体密度、cell phi、兼容面梯度、独立 gravity BC | P2 前；孤立部分 P5 前 | P2 已冻结：cell 势、兼容面梯度、周期/给定势边界；见 P2 专项记录，孤立边界仍待 P5 |
| D-02 | 离散算子、粗细面、粗层算子、transfer、平滑器与 bottom solver | matrix-free composite GMG/FAC；独立辅助层；Jacobi 原型 | P2 前冻结单层，P3 前冻结 AMR | P2 已冻结：重离散 V-cycle、带符号 transfer、Jacobi、≤64 未知量的既有 dense LU；P3 待冻结 |
| D-03 | RK 阶段时间、动量/能量源项、reflux 后处理与重力步长 | 每阶段密度求解，保持已有分裂顺序，显式定义能量预算 | P4 前 | 待推导 |
| D-04 | atol/rtol、最大循环、力误差、时间/空间阶数、物理误差预算 | 第 6 节残差定义；预算先于候选结果固定 | 每个对应测试实施前 | P2 的残差、≥1.8 空间阶数、32 格 1% CGS 误差预算已冻结；时间/AMR 预算待相应阶段 |
| D-05 | 孤立有限域边界的算法、中心/阶数和失败策略 | 三维多极边界优先评估；不引入 FFT | P5 前 | 待评估 |
| D-06 | checkpoint 与输出字段、restart 初值、来源身份 | phi/g 作为可重建字段；恢复后重新求解，保存物理配置和求解精度身份 | P4 前 | 待冻结 |
| D-07 | 设备批布局、缓存键、coarse 路线、收敛检查频率和基准案例 | 设备驻留；按层批执行；基于测量决定 coarse 策略 | P6 前 | 待冻结 |
| D-08 | 低密度状态语义、内置兜底、正性方法、修复预算与支持域 | 复用已有物理下限；内部固定保守保护→可恢复状态兜底记账→不可恢复时报错；不新增模式开关或全局 EPS | P1.5 N0；实现与验收在正式 P2 前 | 专项计划 v0.4；manifest、共用状态/通量实现、修复账本已接入，验收见实施记录 |
| D-09 | 现有物理控制的真实消费者及后续扩参边界 | 专项计划 PC-01–10 及退役清单 PR/ER/CR；删除失效控制/旧别名，登记 3 个既有 Advanced 键，收敛旧 EOS；未来 self 最小 4 个拟议入口随实现发布 | P1.5 契约整理；后续每次新增物理参数前 | 删除/登记已实施；参数归属和运行验证见 P1.5 实施记录 |

P1 已冻结 D-03 中的 Hydro 输入采样位置：Euler 为 `t`，RK2 为 `t,t+dt`，
SSPRK3 为 `t,t+dt,t+dt/2`，分别消费 `Current`、`Scratch`、`Next` 的实际版本。
这不提前决定自引力的能量源项、reflux 校正或物理步长限制。

若选择持久化 phi 以改善 restart warm start，必须说明它对迭代数与有限容差轨迹的影响。
不能保证 bitwise restart 时应明确采用的物理/数值误差预算，不能只删除历史一致性检查。

物理控制参数优先沿用现有 `PhysicsConfig`、物性/网络登记和 Core 元数据。
自引力复用 `gravity_type=self` 与 `gravity_G`，不再加一个启用开关、G 常数或专属密度 floor。
独立引力边界等新物理自由度按 D-09 说明必要性；MG 的内部平滑/退化细节由算法封装，
有效精度目标与合理默认值保留在求解接口。必要的缓存、设备 view、有效配置和诊断可以存在，
但不作为多份可修改的输入来源。ODE 小参数仅在严重控制风险下进入本轮修复范围。

## 10. GUI/CUDA 协作、执行顺序与证据

### 10.1 实测分支关系与评估边界

2026-09-21 已拉取远端 main。由于原本地 main 与远端分叉，保留原分支历史，在当前目录建立
`codex/main-maintainability-review`，基于 `01cc4f72` 开展审查；已有 README 改动和计划均已恢复。
没有执行实际代码重构、功能删除、提交或推送。后续实现仍须确认届时上游及未提交改动，锁定对应基准。

同日核对远端 GUI 契约分支 `d98f6f6e`：其 merge-base 就是上述 main，
`main...ui-contract` 左/右独有提交数为 **0/6**。该分支相对 main 新增 125 个文件、修改 25 个文件；
没有跟踪 `studio/` 文件，也没有合入前端分支历史。因而本次评估的是 **Core API 分支的整合条件**，
不是整个 Studio 当前 HEAD 的合并/界面验收。当前两棵已提交树具备快进关系；未来重构尚不存在，
不能据此保证将来无冲突。当前目录有其他未提交/未跟踪工作，不能直接将这个拓扑结论当成切换工作区的许可。

该分支的六个增量为 `40b7704d`、`47517d1c`、`91a46f8f`、`5e96d4f0`、`97a2b50c`、`d98f6f6e`。
后两项已增加真实初始 AMR、注册模型检查及 CPU 持续预览；不得仍按“只增加 JSON 壳层”估计影响。
源码/协议依据固定在[这份分支快照](https://github.com/Shiro-Akane/ARCH/tree/d98f6f6e853ccb23eaa916ab1d20356019087622/src/api)：
`ApplicationContract.h`、`CONFIGURATION_API.md`、`INITIAL_AMR_API.md`、`PREVIEW_SESSION_HANDOFF.md`。
交接文档记录 18/18 组 CPU 检查通过、未编译 CUDA；本轮只核对源码与记录，未重跑这些检查。

### 10.2 与重构计划重叠的实际接口

| 位置/当前变化 | 未来可能的问题 | 本计划的处理 |
|---|---|---|
| `Driver.h` 将 EOS 细化量绑定提取到 `amr/refinement/RefinementThermodynamics.h`；`SolverDispatch.cpp` 与 API 共用 `driver/initialization/InitialMesh.h` | P1 从旧实现重拆会冲突、漏掉预览消费者或重复初始化 | 先整合共享入口，再提取 Driver；保留生产与预览的共同数值路径。 |
| `api/preview/InitialMesh.h` 直接消费 AMRControl/tree/pool、ghost exchange、Regrid、BCHandler | AMR 签名/布局迁移可能编译失败；自动合并后也可能改变预算停止点、插值或 2:1 网格 | 变更由 Core 同步适配 API，保持预览与正式 CPU t=0 的逐块比较；大规模 AMR 搬迁后置。 |
| `RuntimeParams.h`、`StandardParameters.h`、`ConfigParser.h`、`GlobalDefs.h`、API Configuration | gravity 新键只进运行解析会使表单、默认值、单位或可用性失真 | 标准键、解析、配置元数据与执行 capability 同次接入；GUI 消费权威结果。 |
| `ProblemGenerator.h`、`GenericProblem.h`、ProblemRegistry、Grid、InitialStateConversion | 清理 includes 或初始化流程会破坏检查钩子、模型枚举、点采样及源关联 | 按新接口复核依赖；已有 inline 检查体需要完整类型，不能照旧 main 的引用接口判断直接改成全前置声明。 |
| `eosdispatch.h`、InspectionEosCache、FileFingerprint/VerifiedFileCache、Species | EOS owner/view 拆分可能使跨请求缓存悬空、错配组分或接受过期表数据 | R4 EOS 整理延后；保留请求前后内容校验、拥有的组分存储、失败清理与历史回调类型。 |
| `src/main.cpp`、Application.cmake、HostTests.cmake、案例源码指纹 | 新 `.cpp` 漏编译、命令先启动模拟、源路径迁移后单位证据失效 | 维护独立早期 API 入口；同步源清单、CTest 和单位证据，不能只修 include。 |
| gravity/numerics/MG 新实现及 CUDA 新 executor | 新增文件本身文本冲突较少，但场/阶段/资源与输出语义可能影响 GUI | 算法可独立推进；接入公共生命周期、字段输出与能力声明时执行 V-12。 |

此外，`97a2b50c` 已改变标准单位契约为 CGS，并将 IdealGas 无组分回退 Cv 从 718 改为 7.18e6；
标准数值解析也已趋严格。这些是该分支已有的行为变化，必须在 Core 接入项中单独核验并记录影响，
不能计入“行为不变的重构”。重构对照基线采用确认后的集成版本；本计划不授权借此再修改其他物理数值。

### 10.3 必须保持的 GUI/Core 契约

| 编号 | 约束 |
|---|---|
| UI-01 | 保持 CLI、响应 kind、字段含义、错误/limited 语义与退出码；当前外层 schemaVersion=1.0、configuration=2，其他扩展分别协商。内部改路径不改协议；破坏性变化用新版本并配套调用方。 |
| UI-02 | 配置检查不执行 Setup/EOS/CUDA；真实场/AMR 预览可执行必要的 CPU 初始化，但不启动时间推进或生产 IO。GUI 不依赖 Driver 私有布局，也不复制科学公式。 |
| UI-03 | 保留 configRevision 对原始输入的摘要、requestId/caseId、来源校验及 Host 的项目/binary/进程身份。session 失败清缓存、旧响应淘汰和旧图状态保持，不借重构改变缓存有效期。 |
| UI-04 | P4/P6 按实际验证组合发布 self capability。同步修改 Configuration 中 self unavailable、gravity_G applicability/原因及新 BC/MG 参数；“键可识别”“配置合法”“可预览”“模拟可运行”分别表达。 |
| UI-05 | `--preview-amr` 的叶块逻辑身份、2:1 平衡、complete/limited、最后完整快照及资源预算继续明确。新增 gravity 不应让普通初始场/网格预览隐式执行全域 Poisson；势/力预览如需支持，应另行声明能力与预算。 |
| UI-06 | 当前资源提示按三份流体状态、每份六个基础数组计算，并排除临时存储；不能直接当成 MG 或 GPU 峰值。接入 gravity 时单列已知的 phi/g/MG/workspace 范围及未估算项，保留原字段语义，不虚报可运行内存。 |
| UI-07 | 保留 CGS、坐标和场单位；phi/g 新字段分别声明单位与离散位置。修改案例 `.cpp` 的包括 include 在内的字节会改变编译源码摘要，须重新核对 CaseUnitEvidence；不得自动刷新哈希掩盖未审查内容。 |

当前 AMR 预览与正式启动共同使用根初始化和细化数学，但预览另有有界控制循环。
先保持数值一致；只有两端确实需要共同演进时，才提取不依赖 GUI JSON、MG 或 CUDA 的初始化服务。
不得为“解耦”复制第二套细化算法，也不把整个模拟主循环包成预览入口。

### 10.4 建议执行顺序及并行范围

| 顺序 | 工作与归属 | 退出条件及下游关系 |
|---|---|---|
| I0 冻结交叉评估 | main `01cc4f72` + Core UI `d98f6f6e`；本轮完成 | 仅证据/计划，不是已合并或已通过数值验收。实施时重查远端和未提交内容。 |
| I1 整合共享 Core 基线 | 已完整继承上述 Core API 增量，再做 Driver 重叠改动 | GUI 既有 18/18 CPU 记录沿用，候选 CPU 主程序与受影响核心检查已通过；按后续授权继续补齐 P1 CUDA 门槛，见第 10.6 节。 |
| I2 最小职责整理 | R1/P1；先已有 Burn/Control 与 IO，再 Runtime/Boundary/Regrid/阶段入口 | none/external、AMR/restart 和受影响 API 保持；无需完成所有 L/F 项，也不先做全仓搬迁。 |
| I3 数学与 CPU 闭环 | P2→P3→P4，随后 P5；gravity/elliptic/MG 保持职责分层 | 单层/复合 AMR/阶段源项分段验收；在 P4 发布能力时同步参数、诊断和资源口径。 |
| I4 CUDA 闭环 | P6；实现前已在 P1 冻结设备 view/完成语义 | 同误差预算的正确性与端到端加速分别通过；Host 预览继续走 CPU。 |
| I5 按收益追加维护 | 选择 R2–R5 候选，按模块形成小批次 | 每项有维护收益和调用方映射；EOS/AMR/API 大搬迁在共同接口稳定后进行，不是自引力完成的强制前置。 |

I1 是开始 **重叠接口重构** 的默认前置；不依赖这些接口的 MG 数学设计、小型独立原型可以同时进行。
若共享 Core 暂时不能进入 main，可用固定的集成分支验证双方，避免在两边从旧 main 重复修改同一职责。
本轮不实际执行 I1。GUI 接收方式要以其届时已包含的提交为准：有共同历史时 merge main，
既有 cherry-pick 时先核对补丁等价性，避免重复摘取六个增量；不重写协作者历史。

GUI 的表单、展示、Host 会话管理和 MG 算法可以独立推进；Core 公共接口迁移必须同步适配 API，
交给 GUI 的应是已构建、协议已检查的基线。这样 GUI 无须追随每个 Driver 内部提交，
但仅靠“以后 merge main”不能修复单位、缓存、资源估算或能力语义的变化。

CUDA 优化继续推进时，按已验证提交和接口变化同步，不自动追逐每次 push。
涉及以下任一内容的上游更新，需要重审 SG-03/04/08/09：density view、slot 映射、
topology/storage generation、exchange/reflux、完成凭证、设备资源退休。
数学叶子或局部 kernel 优化可独立进行；公共接口改变应先更新契约与对应两端适配。
最终验收期间冻结实现与参照构建身份。

### 10.5 接续检查与交付

每个涉及公共边界的增量，同时交付：Core 基准/候选提交、调用方与路径映射、契约版本、
参数/单位/行为变化、能力支持矩阵、相关测试记录、旧 binary 的明确降级或拒绝方式。
正式 IO/checkpoint 新字段要单独说明 reader/writer 兼容性；GUI 不从 C++ 文件名推断物理可用性。

- 配置/参数变化：`configuration_api_contract`、`preview_parameter_reads`、`preview_parameter_metadata`。
- 初始化/Driver/AMR 变化：`preview_initial_conversion`、`preview_api_contract`、`preview_cellular_2d`、`ui_expansion_contract`，加受影响的 AMR/事务/启动与重启检查。
- 模型接口/源文件变化：`initialization_probe`、`case_inspection_contract`，核对实际注册列表、CGS 与失效的单位证据。
- EOS/cache/session 变化：`preview_session_contract`、`preview_verified_resources`、`preview_exact_sample_cache`、`tabular_eos_ideal_gas`。
- 构建/include 迁移：CPU-only 与相关 CUDA 编译、原 CTest 发现映射、架构归属检查；主程序与 API 路径均需覆盖。

上述测试名来自所审分支，不表示当前 main 已注册或本轮已运行。按影响范围选择，不要求每次纯文档更新重跑。
前端接续时再验证“旧 UI/新 Core”和“新 UI/旧 Core”的版本检测、错误/limited、取消与过期响应；
不兼容时应明确提示，不能静默使用错误含义的字段。前端实际分支、构建与界面验证另行记录。

### 10.6 当前进度

- [x] 建立实施约束计划，核对远端 main 与关键源码入口。
- [x] R0：更新 main 工作基准，完成全仓长度/目录/引用/依赖审查并纳入清单。
- [x] I0：核对最新 Core UI 分支、实际重叠接口与协议；以维护收益重新约束清单建议。
- [x] I1：从 GUI Core `d98f6f6e` 建立物理分支，完整继承 6 个提交；既有 GUI 测试沿用。
- [x] P0：冻结 main/GUI 基准、工作分支、CPU 二进制及复用输入，登记阶段时间和延后决策。
- [x] P1：完成 Driver 职责提取、Burn 去重、全域阶段准备与标量/字段身份契约；CPU 与相关 GPU 门槛通过。
- [x] R3/P1 后续整理：294 个文件按职责归类；内部根相对 include、测试引用和两个公开算例头完成；CPU 全目标编译通过，见 [目录记录](layout/README.zh-CN.md)。
- [x] P1.5 规划：完成行业方案比较、低密度状态/参数边界、LD 修复归属、NV 验证矩阵和 P2 接续条件。
- [x] P1.5 配置收敛：取消拟新增模式开关，完成 PC 物理配置清点，明确既有参数复用、内部兜底与未来扩参准入。
- [x] P1.5 退役规划：核对 90 个标准键，列出 5 键/2 成员删除、3 个既有控制登记、6 组 EOS 与 4 组关联清理；不保留已退役输入兼容，清理已执行，当前验收见 P1.5 实施记录。
- [x] P1.5 实施/验收：N0–N5 完成；CPU 55/55、CUDA 构建 129/129、各 72 个 CPU/GPU 实际推进、357 工具测试和 16 次 sanitizer 检查。
- [x] P2：完成单层 CPU Poisson/MG；CPU 57/57、三档解析阶数/CGS 尺度/失败路径、sanitizer 和编译兼容通过，self 生产路由仍关闭。
- [ ] P3：完成 CPU composite AMR 求解。
- [ ] P4：完成 CPU 自引力、动态 AMR 与 restart 闭环。
- [ ] P5：完成孤立边界与准确的支持矩阵。
- [ ] P6：完成 CUDA 正确性；另行记录性能门槛结果。
- [ ] P7：完成最终文档、证据与交付复查。

P1 实现为 `95b858fc`；[验证证据](../../validation/gravity/results/selfgravity-p1-20260921/README.md)
记录 5/5 核心 CTest、102/102 架构工具测试、10 案例/40 检查点零场量误差及 4 组重启对照。
前述记录为 CPU 封包时的历史证据。按用户后续指令，本轮补齐 V-01/I1/R1 的 P1 CUDA 子项并
整理 CI；[GPU 与流程记录](../../validation/gravity/results/selfgravity-p1-gpu-20260921/README.md)
单列实际受测提交、构建、数值/重启与设备检查。GPU 22/22 CTest、6 个常规案例的 21 次比较、
12 条 active-ENUC 路线的 9 次重启比较、4 次 sanitizer 均通过；终点守恒与曲线 AMR smoke 另行记录。
355/355 工具测试与 `74a755e5` 的远端完整 CPU CI 通过，原数值预算未改变。
当前 P1/P1.5 已完成；P2 独立原型的边界、验收和接续见 [P2 专项记录](P2PoissonMultigrid.zh-CN.md)。
P1.5 的已推送基线为 `780794b6`；新数值基线的验收见 [实施记录](P1_5ImplementationReport.zh-CN.md)，不能用 P1 行为保持证据替代。
P2 不要求先完成全仓 R2–R4；P1 GPU 验证不代表 P6 自引力 CUDA 已完成。

每个阶段使用以下格式更新本节，详细测量保存在现有 validation 树中：

```text
阶段/状态：未开始 | 进行中 | 已通过 | 未通过 | 环境不可用
源码基准/候选提交：
关联 SG / D / V 编号：
修改的 owner 与调用方：
构建配置、编译器/依赖、可执行文件身份：
测试输入、运行命令、CPU/GPU 环境：
通过/失败/未执行检查及证据路径：
数值误差与容差；性能若涉及则报告范围和波动：
已知限制、下阶段依赖、本文需要更新的内容：
```

| 日期 | 计划变更 | 依据与影响 |
|---|---|---|
| 2026-09-19 | 建立版本 0.1 | 合并 Driver 拆分、自引力/MG、AMR、CUDA 与可选稀疏 provider 的实施边界；未启动源码实现。 |
| 2026-09-21 | 扩充为版本 0.2 | 拉取 main，保留本地分叉历史；新增第 12 节与全仓清单，明确不删改功能及 Fortran 转译数学豁免。 |
| 2026-09-21 | 扩充为版本 0.3 | 核对 Core UI `d98f6f6e`；新增共享接口、兼容检查与整合顺序。长度/目录阈值仅作线索，取消 Driver 行数目标，全部拆并建议按维护收益选择。 |
| 2026-09-21 | 实施版本 0.4 | 按用户新授权建立 `physics/selfgravity` 并完整继承 GUI Core；完成 P0/P1 CPU 工作和封包。实际阶段绑定集中到 `DriverStages`，Boundary/Regrid 共用 Runtime 声明；算子空壳延后到首个消费者。GUI 旧测试不重跑，所有 CUDA 验证后置。 |
| 2026-09-22 | 验证版本 0.5 | 按后续授权补齐 P1 Release CUDA 构建、契约/数值/重启/sanitizer 验证；CI 接入物理分支、按需下载 LFS，审计排除构建产物，Host 比较器解除 CUDA 链接依赖。维持原数值预算并拒绝漏跑/skip，仍停在 P1。 |
| 2026-09-22 | 维护版本 0.6 | GPU 封包后按新授权完成目录分类、项目根相对引用及公开算例头；更新已审核案例源码摘要，CPU 编译通过。沿用 GUI 已交付 CGS，修正过期单位说明；无物理实现修改，仍停 P1。 |
| 2026-09-22 | 规划版本 0.7 | 增加 P1.5 低密度可靠性修复：物理参数与内部数值保护分离、统一状态语义、保守通量与显式修复记账；定义 CPU 后集中 CUDA 验收及 P2 接续门槛。补充候选密度后的引力能量完成点；本轮仅文档，未实施修复。 |
| 2026-09-22 | 规划版本 0.8 | 按用户要求取消 P1.5 新增配置键和模式选择，复用已有物理下限、内置受约束兜底。静态清点现有物理参数、未消费项和派生状态；重点复核燃烧阈值混用，普通 ODE 小参数不全面整理。新增物理控制须证明独立含义及真实消费者；仅修改计划。 |
| 2026-09-22 | 规划版本 0.9 | 按最新要求允许不兼容退役：清点失效键、旧别名、旧 EOS 与独占设施；区分 strict free-energy、native 与规范化 direct，保护 Helm 共用反解和来源数学。允许将已有 3 个时间步控制登记为标准 Advanced；列出未来 self 最小 4 个拟议入口。只更新规划，无代码删除或新运行验证。 |

| 2026-09-22 | 实施版本 1.0 | 完成 P1.5 N0–N5：复用既有物理下限，统一可接受状态、保守通量、修复账本、严格 EOS 和有界失败；清理失效配置与旧设施。CPU/CUDA 同一物理矩阵、全量回归与 sanitizer 通过，CI 增加低密度运行门槛；证据见实施记录，P2 尚未开始。 |
| 2026-09-22 | 实施版本 1.1 | 按新授权完成 P2 独立 CPU Poisson/MG：冻结单层算子/BC/transfer/粗解与预算；完成 1D/2D/3D 解析、弱扰动、CGS 尺度、失败路径和 CPU 57/57。复用小型 LU，新增代码通过 CUDA 编译检查；不新增配置键，不启用 self，不越过 P3/P4/P6。 |

## 11. 参考依据

本计划的 ARCH 文件与状态判断来自第 4.1 节所列的 main 源码，实施时以新锁定的提交复核。
外部资料只用于核对算法分层与实现考虑，不代表引入这些框架作为依赖，也不构成 ARCH 的验收证据。

- AMReX 将线性算子与多重网格求解分开，并提供多 AMR 层 composite solve 与粗层聚合接口；用于核对本计划的模块分工和粗层组织方向。[MLMG and Linear Operator Classes](https://amrex-codes.github.io/amrex/docs_html/LinearSolvers.html)
- Castro 的引力文档讨论 Poisson 求解和孤立边界的多极处理；用于后续 D-05 的方法比较。[Gravity](https://amrex-astro.github.io/Castro/docs/gravity.html)
- NVIDIA 建议减少 Host/Device 传输、让中间数组留在设备并批处理小传输；对应 SG-09 的执行约束。[CUDA Best Practices: Data Transfer Between Host and Device](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)

## 12. 全仓可维护性重构：本计划的新增约束

### 12.1 已完成的审查与结论

对 `01cc4f72` 的 18,205 个跟踪文件完成清点。计入 3 个 CUDA `.cu.in` 生成模板后，
持续维护的代码共 461 个，其中 42 个大于 600 行、106 个小于 60 行。
42 个大文件分别记录：19 项优先评估提取、14 项后续评估、5 项来源数学豁免、4 项保持现状。
全部短文件都有单独的保留/收窄/候选处理理由。目录共 36 个命中直接文件数大于 8，
其中 22 个维护目录给出分类判断，14 个历史证据目录保持原结构。

完整逐项清单见[main 全仓可维护性审查](MainMaintainabilityAudit.zh-CN.md)：

- L 项：大文件可能的职责提取边界，含测试/工具与数学豁免；不是逐项拆分任务。
- S 项：短文件是否有独立存在的职责、构建或 ABI 理由。
- DC 项：无消费者候选、休眠接口、来源公式与保留依据。
- DU 项：重复遍历、读取校验、指纹辅助及正常的声明/类型绑定。
- F 项：二级目录建议、保留理由和迁移连带修改。

长度阈值用于发现问题，不是机械验收上限。主要目标是维护边界清晰，保留全部受支持功能、
输入格式、边界条件、失败语义、数值规则与测试覆盖。大文件可因单一职责或来源约束保留；
短文件可因轻量声明、独立编译和公共数学权威保留。

具体选择以“同一职责是否共同变化、修改时是否需要跨越更多文件、依赖是否收窄、
资源生命周期是否更容易追踪、是否保持模板编译隔离”为依据。结构整理后需要频繁往返跳转、
大量透传参数或互相 include，说明拆分边界可能不合适，应合并同一职责或保留原结构。
新增目录必须有稳定的功能分类；当前一个职责只有少量文件时可直接放在已有目录。

### 12.2 功能保持与来源保护

| 编号 | 本轮及后续重构必须满足的条件 |
|---|---|
| MR-01 | 纯维护重构保持受支持行为；用户后续明确授权的 PR/ER/CR 退役独立记录为功能范围调整，不要求旧参数别名/旧 EOS 格式兼容。未列入退役集合的功能、模型与拒绝路径继续保持并验证。 |
| MR-02 | 原始 Fortran 转译数学豁免按行数拆分、公式去重、常数替换与表达式改序。清单第 2 节指定的 Timmes/Helm/NSE 主体及休眠 Jacobian 片段保留。 |
| MR-03 | 无直接调用只构成候选：必须核对模板/虚接口、宏注册、生成代码、CMake、测试和外部接口承诺。无法排除契约时保留，不为完成“清理数量”删除。 |
| MR-04 | 保留数学权威与独立测试 oracle；共享遍历/分配辅助可以合并，算法、精度预算及独立参照不能被合并掩盖。 |
| MR-05 | 保留 CUDA 类型绑定小 TU、noinline/inlining 边界、归约顺序、状态发布和资源退休；拆分不得增加不必要的数据传输或同步。 |
| MR-06 | 目录调整与职责提取分成可审查的小步。更新 CMake、include、模板生成、测试发现、路径断言、文档和受影响的 provenance 规则，不能靠长期多层转发头隐藏旧结构。 |
| MR-07 | 历史结果、失败反例、科学输入表和第三方来源记录维持原身份与引用。改善索引可以进行，不能将 evidence 当死代码重排或覆盖。 |
| MR-08 | 重构前后记录功能/测试清单映射；保留特性的测试覆盖、错误路径和数值预算不变。已退役特性的断言可移除或改为拒绝检查，须注明支持范围变更。新 owner 的测试要验证行为和调用归属，不能删除位置断言后空称通过。 |
| MR-09 | 拆分/合并/目录分类均需说明维护收益；不得以行数、文件数或清单完成率验收。共享接口小文件、内聚的大实现及独立编译 TU 均可长期保留。 |
| MR-10 | 主清单是 main 快照；整合 GUI/CUDA 后重新查实际消费者。按第 10 节同步修改 Core API 并验证，不删除新分支已使用的所谓“死接口”。 |

### 12.3 实施工作包与 gravity 的依赖

| 工作包 | 范围 | 与既有阶段的关系 | 当前状态 |
|---|---|---|---|
| R0 全仓清点 | 基准、阈值清单、Fortran 豁免、引用/目录/include 审查 | 为本计划提供可复查清单；不代表构建/数值基线已完成 | 已完成 |
| R1 gravity 必需的职责边界 | Driver 与窄 Runtime/Boundary/Regrid、AMR 标量适配所需界面、阶段准备契约 | 与 P1 合并安排；AMR 几何/数值适配继续随 P2/P3 实施 | P1 部分已完成，GPU 补验状态见第 10.6 节 |
| R2 机械重复的收拢 | Burn 半步、Host flux traversal、Tabular loader 校验、AMR byte fingerprint | DU-01 与 P1 协同；其他项目可独立，不阻塞单层 MG | DU-01 已收拢，其余未开始 |
| R3 目录与 include | 按职责分类生产代码、测试和 CMake；规范公开与内部 include | 后续明确授权覆盖本轮目录整理；保持公开 API、工具 CLI 与历史记录入口，CPU 编译验收 | 已完成 294 项迁移、公开算例头及 include 统一；完整映射和例外见 [目录记录](layout/README.zh-CN.md) |
| R4 次级大文件/测试/工具 | 按需评估 EOS Host owner/view、CUDA resource/control、测试责任分组、validation CLI | 保持保留路线的公开入口与案例覆盖；EOS 收敛按 ER 清单执行并对接 GUI session 生命周期，其他项可独立进行 | 未开始 |
| R5 兼容入口复核 | DC-01 的旧别名头等非数学候选 | 已核实的 PR/ER/CR 按最新用户要求退役，无兼容转发；其他候选继续核对真实消费者，不按文件年龄删除 | 退役清单已实施，P1.5 CPU/CUDA 验收完成 |

不要求完成所有 R 工作包后才进入 P2。gravity 的硬前置是相关 R1/P1 契约与验收；
无关 EOS、测试目录和工具整理不能把自引力无限延期。每个 R 工作包仍遵守 SG-02，
不在同一提交中同时搬迁实现、修改算法和增加新的物理支持范围。

文件夹分类方向参考清单 F 项，实施前按实际职责选取子集。例如 driver 可以分 `runtime/`、`stages/`、`schedule/`、`io/`；
AMR 可分 topology/exchange/regrid/flux；CUDA microphysics 可分 eos owners/burn/linalg，
为 MG 粗层 provider 提供清晰归属。少量且职责明确的 core/diffusion/timmes_common 文件不强制增层。

### 12.4 重构验收与复查顺序

1. **维护收益与清单复核**：说明为何提取、合并或保持；本次关联哪些 L/S/DC/DU/F、SG/MR/UI 项；确认来源豁免和最新分支消费者。
2. **构建接线**：核对显式/生成源归属、CPU-only 与相关 CUDA targets；短绑定 TU 不因目录变化漏编译。
3. **测试映射**：保留特性的原测试入口、发现数量、场景矩阵、失败路径和独立 oracle 均有对应位置；PR/ER/CR 退役项另记移除或拒绝检查的映射。Python 新子目录必须按现有发现方式可被执行。
4. **数值与生命周期**：复用 V-02、V-06、V-08、V-10 的受影响部分；纯迁移保留相同浮点顺序时，同 backend 对照应争取精确一致。
5. **资源与可维护性**：在受影响 TU 记录增量编译时间/峰值内存，检查 CUDA launch/传输/同步是否增长；不以文件数减少作为性能证据。
6. **文档与交接**：更新 owner、include 入口与清单状态；保留原始基准行数，在阶段记录注明新文件映射和验收身份。

本轮对干净 main 导出的原样架构审计退出码为 0；词法检查未报告其规则覆盖的 include 环或失效项目引用。
工作区直接运行时出现的 3 条 CUDA glob 错误来自 `build-cpu/_deps/suitesparse-src/` 的第三方构建文件，
已定位为审计扫描边界问题，列入 R3。解决时应保留对真实生成源和已配置外部网络的检查，
不能用“只查 tracked 文件”掩盖会参与构建的新增源码。

上述 main 清点保留原始快照口径；v0.4 已实施 P1，v0.5 补齐 GPU/流程记录，v0.6 完成授权目录/include 整理，v0.7 增加 P1.5 数值修复规划，v0.8 收敛配置面和扩参边界，v0.9 明确旧实现退役与必要 Advanced 登记，v1.0 完成 P1.5 实施验收，v1.1 接续 P2 单层原型与解析验收，当前状态以第 10.6 节为准。
本次整合还修正审计器对 GUI 严格布尔错误和 schema 默认值导出的两处过期规则，并新增反例检查；
构建目录/其他 worktree 的扫描范围问题暂未改工具，通过包含全部候选源码的干净快照执行审计。
“审查完成”不等于“重构完成”“没有其他死代码”或“加速已实现”。
