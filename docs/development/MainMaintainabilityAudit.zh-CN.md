# main 全仓可维护性审查清单

本清单是[自引力与 Driver 实施计划](SelfGravityImplementationPlan.zh-CN.md)第 12 节的组成部分，提供逐文件证据与建议。main 快照审查完成；P1 局部实施更新见第 11 节，原始计数保留。

**处置规则（2026-09-21 补充）：维护性优先，相关/同一职责放在一起。** 行数和目录文件数只作检查线索，
表内“优先评估提取”“后续评估”“建议分类”均为候选，不是必须完成的拆并清单。
开始实施前说明维护收益；若只增加文件跳转、透传或迁移冲突，应保留或收拢同一职责。
主计划第 10 节的 GUI/Core 集成顺序优先于本表的局部提取/搬迁建议。

**后续决定（2026-09-22）：** 用户已明确不要求旧输入及已退役 EOS 兼容。
[物理配置与旧 EOS 退役清单](ParameterRetirementAudit.zh-CN.md)的 PR/ER/CR 集合覆盖本表对这些项目的
“默认保留兼容”建议，特别是 DC-01 的旧别名头及 DC-08 的旧配置/规范化 EOS 路线。
本表其余历史证据与来源数学豁免保留；有实际用途的 native/strict EOS 不属于笼统删除对象。
此补充是当前规划，删除尚未实施。

基准：`01cc4f723e674d47fe23850e7c0fef221e92e98c`（2026-09-21 拉取的远端 main）；审查工作分支：`codex/main-maintainability-review`。本地原 main 的 `33abca61` 历史保留，已有计划与 README 改动已恢复，未跟踪的 studio 独立开发目录未纳入 main 审查。

## 1. 范围、计数与方法

- 清点全部 **18,205 个 main 跟踪文件**：其中 690 个属于持续维护树，17,515 个属于 `results/` 或 `archive/` 历史/证据树。文件夹计数只统计该层直接文件，不把后代文件累计上来。
- 持续维护的代码/构建模板共 **461 个**，包含 C/C++、CUDA、Python、CMake、`.inc` 与 3 个 `.cu.in`。其中 **42 个大于 600 行，106 个小于 60 行**；恰好 600/60 行不命中。包含空行和注释，统计物理行数，不以行数替代职责判断。
- 历史树另有 58 个代码文件，其中 5 个大于 600 行、8 个小于 60 行；单独列为保留证据。配置、文档、表格和二进制同样清点，但不套用可执行代码的拆分规则。
- main 中 **36 个目录**直接文件数大于 8：22 个持续维护目录逐项评估，14 个历史证据目录保留结构。新增计划/审查文档不在冻结的 main 计数中。
- 完成元数据全量扫描、阈值文件的职责/入口审查、tracked source 引用查找、局部 include 图和重复代码窗口筛查；对候选片段继续读取调用方与构建入口。这不是所有配置下的全程序不可达证明，也不保证找尽所有重复实现。
- 清单范围不包含 build 产物、下载的第三方依赖、未跟踪的 Studio 工程；它们均未被改写。不把冻结日志当代码拆分，不将“仓内无引用”直接等同于允许删除公开接口。审计器初跑误扫构建依赖的情况单列在第 9 节。

| 范围 | 持续维护代码 | >600 行 | <60 行 |
|---|---:|---:|---:|
| `src/` | 270 | 19 | 75 |
| `tests/` | 115 | 21 | 14 |
| `tools/` | 17 | 2 | 0 |
| `cmake/` | 16 | 0 | 7 |
| `simulation/` | 11 | 0 | 2 |
| `validation/` | 30 | 0 | 6 |
| `examples/` | 1 | 0 | 1 |
| 根 CMakeLists.txt | 1 | 0 | 1 |

## 2. 转译数学与来源豁免

用户明确豁免原始 Fortran 转译数学。此范围禁止按行数拆公式、去重反应率、调整常数、改表达式次序或删除未调用的转译片段。可改的是外围 ARCH 调度、存储、加载与绑定；必须保持来源追踪、接口和数值行为。

| 范围 | 处理 |
|---|---|
| 四套 network 的 `TimmesRateLibrary.h`、`TimmesRhs.inc`，三套 `TimmesJacobian.inc` | 原样保留，包括低于 60 行或当前生产不消费的部分。 |
| `timmes_common/Ecapnuc.h`、`ScreeningTimmes.h`、`TfactorsData.h`、`NuclearConstants.h` | 保留源公式、温度因子与原始常数；不误合并为中央常数的新口径。 |
| `physics/eos/HelmEos.h`、`physics/nse/nse_solver.h` | 数学主体保守纳入来源豁免；外围 owner 单独评估。 |
| `physics/diffusionCoe/diffusion_math.hpp` | 333 行，不命中大文件阈值；直接来源是 AMReX Microphysics C++，不是本轮新认定的 Fortran 直译。仍按来源数学保持，不借重构改输运模型。 |
| 表格 EOS 的 HDF5/ASCII loader | 使用 Fortran 数组顺序不代表整个 loader 是转译公式；可拆读取/校验，但保留坐标顺序、来源常数和全部格式。 |

## 3. 全部 >600 行的持续维护代码

“优先”指值得先评估维护收益与 gravity 相关度，不表示每个文件都必须拆分；主计划 R 阶段规定依赖。以下共 42 项，5 项数学豁免。

| 编号 | 文件 | 行数 | 判断 | 具体边界 |
|---|---|---:|---|---|
| L-01 | [src/amr/AmrTree.h](../../src/amr/topology/AmrTree.h) | 1039 | 优先评估提取 | 树/邻接/平衡保留为 topology owner；PreparedRegrid 与 prepare/migrate/publish/rollback 提取到 regrid 模块，保持事务与私有状态一致。 |
| L-02 | [src/amr/ExchangePlan.h](../../src/amr/exchange/ExchangePlan.h) | 712 | 优先评估提取 | 逻辑同层计划、Host lowering 和 Host executor/workspace 分离；指纹字节顺序保持。 |
| L-03 | [src/amr/GhostExchange.h](../../src/amr/exchange/GhostExchange.h) | 706 | 优先评估提取 | 计划缓存/构建与 Host 粗细层执行分离；对外仍保留一个 exchange 协调入口。 |
| L-04 | [src/cuda/diffusion/DiffusionKernels.cuh](../../src/cuda/diffusion/DiffusionKernels.cuh) | 709 | 后续评估 | operator/dt kernels、RKL update kernels、launch validation 分层；共享数学先从 Host 调度头解耦。 |
| L-05 | [src/cuda/runtime/control/CudaBackendResources.cpp](../../src/cuda/runtime/control/CudaBackendResources.cpp) | 884 | 优先评估提取 | stream/event 与基础 storage、BlockRuntime 建立/上传、EOS/species workspace 分离；异常 quiescence/析构顺序不变。 |
| L-06 | [src/cuda/runtime/hydro/CudaBackendHydroControl.cpp](../../src/cuda/runtime/hydro/CudaBackendHydroControl.cpp) | 711 | 优先评估提取 | 同层与粗细层 exchange 协调进入 runtime/amr 的 control 文件；Hydro 阶段、CFL、slot 轮换保留。 |
| L-07 | [src/driver/Driver.h](../../src/driver/Driver.h) | 1499 | 优先评估提取 | 执行主计划 P1；Runtime、Boundary、Regrid、IO、Hydro/Diffusion 分工，Burn 半步去重复。 |
| L-08 | [src/driver/StageScheduler.h](../../src/driver/schedule/StageScheduler.h) | 775 | 优先评估提取 | 可按 plans/descriptors、context/clock/binding、execution 分开；gravity prepare 仍由同一执行路径触发。 |
| L-09 | [src/driver/dispatch/PolicyDescriptor.h](../../src/driver/dispatch/PolicyDescriptor.h) | 851 | 后续评估 | 轻量 IDs/tags、单一 registration 数据、visitor/lookup 分离；不为 CPU/CUDA 各造注册表。 |
| L-10 | [src/numerics/diffusion/DiffFlux.h](../../src/numerics/diffusion/DiffFlux.h) | 803 | 优先评估提取 | 可共享的 face/geometry/stability 数学与 Host 数组遍历分开，便于 GPU 窄包含。 |
| L-11 | [src/numerics/diffusion/DiffusionAMRStages.h](../../src/numerics/diffusion/DiffusionAMRStages.h) | 820 | 优先评估提取 | 公共 cell recurrence 与 Host single/AMR 调度分离；保留表达式求值顺序、reflux 与边界时序。 |
| L-12 | [src/numerics/flux/FluxFunctions.h](../../src/numerics/flux/FluxFunctions.h) | 673 | 后续评估 | 方向/物理通量、分裂通量、Roe/HLL 波速按数学族分类；源项公式与浮点求值次序不改。 |
| L-13 | [src/physics/eos/HelmEos.h](../../src/physics/eos/HelmEos.h) | 791 | 数学豁免 | Timmes Helmholtz 转译公式及插值主体不拆；Host 加载/owner 边界若要移动，另做不触碰公式的独立验证。 |
| L-14 | [src/physics/eos/Tabular3DEOS.h](../../src/physics/eos/tabular/Tabular3DEOS.h) | 966 | 后续评估 | Host storage/loader 与共享 view/query 分离；native/normalized/free-energy 支持均保留，避免增添一套 EOS。 |
| L-15 | [src/physics/eos/Tabular4DEOS.h](../../src/physics/eos/tabular/Tabular4DEOS.h) | 804 | 后续评估 | Host owner 与 4D query 分层；重插值函数的 ARCH_HEAVY_INLINE 边界保持，未测量前不强制内联。 |
| L-16 | [src/physics/network/aprox13/TimmesRateLibrary.h](../../src/physics/network/aprox13/TimmesRateLibrary.h) | 963 | 数学豁免 | public_aprox13.f90 机械转译；保留文件、公式、常数、源行映射与调用顺序。 |
| L-17 | [src/physics/network/aprox19/TimmesRateLibrary.h](../../src/physics/network/aprox19/TimmesRateLibrary.h) | 1364 | 数学豁免 | public_aprox19.f90 机械转译；不按长度拆分，也不跨网络合并相似反应式。 |
| L-18 | [src/physics/network/aprox21/TimmesRateLibrary.h](../../src/physics/network/aprox21/TimmesRateLibrary.h) | 1468 | 数学豁免 | public_aprox21.f90 机械转译；不按长度拆分，也不因函数名相同认定重复算法。 |
| L-19 | [src/physics/nse/nse_solver.h](../../src/physics/nse/nse_solver.h) | 747 | 数学豁免 | Timmes NSE 改编的 Saha/Newton 主体保守纳入豁免；记录 ARCH adapter 边界，但本轮不拆公式。 |
| L-20 | [tests/cuda/test_burn_policy_parity.cu](../../tests/cuda/microphysics/burn/test_burn_policy_parity.cu) | 839 | 后续评估 | route parity、handoff/status、workspace/lifetime 按测试责任分组；保持独立参照与完整路线集合。 |
| L-21 | [tests/cuda/test_cuda_hydro_block.cu](../../tests/cuda/hydro/test_cuda_hydro_block.cu) | 1251 | 优先评估提取 | Hydro route/integrator、EOS owner、burn/diffusion、lifetime/ENUC 分离；保留每类原有 CTest 运行覆盖。 |
| L-22 | [tests/cuda/test_cuda_multiblock_hydro.cu](../../tests/cuda/hydro/test_cuda_multiblock_hydro.cu) | 649 | 保留 | 649 行集中验证多块 Hydro/ghost/indicator 批契约；暂不为超限 49 行增加入口，仅按函数整理。 |
| L-23 | [tests/cuda/test_cuda_single_level_validation.cpp](../../tests/host/io/test_cuda_single_level_validation.cpp) | 1346 | 优先评估提取 | 参数/运行、输出比较、计时与 trace 验证分离，保留 CLI 与原有验证口径。 |
| L-24 | [tests/cuda/test_curvilinear_geometry_smoke.cu](../../tests/cuda/grid/test_curvilinear_geometry_smoke.cu) | 622 | 保留 | 622 行为一套几何组合 smoke；保持案例矩阵，增加场景时再提取 fixture。 |
| L-25 | [tests/cuda/test_diffusion_rkl_parity.cu](../../tests/cuda/numerics/test_diffusion_rkl_parity.cu) | 1538 | 优先评估提取 | operator/dt、RKL recurrence、边界/失败与 workspace 分组；预算检查与独立 reference 保留。 |
| L-26 | [tests/cuda/test_eos_host_device_parity.cu](../../tests/cuda/microphysics/eos/test_eos_host_device_parity.cu) | 1931 | 优先评估提取 | immutable owner/lifetime、Ideal/Helm、Tabular3D/4D 查询分离；数值期望不调用被测实现。 |
| L-27 | [tests/cuda/test_hydro_leaf_parity.cu](../../tests/cuda/hydro/test_hydro_leaf_parity.cu) | 1754 | 优先评估提取 | reconstruction、flux/source、route matrix 与共享测试装置分组；冻结参照值原样迁移。 |
| L-28 | [tests/cuda/test_network_nse_device.cu](../../tests/cuda/microphysics/network/test_network_nse_device.cu) | 3301 | 优先评估提取 | network RHS/Jacobian、NSE、设备存储/热路径分组；大型参照数据进入 fixture，保留其身份。 |
| L-29 | [tests/host/test_amr_operation_plans.cpp](../../tests/host/amr/test_amr_operation_plans.cpp) | 1156 | 后续评估 | 标量数学、逻辑计划/lowering、composition、Host exchange/cache 分组，语义不同的 oracle 保持独立。 |
| L-30 | [tests/host/test_boundary_plan.cpp](../../tests/host/amr/test_boundary_plan.cpp) | 764 | 后续评估 | 边界计划/指纹、Host lowering/BCHandler、scheduler completion 分组；不能删源位置断言而不补行为断言。 |
| L-31 | [tests/host/test_checkpoint_compatibility.cpp](../../tests/host/io/test_checkpoint_compatibility.cpp) | 646 | 后续评估 | 文件指纹、科学身份、HDF5/native composition roundtrip、restart 分组；schema/拒绝规则保留。 |
| L-32 | [tests/host/test_compute_backend.cpp](../../tests/host/driver/test_compute_backend.cpp) | 782 | 后续评估 | storage generation、transfer transaction、batch contracts 分组；FakeBackend 只作 fixture。 |
| L-33 | [tests/host/test_resolved_execution_plan.cpp](../../tests/host/driver/test_resolved_execution_plan.cpp) | 916 | 后续评估 | 参数 aliases/defaults、capabilities、factory 与 NSE auto 分组；不删仍受支持的别名。 |
| L-34 | [tests/host/test_runtime_probe_and_capabilities.cpp](../../tests/host/driver/test_runtime_probe_and_capabilities.cpp) | 675 | 保留 | 同一启动能力矩阵，675 行尚连贯；可提 fixture，不必立即多建可执行文件。 |
| L-35 | [tests/host/test_shared_stage_scheduler.cpp](../../tests/host/driver/test_shared_stage_scheduler.cpp) | 1668 | 优先评估提取 | topology identity、plan/clock、执行/失败发布、生产接线分组；全局 allocation-failure override 必须隔离，防止改变其他测试环境。 |
| L-36 | [tests/host/test_state_residency.cpp](../../tests/host/driver/test_state_residency.cpp) | 738 | 保留 | 状态机转换测试边界明确；先整理 fixture/章节，避免拆散跨转换不变量。 |
| L-37 | [tests/host/test_topology_transaction.cpp](../../tests/host/amr/test_topology_transaction.cpp) | 720 | 后续评估 | 通用 transaction 与真实 PreparedRegrid 故障/回滚分离，仍验证集成后的原子发布。 |
| L-38 | [tests/tooling/test_audit_architecture.py](../../tests/tooling/architecture/test_audit_architecture.py) | 1034 | 优先评估提取 | include graph、数学 authority、CMake/注册、runtime 生命周期按规则族分组；发现路径规则保持可测试。 |
| L-39 | [tests/tooling/test_portable_network_generator.py](../../tests/tooling/network/test_portable_network_generator.py) | 932 | 后续评估 | NSE eligibility、weak storage/坐标、生成 C++ 转换分组；保留运行环境 skip 的准确状态。 |
| L-40 | [tests/tooling/test_validation_provenance.py](../../tests/tooling/validation/test_validation_provenance.py) | 656 | 后续评估 | 源码/二进制来源身份与 qualification 分组；旧 evidence 兼容检查继续保留。 |
| L-41 | [tools/audit_architecture.py](../../tools/audit_architecture.py) | 977 | 优先评估提取 | 文件枚举/词法解析、include graph、数学归属、构建/runtime 规则拆成模块；CLI 保持，先修审查范围和错误定位。 |
| L-42 | [tools/validate_backend_results.py](../../tools/validate_backend_results.py) | 1187 | 优先评估提取 | manifest/参数、运行协调、checkpoint/物理比较、trace/STS/regrid 分离；复用已有 provenance/设备测量 helper。 |

## 4. 全部 <60 行的持续维护代码

短文件大多是声明边界、实例化入口、共享数学叶子或独立测试入口。合并会扩大 include 范围或改变构建内存时，应保留。以下 106 项均有处置理由，不批量合并。

| 编号 | 文件 | 行数 | 判断与理由 |
|---|---|---:|---|
| S-001 | [CMakeLists.txt](../../CMakeLists.txt) | 38 | 保留：项目构建入口；短是分层后的正常结果。 |
| S-002 | [cmake/CudaBurnDenseRoutes.cmake](../../cmake/cuda/CudaBurnDenseRoutes.cmake) | 58 | 保留：构建能力/注册/依赖发现的功能模块；可随 cmake/cuda 分类迁移。 |
| S-003 | [cmake/CudaBurnNetworks.cmake](../../cmake/cuda/CudaBurnNetworks.cmake) | 25 | 保留：构建能力/注册/依赖发现的功能模块；可随 cmake/cuda 分类迁移。 |
| S-004 | [cmake/CudaCodeImages.cmake](../../cmake/cuda/CudaCodeImages.cmake) | 36 | 保留：构建能力/注册/依赖发现的功能模块；可随 cmake/cuda 分类迁移。 |
| S-005 | [cmake/CudaCustomDenseRoute.cu.in](../../cmake/templates/CudaCustomDenseRoute.cu.in) | 40 | 保留：配置生成的 CUDA 绑定模板；集中于 templates，保留独立 TU 粒度。 |
| S-006 | [cmake/FindCuDSS.cmake](../../cmake/dependencies/FindCuDSS.cmake) | 19 | 保留：构建能力/注册/依赖发现的功能模块；可随 cmake/cuda 分类迁移。 |
| S-007 | [cmake/templates/CudaBurnDenseRoute.cu.in](../../cmake/templates/CudaBurnDenseRoute.cu.in) | 45 | 保留：配置生成的 CUDA 绑定模板；集中于 templates，保留独立 TU 粒度。 |
| S-008 | [cmake/templates/CudaBurnSparseOwner.cu.in](../../cmake/templates/CudaBurnSparseOwner.cu.in) | 24 | 保留：配置生成的 CUDA 绑定模板；集中于 templates，保留独立 TU 粒度。 |
| S-009 | [examples/network/CustomNetworkRecipe.py](../../examples/network/CustomNetworkRecipe.py) | 21 | 保留：用户可独立复用的生成配方示例。 |
| S-010 | [simulation/BurnOneZone/BurnOneZone.cpp](../../simulation/BurnOneZone/BurnOneZone.cpp) | 57 | 保留：独立注册的物理算例入口；不是可合并的重复算法。 |
| S-011 | [simulation/ExternalGravity/ExternalGravity.cpp](../../simulation/ExternalGravity/ExternalGravity.cpp) | 51 | 保留：独立注册的物理算例入口；不是可合并的重复算法。 |
| S-012 | [src/amr/AmrDefines.h](../../src/amr/topology/AmrDefines.h) | 32 | 保留：块尺寸/ghost 常量的单一入口，不混入控制逻辑。 |
| S-013 | [src/amr/ConservativeRestriction.h](../../src/amr/transfer/ConservativeRestriction.h) | 45 | 保留：CPU/CUDA 共用的 restriction 数学叶子。 |
| S-014 | [src/core/ArchPortability.h](../../src/core/ArchPortability.h) | 25 | 保留：Host/Device 编译宏；两个 force-inline 名称都有测试消费者。 |
| S-015 | [src/core/CompensatedSum.h](../../src/core/CompensatedSum.h) | 52 | 保留：独立公共补偿求和数学及浮点约束。 |
| S-016 | [src/core/FileFingerprint.h](../../src/core/files/FileFingerprint.h) | 23 | 保留：SHA 接口与实现分离，避免扩大依赖。 |
| S-017 | [src/cuda/amr/RegridMigration.h](../../src/cuda/amr/RegridMigration.h) | 48 | 保留：迁移 kernel 的窄声明与非 owning view。 |
| S-018 | [src/cuda/common/DeviceEosStatus.h](../../src/cuda/common/DeviceEosStatus.h) | 27 | 保留：device EOS 错误状态绑定的共享叶子。 |
| S-019 | [src/cuda/common/DeviceStateFields.cuh](../../src/cuda/common/DeviceStateFields.cuh) | 26 | 保留：设备状态字段映射权威，避免各 kernel 复制。 |
| S-020 | [src/cuda/common/GridMetricsCache.h](../../src/cuda/common/GridMetricsCache.h) | 44 | 保留：几何缓存接口，与 .cu 分离。 |
| S-021 | [src/cuda/hydro/GridGeometryAdapter.cuh](../../src/cuda/hydro/GridGeometryAdapter.cuh) | 32 | 保留：设备 POD 到公共几何 view 的适配，无另一套几何公式。 |
| S-022 | [src/cuda/hydro/HydroFluxPolicies.cuh](../../src/cuda/hydro/policies/HydroFluxPolicies.cuh) | 41 | 保留：调用原通量策略的适配层。 |
| S-023 | `src/cuda/microphysics/burn/SparseBeNrBatch.cuh`（原路径） | 17 | P1.5 按明确退役授权删除无消费者别名头；通用稀疏 ODE 路径保留，见 DC-01。 |
| S-024 | [src/cuda/microphysics/SparseEquilibration.h](../../src/cuda/microphysics/linalg/SparseEquilibration.h) | 25 | 保留：稀疏缩放/原始残差的 CUDA 声明。 |
| S-025 | [src/cuda/microphysics/device_eos_owner_utils.h](../../src/cuda/microphysics/eos/device_eos_owner_utils.h) | 55 | 保留：EOS owners 共用分配/上传辅助；不能复制进各 owner。 |
| S-026 | [src/cuda/microphysics/device_network_owner.h](../../src/cuda/microphysics/network/device_network_owner.h) | 56 | 保留：弱反应表的设备存储生命周期。 |
| S-027 | [src/cuda/microphysics/device_species_owner.h](../../src/cuda/microphysics/network/device_species_owner.h) | 51 | 保留：组分设备所有权；后续可收窄其 IdealGas 依赖。 |
| S-028 | [src/cuda/microphysics/helm_eos_device_owner.h](../../src/cuda/microphysics/eos/owners/helm_eos_device_owner.h) | 53 | 保留：一种 EOS 的设备 owner 声明；重组目录不合并不同存储生命周期。 |
| S-029 | [src/cuda/microphysics/helm_eos_loader.h](../../src/cuda/microphysics/eos/helm_eos_loader.h) | 17 | 保留聚合入口：仍被 runtime internal 与 EOS 测试包含；重组时可明确命名为 owners aggregate。 |
| S-030 | [src/cuda/microphysics/tabular3_eos_device_owner.h](../../src/cuda/microphysics/eos/owners/tabular3_eos_device_owner.h) | 55 | 保留：一种 EOS 的设备 owner 声明；重组目录不合并不同存储生命周期。 |
| S-031 | [src/cuda/microphysics/tabular4_eos_device_owner.h](../../src/cuda/microphysics/eos/owners/tabular4_eos_device_owner.h) | 54 | 保留：一种 EOS 的设备 owner 声明；重组目录不合并不同存储生命周期。 |
| S-032 | [src/cuda/runtime/CudaBackendTypes.h](../../src/cuda/runtime/CudaBackendTypes.h) | 18 | 保留：跨执行单元使用的轻量结果类型，避免包含整个 backend。 |
| S-033 | [src/cuda/runtime/burn/CudaBackendBurnDenseRoutes.h](../../src/cuda/runtime/burn/dense/CudaBackendBurnDenseRoutes.h) | 47 | 保留：类型绑定的声明面，具体实现进入独立编译单元。 |
| S-034 | [src/cuda/runtime/burn/CudaBackendBurnNetworkRouteImpl.cuh](../../src/cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRouteImpl.cuh) | 24 | 保留：小型 route 定义宏，共享委托体；不手工复制到每个 .cu。 |
| S-035 | [src/cuda/runtime/burn/CudaBackendBurnNetworkRoutes.h](../../src/cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRoutes.h) | 52 | 保留：route 声明，与模板重实现隔离。 |
| S-036 | [src/cuda/runtime/burn/CudaBackendBurnReduction.cuh](../../src/cuda/runtime/burn/CudaBackendBurnReduction.cuh) | 58 | 保留：所有 dense burn route 共用归约 kernel。 |
| S-037 | [src/cuda/runtime/burn/CudaBackendBurnRegisteredRoutes.h](../../src/cuda/runtime/burn/dispatch/CudaBackendBurnRegisteredRoutes.h) | 48 | 保留：消费单一注册表的 visitor，不是第二份网络清单。 |
| S-038 | [src/cuda/runtime/burn/CudaBackendBurnSparse.h](../../src/cuda/runtime/burn/sparse/CudaBackendBurnSparse.h) | 57 | 保留：稀疏 owner 的轻量 ABI。 |
| S-039 | [src/cuda/runtime/burn/CudaBackendBurnSparseRoutes.h](../../src/cuda/runtime/burn/sparse/CudaBackendBurnSparseRoutes.h) | 42 | 保留：稀疏 owner route 声明，避免拉入完整 ODE。 |
| S-040 | [src/cuda/runtime/burn/CudaBurnOdeTypes.h](../../src/cuda/runtime/burn/CudaBurnOdeTypes.h) | 31 | 保留：ODE tag 到类型绑定的小型所有者；限定为需要完整类型的消费者。 |
| S-041 | [src/cuda/runtime/burn/routes/CudaBackendBurnHelm.cu](../../src/cuda/runtime/burn/routes/CudaBackendBurnHelm.cu) | 24 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-042 | [src/cuda/runtime/burn/routes/CudaBackendBurnIdeal.cu](../../src/cuda/runtime/burn/routes/CudaBackendBurnIdeal.cu) | 24 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-043 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular3D.cu](../../src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3D.cu) | 48 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-044 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox13.cu](../../src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox13.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-045 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox19.cu](../../src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox19.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-046 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox21.cu](../../src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox21.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-047 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DIso7.cu](../../src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DIso7.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-048 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular4D.cu](../../src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4D.cu) | 48 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-049 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox13.cu](../../src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox13.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-050 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox19.cu](../../src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox19.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-051 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox21.cu](../../src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox21.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-052 | [src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DIso7.cu](../../src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DIso7.cu) | 15 | 保留：EOS/network 独立编译绑定，控制模板编译内存；可按 tabular3/tabular4 分类。 |
| S-053 | [src/cuda/runtime/hydro/CudaBackendHydroHelm.cu](../../src/cuda/runtime/hydro/CudaBackendHydroHelm.cu) | 11 | 保留：单 EOS 的 Hydro 显式实例化入口；合并会改变 NVCC 编译粒度。 |
| S-054 | [src/cuda/runtime/hydro/CudaBackendHydroIdeal.cu](../../src/cuda/runtime/hydro/CudaBackendHydroIdeal.cu) | 11 | 保留：单 EOS 的 Hydro 显式实例化入口；合并会改变 NVCC 编译粒度。 |
| S-055 | [src/cuda/runtime/hydro/CudaBackendHydroTabular3.cu](../../src/cuda/runtime/hydro/CudaBackendHydroTabular3.cu) | 11 | 保留：单 EOS 的 Hydro 显式实例化入口；合并会改变 NVCC 编译粒度。 |
| S-056 | [src/cuda/runtime/hydro/CudaBackendHydroTabular4.cu](../../src/cuda/runtime/hydro/CudaBackendHydroTabular4.cu) | 11 | 保留：单 EOS 的 Hydro 显式实例化入口；合并会改变 NVCC 编译粒度。 |
| S-057 | [src/driver/SolverDispatch.h](../../src/driver/SolverDispatch.h) | 21 | 保留：启动接口与重模板实现隔离。 |
| S-058 | [src/interface/ProblemGenerator.h](../../src/interface/ProblemGenerator.h) | 58 | 保留接口；建议用前置声明收窄 AMRControl/Grid/IdealGas 的传递依赖。 |
| S-059 | [src/io/IO.h](../../src/io/IO.h) | 52 | 保留：IO 公共接口与 HDF5/AMR 实现隔离。 |
| S-060 | [src/io/chk/CheckpointCompatibility.h](../../src/io/chk/CheckpointCompatibility.h) | 46 | 保留：checkpoint identity 契约声明。 |
| S-061 | [src/numerics/burnsolver/Networks.h](../../src/numerics/burnsolver/Networks.h) | 36 | 保留为 factory 专用目录；移除 Driver 无需求的直连前须做自包含编译检查。 |
| S-062 | [src/numerics/burnsolver/OdeContinuation.h](../../src/numerics/burnsolver/ode/OdeContinuation.h) | 34 | 保留：CPU/provider/device 共用 continuation 协议。 |
| S-063 | [src/numerics/diffusion/DiffFunction.h](../../src/numerics/diffusion/DiffFunction.h) | 59 | 保留：RKL 系数声明与 .cpp 的单一数学权威。 |
| S-064 | [src/numerics/diffusion/RKL1TimeIntegrator.h](../../src/numerics/diffusion/RKL1TimeIntegrator.h) | 36 | 保留：RKL1 的薄策略入口，委托公共阶段引擎。 |
| S-065 | [src/numerics/diffusion/RKL2TimeIntegrator.h](../../src/numerics/diffusion/RKL2TimeIntegrator.h) | 36 | 保留：RKL2 的薄策略入口，区别不能因行数被抹去。 |
| S-066 | [src/numerics/integrator/HydroSolverImpl.h](../../src/numerics/integrator/HydroSolverImpl.h) | 59 | 保留：模板策略到 IHydroSolver 的适配，防止 Driver 模板组合膨胀。 |
| S-067 | [src/numerics/linalg/LinearEquilibration.h](../../src/numerics/linalg/LinearEquilibration.h) | 51 | 保留：各 provider 共享的缩放数学。 |
| S-068 | [src/numerics/reconstruction/AMRInterfaceStencil.h](../../src/numerics/reconstruction/AMRInterfaceStencil.h) | 38 | 保留：共同界面 stencil 判据，CPU/CUDA 消费。 |
| S-069 | [src/physics/eos/TabularCompletion.h](../../src/physics/eos/sources/TabularCompletion.h) | 41 | 保留：补全物理成分的独立声明；不能与表格读取混为一层。 |
| S-070 | [src/physics/eos/TabularSource.h](../../src/physics/eos/sources/TabularSource.h) | 41 | 保留：source format/component 元数据契约。 |
| S-071 | [src/physics/eos/eos.h](../../src/physics/eos/eos.h) | 55 | 保留：规范的 EOS 前置声明/别名与 marker；可收窄无必要的 STL/FluidState includes。 |
| S-072 | [src/physics/eos/eos_state.h](../../src/physics/eos/eos_state.h) | 35 | 保留：热力学输入输出 POD，与拥有数据的 EOS 分开。 |
| S-073 | [src/physics/gravity/ExternalGravity.h](../../src/physics/gravity/ExternalGravity.h) | 50 | 保留：Host patch adapter；物理 cell 数学已共享。 |
| S-074 | 原 `src/physics/gravity/ExternalGravitySource.h`，现并入 [GravitySource.h](../../src/physics/gravity/GravitySource.h) | 39（原 main 快照） | P8 合并同职责的小源项头；CPU/CUDA 共用数学仍由一个文件负责。 |
| S-075 | [src/physics/gravity/GravityDispatch.h](../../src/physics/gravity/GravityDispatch.h) | 49 | 保留：resolved gravity factory；未来依主计划增加 self。 |
| S-076 | `src/physics/gravity/GravityNone.h`（原路径） | 29 | P1.5 保留 none 行为，将紧凑策略并入唯一构造入口 `GravityDispatch.h`，移除独立文件。 |
| S-077 | [src/physics/gravity/IGravityPolicy.h](../../src/physics/gravity/IGravityPolicy.h) | 46 | 保留实际 patch 源项接口；P1.5 删除无调用 update_field，未来场更新归 domain service，见 DC-03。 |
| S-078 | [src/physics/network/NuclearEnergy.h](../../src/physics/network/NuclearEnergy.h) | 41 | 保留：组成能量变化的单一数学权威。 |
| S-079 | [src/physics/network/WeakTableView.h](../../src/physics/network/WeakTableView.h) | 46 | 保留：Host/Device 弱率表视图，不拥有设备内存。 |
| S-080 | [src/physics/network/aprox13/NetAprox13.cpp](../../src/physics/network/aprox13/NetAprox13.cpp) | 6 | 保留：包含 NUM_SPECIES/ODE_NEQ 的 static_assert 默认编译检查，不是空 TU；若整合需保留编译覆盖。 |
| S-081 | [src/physics/network/aprox19/NetAprox19.cpp](../../src/physics/network/aprox19/NetAprox19.cpp) | 6 | 保留：包含 NUM_SPECIES/ODE_NEQ 的 static_assert 默认编译检查，不是空 TU；若整合需保留编译覆盖。 |
| S-082 | [src/physics/network/aprox21/NetAprox21.cpp](../../src/physics/network/aprox21/NetAprox21.cpp) | 6 | 保留：包含 NUM_SPECIES/ODE_NEQ 的 static_assert 默认编译检查，不是空 TU；若整合需保留编译覆盖。 |
| S-083 | [src/physics/network/iso7/NetIso7.cpp](../../src/physics/network/iso7/NetIso7.cpp) | 6 | 保留：包含 NUM_SPECIES/ODE_NEQ 的 static_assert 默认编译检查，不是空 TU；若整合需保留编译覆盖。 |
| S-084 | [src/physics/network/iso7/TimmesRhs.inc](../../src/physics/network/iso7/TimmesRhs.inc) | 18 | 数学豁免：短 RHS 仍保留原始网络映射。 |
| S-085 | [src/physics/network/timmes_common/NuclearConstants.h](../../src/physics/network/timmes_common/NuclearConstants.h) | 20 | 数学豁免：保留 Timmes 原始常数约定，不改成别的常数口径。 |
| S-086 | [src/physics/network/timmes_common/RatePair.h](../../src/physics/network/timmes_common/RatePair.h) | 41 | 保留：公共 rate 值/导数 adapter；不随数值公式复制。 |
| S-087 | [tests/cuda/GeneratedSparseBurnFactory.h](../../tests/cuda/generated/GeneratedSparseBurnFactory.h) | 11 | 保留：独立 oracle/生成工厂声明；不并入生产数学。 |
| S-088 | [tests/cuda/test_generated_nse_device.cu](../../tests/cuda/generated/test_generated_nse_device.cu) | 40 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-089 | [tests/cuda/test_generated_sparse_burn_factory.cu](../../tests/cuda/generated/test_generated_sparse_burn_factory.cu) | 14 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-090 | [tests/cuda/test_network_derivative.cu](../../tests/cuda/microphysics/network/test_network_derivative.cu) | 32 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-091 | [tests/fixtures/GeneratedNseReference.h](../../tests/fixtures/network/GeneratedNseReference.h) | 19 | 保留：独立 oracle/生成工厂声明；不并入生产数学。 |
| S-092 | [tests/host/SparseKLURegression.cpp](../../tests/host/numerics/SparseKLURegression.cpp) | 33 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-093 | [tests/host/test_compensated_sum.cpp](../../tests/host/numerics/test_compensated_sum.cpp) | 25 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-094 | [tests/host/test_generated_network_reference.cpp](../../tests/host/network/test_generated_network_reference.cpp) | 33 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-095 | [tests/host/test_generated_nse.cpp](../../tests/host/network/test_generated_nse.cpp) | 37 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-096 | [tests/host/test_physical_constants.cpp](../../tests/host/core/test_physical_constants.cpp) | 43 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-097 | [tests/host/test_sparse_residual.cpp](../../tests/host/numerics/test_sparse_residual.cpp) | 54 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-098 | [tests/tooling/test_cuda_code_images.py](../../tests/tooling/build_tools/test_cuda_code_images.py) | 55 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-099 | [tests/tooling/test_large_network_runtime_manifest.py](../../tests/tooling/network/test_large_network_runtime_manifest.py) | 32 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-100 | [tests/tooling/test_microphysics_progress.py](../../tests/tooling/microphysics/test_microphysics_progress.py) | 46 | 保留：独立测试入口或 Host/Device 共用测试体的绑定，短入口不代表无覆盖。 |
| S-101 | [validation/network/inputs/audit150.py](../../validation/network/inputs/audit150.py) | 22 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |
| S-102 | [validation/network/inputs/audit200.py](../../validation/network/inputs/audit200.py) | 21 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |
| S-103 | [validation/network/inputs/audit31.py](../../validation/network/inputs/audit31.py) | 18 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |
| S-104 | [validation/network/inputs/nse_alpha.py](../../validation/network/inputs/nse_alpha.py) | 12 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |
| S-105 | [validation/network/inputs/nse_light.py](../../validation/network/inputs/nse_light.py) | 18 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |
| S-106 | [validation/network/inputs/weak_urca.py](../../validation/network/inputs/weak_urca.py) | 18 | 保留：独立网络规模/物理配方；分文件支持可复现生成。 |

## 5. 死代码、休眠接口与误判排除

| 编号 | 证据 | 结论/允许动作 |
|---|---|---|
| DC-01 | `SparseBeNrBatch.cuh:12–16` 只有 3 个旧名称别名；tracked src/tests/tools/cmake 未找到 include 或名称消费者，文档仍介绍兼容别名。 | 可信的仓内休眠兼容入口候选；默认保留。先核对公开兼容承诺/生成与外部使用，再决定保留、弃用说明或等价迁移。不能把 BE-NR 求解功能一并删除。 |
| DC-02 | `aprox13/19/21/Net*.h` 定义 frozen-screening 手工 Jacobian 包装并 include `TimmesJacobian.inc`；生产 `TimmesNetworkSupport::eval_jacobian` 使用 Dual 与 `molar_rhs_frozen_screening`。 | 已确认不在现行生产 Jacobian 路径；处于转译数学豁免范围，保留并更新说明，禁止以死代码清理名义删除。 |
| DC-03 | `IGravityPolicy::update_field` 只有声明及 None/External 空覆写，tracked 源码未发现调用。 | 未完成的 patch field 生命周期，不是自引力实现；按 SG-03/04 接入 domain service 后审查接口，不直接删整个 gravity policy。 |
| DC-04 | 四个 `Net*.cpp` 各 6 行，但包含 NUM_SPECIES/ODE_NEQ 的 static_assert，且应用 CMake 递归收集它们。 | 有默认编译检查用途，不是空死文件；若整合检查 TU，需保留默认构建覆盖并量测编译资源。 |
| DC-05 | `ARCH_FORCE_INLINE`/`ARCH_FORCEINLINE` 在 src 内仅见定义；tests/cuda/test_cuda_compile_probe.cu 和 test_mainline_authority.cpp 实际消费。 | 非死宏；仅搜生产 src 会误判。 |
| DC-06 | `GravityNone`、AbsentBinding、generated/network 无能力路径与异常拒绝分支。 | 表示可选关闭或明确不支持，不是可删空实现；不能把错误路径改成静默 fallback。 |
| DC-07 | `helm_eos_loader.h` 是 include 聚合，但 CudaBackendInternal 和 EOS owner 测试仍消费。 | 有消费者；可改善命名/缩小 include，但不将其当无用文件。 |
| DC-08 | 当前 tracked src 未见 predictive_amr、GetSolverName、riemann_solver 的旧实现；旧记录仍有相关文字。 | 已退休功能的历史文字不构成生产死代码，保留原 evidence。Tabular native/normalized/free-energy、配置别名和旧生成包格式仍可能是受支持能力，不能顺手删除。 |

本轮没有删除任何候选。未来删除前必须同时检查直接/间接 include、模板/虚接口、注册宏、CMake 生成与显式实例化、工具脚本、测试与文档公开入口；无法排除外部契约时保留该入口。

## 6. 重复实现、重复声明与 include 复杂度

| 编号 | 位置/证据 | 处置边界 |
|---|---|---|
| DU-01 | Driver.h 两个 Burn 半步，1161/1309 行起，批执行与归约主体高度相同。 | 提取显式 half-id 的同一执行体；保留两种 reduction key、累计限制、ENUC 和错误标识。 |
| DU-02 | FluxHLL.h:90、FluxHLLC.h:186、FluxRoe.h:89、FluxSW.h:76、FluxVL.h:77 附近，39/30 条非空有效行级别的共同遍历窗口。 | 提取 Host face traversal；各 compute_face_flux、entropy/smoothing 参数和 near-vacuum 行为保持在各策略中。不能合并五种算法。 |
| DU-03 | FluxFunctions.h:98 与 :121 的 get_flux 重载重复物理通量算术。 | 可让 EOS 重载取得压力后委托压力重载；必须保留低密度时不调用 EOS 的短路与浮点次序。 |
| DU-04 | Tabular3DEOS.cpp:75/115/144 与 Tabular4DEOS.cpp:71/111/140 具有相同的读取/数组校验段。 | 抽取到已有 TabularLoaderUtils.h 或 source helper；维数、坐标和支持模式各自保留，不能通过删除格式消除重复。 |
| DU-05 | AmrTransferPlans.h:130 与 ExchangePlan.h:126 的 Fingerprint 字节累积器相同。 | 可提取独立 byte-hash primitive；输入字段顺序和现有 plan fingerprint 必须逐位保持。不能与文件 SHA256 混用。 |
| DU-06 | LimitedLinearProlongation.h:35 与 RegridTransferMath.h:71 都叫 minmod，但对 NaN 的分支条件不同。 | 暂不合并；证明调用域、NaN/Inf、符号零语义后才能共享 scalar leaf，两个 AMR transfer 流程仍分开。 |
| DU-07 | RK2/RK3 executor、Dispatch_RK2/RK3 type visitors 存在相似包装；BD/ROS4 integrate_report 有同形 continuation 适配。 | 仅提取机械适配；RK/ODE 表、错误控制、时间精度和独立编译 TU 保留，不将不同算法视为重复功能。 |
| DU-08 | 多处 SimConfig/SpeciesManager/AMRControl 前置声明；局部 Impl/Binding/Route 别名重名。 | 作用域明确的声明隔离是正常设计；当前词法检查未见同一文件重复直接 include，未取得 ODR 重复定义的构建证据。无必要创建一个更重的统一大头文件。 |

重复窗口筛查排除来源豁免公式；候选依据是去注释后连续有效行相同，不能代替 C++ 符号/ABI 分析。保护表达式次序与 inlining/noinline 边界，避免通过“去重”引入数值或 NVCC 资源退化。

| 头/入口 | 直接项目依赖 | 传递可达项目文件 | 建议 |
|---|---:|---:|---|
| [src/driver/dispatch/DispatchImpl.h](../../src/driver/dispatch/bindings/DispatchImpl.h) | 16 | 104 | 随 Driver 拆分隔离非模板组织逻辑，保留必要的策略实例化。 |
| [src/driver/Driver.h](../../src/driver/Driver.h) | 19 | 92 | Networks.h 等目录仅供真实 factory 消费；提取 .cpp，避免拆头后仍全部传递包含。 |
| [src/cuda/runtime/control/CudaBackendInternal.h](../../src/cuda/runtime/control/CudaBackendInternal.h) | 18 | 61 | 实际资源 owner 当前确需完整 EOS/variant；未来拆资源声明需处理 incomplete-type/析构，不能只删 include。 |
| [src/cuda/diffusion/DiffusionKernels.cuh](../../src/cuda/diffusion/DiffusionKernels.cuh) | 9 | 57 | 从 DiffusionAMRStages/DiffFlux 提取真正共享数学叶子，去掉间接 Host 调度依赖。 |
| [src/numerics/diffusion/DiffusionAMRStages.h](../../src/numerics/diffusion/DiffusionAMRStages.h) | 4 | 40 | 分开公共 recurrence 与 Host 调度。 |
| [src/interface/ProblemGenerator.h](../../src/interface/ProblemGenerator.h) | 6 | 34 | 接口只用引用和 EosId，适合前置声明；case .cpp 自己包含实际所用定义。 |
| [src/numerics/burnsolver/Networks.h](../../src/numerics/burnsolver/Networks.h) | 7 | 32 | 限制在 factory/binding 使用；不能传进通用 ODE 或普通运行声明。 |

这是解析字面量 project include 得到的词法闭包，包含条件编译各分支，不是预处理器实测的编译依赖或耗时。选定配置后还需用 compiler depfile/self-contained header 编译核实；不能据此声称缩短编译百分比。

## 7. 超过 8 个直接文件的目录

阈值触发审查，不是强制上限；明确归属优先于均分数量。目录移动必须同步 CMake、include、生成模板、测试发现/路径断言、文档与证据入口，不能长期铺满纯转发头。

| 编号 | 目录 | 直接文件数 | 判断 | 二级分类建议 |
|---|---|---:|---|---|
| F-01 | `.` | 10 | 保持 | 项目入口、许可证、README 和 Git 配置应可发现，不为数量限制移动。 |
| F-02 | `cmake` | 13 | 建议分类 | cuda/ 放后端/route/image 配置；templates/ 接收所有 .cu.in；保留模块入口并核对 CMAKE_CURRENT_LIST_DIR。 |
| F-03 | `src/amr` | 24 | 优先分类 | topology/（tree/block/identity/pool/Morton）、exchange/（boundary/ghost/plan）、regrid/（migration/transaction/math）、flux/（register/reflux）、indicators/；避免整体 include 环。 |
| F-04 | `src/core` | 10 | 保持为主 | 只有 9 个代码文件；低层公共叶子不需人为拆散。若以后扩容再分 startup/ 与 math/。 |
| F-05 | `src/cuda/hydro` | 14 | 建议分类 | flux/（face/reconstruct/policy）、stages/（batch/state/stage/source）、boundary/（boundary/exchange）；几何 adapter 放共同几何边界。 |
| F-06 | `src/cuda/microphysics` | 22 | 优先分类 | eos/owners/、burn/（sparse ODE/batch）、linalg/（cuDSS/equilibration）；后者可为 gravity 复用，避免把引力塞入燃烧目录。 |
| F-07 | `src/cuda/runtime/burn` | 13 | 建议分类 | 声明入口保留；dense/、sparse/、dispatch/、routes/ 按存储与执行职责组织。 |
| F-08 | `src/cuda/runtime/burn/routes` | 13 | 建议分类 | tabular3/、tabular4/ 分类组合绑定；Ideal/Helm 入口可留本层；不减少显式编译单元。 |
| F-09 | `src/driver` | 13 | 优先分类 | runtime/（backend/state/topology）、stages/（burn/hydro/diffusion/gravity）、schedule/（plans/control）、io/、既有 dispatch/；Driver.h 保持根入口。 |
| F-10 | `src/driver/dispatch` | 10 | 可渐进分类 | registry/（tags/注册/visitors）、backend/（probe/capability）、bindings/（Euler/RK）；先拆职责再决定迁移。 |
| F-11 | `src/numerics/burnsolver` | 11 | 建议分类 | ode/ 放 BE-NR/BD/ROS4 及 continuation；coupling/ 放热力学/网络导数；factory/ 放绑定目录与 dispatch。 |
| F-12 | `src/numerics/diffusion` | 9 | 暂缓分类 | 8 个代码文件加 README；先完成数学与 Host stage 分离，再考虑 math/、stages/，不为一个超额文件增层。 |
| F-13 | `src/physics/eos` | 23 | 建议分类 | tabular/（views/math）、sources/（loader/completion）、policies/（Host factory）；Helm 转译数学整体保留，移动路径也保持源追踪。 |
| F-14 | `src/physics/network/timmes_common` | 9 | 保持 | 8 个代码文件与 README 构成明确来源边界；转译公式不拆，不增加多层跳转。 |
| F-15 | `tests/cuda` | 44 | 优先分类 | hydro/、amr/、microphysics/、runtime/，随大测试责任拆分；原 CTest 标识与资源属性需要映射。 |
| F-16 | `tests/fixtures` | 12 | 建议分类 | amr/、eos/、burn/、nse/；独立 reference 不能变成被测实现的包装。 |
| F-17 | `tests/host` | 33 | 优先分类 | amr/、driver/、eos/、burn/、io/、core/；按责任组织而非按行数均分。 |
| F-18 | `tests/math` | 10 | 保持为主 | 当前共享案例库边界明确；可在随 EOS/burn/amr 测试增长时分类，不强制移动 9 个案例文件。 |
| F-19 | `tests/tooling` | 21 | 建议分类 | architecture/、build/、network/、validation/、resources/；必须保持 unittest discover 可递归发现与测试数量。 |
| F-20 | `tools` | 13 | 优先分类 | architecture/、validation/、resources/；命令行入口薄而稳定，不增加重复 CLI；复用已存在 network/。 |
| F-21 | `validation/hydro/inputs` | 13 | 保持 | PCM/MUSCL/PPM 分辨率扫描是一套小型输入矩阵，优先 README 索引；不得破坏冻结 evidence 的路径。 |
| F-22 | `validation/network` | 15 | 建议分类 | probes/、runners/、references/；recipe/test/manifest 调用路径同步更新，results 原封不动。 |

新增本计划与清单后，`docs/development/` 也会超过 8 个直接文件。建议后续将进行中的功能计划归入 `plans/`、保留 README 和长期维护规范在根层；必须更新索引，历史 archive 原样保持。本轮仍保留已有计划路径，避免打断已交付链接。

## 8. 非代码大文件与冻结历史

| 持续维护非代码文件 | 行数 | 处理 |
|---|---:|---|
| `EOS_toolkit/tables/baryon/eos2.tab` | 656929 | 科学输入数据，保留整体格式与身份，禁止按行数拆表。 |
| `EOS_toolkit/tables/baryon/eos4.tab` | 656929 | 科学输入数据，保留整体格式与身份，禁止按行数拆表。 |
| `EOS_toolkit/tables/helmholtz/helm_table.dat` | 434964 | 科学输入数据，保留整体格式与身份，禁止按行数拆表。 |
| `docs/Reference.md` | 1214 | 可按配置/运行/物理/IO 等主题拆页面；中英文同步，保留旧章节锚点入口；不属于源码死代码。 |
| `docs/Reference.zh-CN.md` | 907 | 可按配置/运行/物理/IO 等主题拆页面；中英文同步，保留旧章节锚点入口；不属于源码死代码。 |
| `tests/fixtures/validation_provenance/backend-validation-evidence.json` | 8427 | 固定验证 fixture，保留来源/格式；不按长度整理。 |
| `validation/amr/gpu_curvilinear_cases.json` | 1098 | 机器可读案例矩阵，维持消费 schema；未来仅在 loader 支持显式分片时组织。 |

历史代码中命中长度阈值的全部 13 项如下，统一保留；其中失败实验是用于复现反例的 evidence，不是现行生产实现。

| 历史代码文件 | 行数 |
|---|---:|
| `validation/amr/results/sustained-first-law-20260907/diagnosis-746/inspect_cross_restore.py` | 46 |
| `validation/amr/results/sustained-first-law-20260907/release-740/inspect_chain.py` | 49 |
| `validation/backend/results/final-acceptance-20260907/qualify_runtime.py` | 46 |
| `validation/backend/results/final-acceptance-20260907/release-73a9cf50/index-preflight-927/review_index.py` | 1083 |
| `validation/backend/results/final-acceptance-20260907/review_index.py` | 1095 |
| `validation/hydro/results/sedov-first-law-20260907/FluxFunctions-energy-secant-negative.h` | 663 |
| `validation/hydro/results/sedov-first-law-20260907/FluxFunctions-negative-814.h` | 632 |
| `validation/hydro/results/sedov-first-law-20260907/energy_secant_probe.cpp` | 46 |
| `validation/hydro/results/sedov-first-law-20260907/face_replay.cpp` | 58 |
| `validation/hydro/results/sedov-first-law-20260907/flux_identity.cpp` | 10 |
| `validation/hydro/results/sedov-first-law-20260907/hydro_leaf_before_roe.cu` | 1731 |
| `validation/hydro/results/sedov-first-law-20260907/isolate_symmetry.py` | 52 |
| `validation/hydro/results/sedov-first-law-20260907/roe_thermodynamic_probe.cpp` | 14 |

以下 14 个历史目录直接文件数大于 8，均维持原路径，使用索引导航；不得重排原始 evidence 或改写冻结引用。

| 历史目录 | 直接文件数 |
|---|---:|
| `docs/development/archive` | 9 |
| `validation/amr/results/curved-native-20260907/memcheck-904/spherical_coupled_amr_rkl2_wedge_3d/step-2/cuda` | 9 |
| `validation/amr/results/curved-native-20260907/memcheck-904/spherical_coupled_amr_rkl2_wedge_3d/step-5/cuda` | 9 |
| `validation/amr/results/curved-native-20260907/racecheck-906/spherical_coupled_amr_rkl2_wedge_3d/step-2/cuda` | 9 |
| `validation/amr/results/curved-native-20260907/racecheck-906/spherical_coupled_amr_rkl2_wedge_3d/step-5/cuda` | 9 |
| `validation/amr/results/local-fixes-20260905` | 17 |
| `validation/backend/results/final-first-law-20260907` | 11 |
| `validation/backend/results/maintenance-freeze-20260908` | 18 |
| `validation/eos/results/tabular-extension-20260908` | 24 |
| `validation/eos/results/tabular-extension-20260908/diagnostics/first-static/raw-1-eos2-static` | 12 |
| `validation/eos/results/tabular-extension-20260908/diagnostics/first-static/raw-2-eos4-static` | 12 |
| `validation/eos/results/tabular-extension-20260908/static/raw-1-eos2-static` | 12 |
| `validation/eos/results/tabular-extension-20260908/static/raw-2-eos4-static` | 12 |
| `validation/hydro/results/sedov-first-law-20260907` | 12 |

## 9. 本轮验证与实施限制

- 已拉取 main 并在同一工作目录建立审查分支。原 main 未 reset/rebase；已有 README 改动通过 stash 恢复，备份保留。没有源码实现改动、删除功能、提交或推送。
- 现有 `python3 tools/audit_architecture.py .` 在工作区初跑报 3 次 “production CUDA source glob is forbidden”。定位到 `build-cpu/_deps/suitesparse-src/` 内 GraphBLAS、SPQR/GPUQREngine、CHOLMOD/GPU 的 CMakeLists；审计只排除名为 build 的目录，漏掉 build-cpu。
- 将 main 导出为不含本地构建/Studio 的干净快照后，原样运行同一审计命令，退出码 0；tracked 范围的独立复核也未发现本地 include 环、无效项目引用或现有归属规则违例。初跑错误不能解释为 main 的 CUDA 模块重复实现。
- 后续应改进审计的 scope 与错误定位：排除构建/下载产物，同时继续覆盖真实生产生成源和已配置的外部网络，不能简单忽略所有未跟踪源码。
- `test_shared_stage_scheduler.cpp:1425` 等测试按 Driver.h 文字/位置验证生产接线，`audit_architecture.py` 也包含路径权威表。重构需将检查迁移到真实新 owner，并增加/保留行为检查，不能删除这些断言来取得通过。
- 本次未编译/执行 CPU/CUDA 数值测试，没有声称死代码已穷尽、功能等价已验收或性能已提高。文档链接、清单覆盖和编号另做校验。

复现干净审查的方式：对上述固定提交使用 `git archive` 导出临时目录，在该目录运行原样的 `tools/audit_architecture.py`。行数清点以 main 跟踪文件为输入，按本清单第 1 节扩展名与证据路径分类。物理行数包括已检出的 EOS 文本表，不对科学数据做重新编码。

## 10. GUI 分支接续后的适用性补充

2026-09-21 核对 `origin/codex/studio-core-ui-contracts` 的 `d98f6f6e853ccb23eaa916ab1d20356019087622`。
该分支基于本清单的 main，新增 6 个提交，涉及 150 个文件；其跟踪树不含 Studio 前端。
本清单的行数、include 闭包与阈值统计仍指原 main，不能当作整合后的统计，也不能用于证明新 API 没有消费者。
详细重叠矩阵、接口约束和执行顺序集中在[主计划第 10 节](SelfGravityImplementationPlan.zh-CN.md#10-guicuda-协作执行顺序与证据)，以下只修正维护判断。

| 新证据/涉及清单 | 维护判断 |
|---|---|
| `driver/InitialMesh.h` 11 行，正式启动与 API 共用 `InitializeRootState` | 有明确的共享初始化边界，保留；不能仅为减少小文件数并进庞大的 Driver。 |
| `amr/RefinementThermodynamics.h` 29 行、`core/InitialStateConversion.h` 38 行 | 分别集中 EOS 细化量和初始守恒量转换；保留单一数值实现及多个消费者，不回填重复体。 |
| `api/InitialMesh.h` 与 `driver/InitialMesh.h` 同名 | 前者负责有界预览和响应，后者负责公共根初始化；同名不等于重复实现，不将 GUI 协议放进 Driver。必要时改名改善可发现性，需同步调用方。 |
| S-058 / ProblemGenerator include 收窄 | 新版新增 inline 检查体、采样和观察接口；重新判断完整类型需求，不能机械应用旧 main 的全前置声明建议。 |
| F-03/F-09 与 DriverUtils/AMR 提取 | API 已直接使用 BCHandler、tree/pool/exchange/regrid。先纳入公共接口，再选择迁移；一起修改 Core API 并保持正式 t=0 对照。 |
| F-13 / EOS owner-view、DU-04 / loader | 新 session 持有 EOS 资源与独立组分，涉及内容校验和失败清理；后续提取同时验证生命周期，不能只依据相似代码合并。 |
| F-04 / core 与新增 api 密集目录 | 新增文件使旧目录计数失效。先按配置、初始化、资源、协议等稳定职责评估；不因为超过 8 个文件立即新建层次或合并小声明头。 |
| simulation 源码/include 调整 | 新单位证据绑定编译 `.cpp` 的 SHA-256；修改后需重新审核并登记证据，不能把 GUI 丢失单位视为无关故障。 |

前述 L/R/F 候选的实施以维护收益及 GUI/CUDA 实际接口为准，不以文件规模排名排期。
本次补充只修改计划与清单，未合并分支、改动实现或重跑分支所记录的 CPU/CUDA 检查。

## 11. P1 实施增量（2026-09-21）

本节覆盖上文“尚未实施”的历史状态；不重新解释 main 的冻结计数。
`physics/selfgravity` 从 GUI Core `d98f6f6e` 建立，P1 源码提交为 `95b858fc`。

| 涉及项 | 实际处理与维护收益 |
|---|---|
| Driver 大文件与 R1 | 主循环集中于 174 行 `Driver.h`；Runtime 唯一持有运行资源，Boundary/Regrid 是它的实现文件，IO 独立承担输出适配。避免调用者在长模板里寻找事务与可见性边界。 |
| DU-01 Burn 两半步 | `DriverStages.h::execute_burn_half` 共用批执行/错误检查/归约体，First/Second 保留独立逻辑身份；原 cell 数学和 Host traversal 保留。 |
| 阶段绑定与小文件 | Hydro、Diffusion、Burn 的 EOS 模板绑定及复用 scratch 共同放在 476 行 `DriverStages.h`。没有按计划候选名称机械创建三个阶段头或 Boundary/Regrid 透传头。 |
| 小声明文件 | `ScalarFieldView.h` 和 `DriverIO.h` 均 37 行，分别承担独立的非 owning 字段视图与输出接口，保留。GUI 的 InitialMesh/RefinementThermodynamics 共享入口保留。 |
| 目录与来源数学 | 本轮没有全仓目录迁移或 Fortran 数学改写；F/S/DC 的其他候选继续待评估，不因 P1 开始而自动获准删除。 |
| 测试/审计跟随 owner | 调度测试沿新 Runtime/Boundary/Regrid/Stages 检查实际接线，并保留行为测试。审计器修正两项继承 GUI 后过期的规则，保留对真实隐藏 CPU fallback 的拒绝。 |
| 审计与验证后续整理 | `74a755e5` 在遍历前排除实际 CMake 构建树/辅助 checkout，保留未跟踪生产代码与源模块检查；Host 检查点比较器解除 CUDA 链接依赖，比较实现不复制、不放宽。 |

[交接记录](SelfGravityP1Handoff.zh-CN.md)及其关联证据记录构建、5 项核心测试、102 项审计测试、
10 个 CPU 对照案例和 4 组重启，这是第一包的冻结证据。后续按授权补齐的
[P1 GPU 与 CI 记录](../../validation/gravity/results/selfgravity-p1-gpu-20260921/README.md)
单独登记 Release CUDA 构建和实际检查范围；仍未进入 P2。
后续完整工具测试 355/355、零跳过，工作区直接审计通过；不再需要导出源码快照来避开构建产物。
该范围修正不代表其余大型文件拆分、目录迁移或死代码删除候选已经实施。

## 12. P1 目录与引用增量（2026-09-22）

GPU 验证封包 `c95f606f` 后按用户追加授权实施 F-02/03/05–11/13/15–19 及 GUI 新增 API/core
的职责分类，294 项路径见 [目录记录](layout/README.zh-CN.md)。F-12/14 保持内聚入口；
F-20/22 的公开 CLI/import 路径继续兼容，F-21、历史记录和既有文档入口不强制迁移。

内部 include 全面统一为根相对路径，测试亦同；公开算例新增两个稳定转发头，短小是其契约职责，
不是重复声明。来源数学主体不变。CMake、生成模板、审计 owner、路径断言与测试发现同步更新。
最终全部配置 CPU 目标编译成功，工具检查 355/355；没有减少验收断言或重写历史 GPU 证据。
所有当前状态以主计划 0.6 和目录记录为准，上文 main 计数继续保持历史快照。
