# physics/selfgravity：P0/P1 交接

日期：2026-09-21。实施参考：[主计划 v0.4](SelfGravityImplementationPlan.zh-CN.md)。
源码提交：`95b858fc9f97147a8bced5146dd229dcc3d08df4`。
本轮完成 P1 的 CPU 门槛并封包；CUDA 验证按用户指令统一后置，P2 尚未开始。

## 1. 基准与 GUI 整合

- main：`01cc4f723e674d47fe23850e7c0fef221e92e98c`。
- GUI Core：`d98f6f6e853ccb23eaa916ab1d20356019087622`，在上述 main 之后有 6 个线性提交。
- 工作目录 `/home/shiroakane/ARCH`，工作分支 `physics/selfgravity` 直接从 GUI Core 基准创建，因此完整继承其历史。
- 继承提交为 `40b7704d`、`47517d1c`、`91a46f8f`、`5e96d4f0`、`97a2b50c`、`d98f6f6e`，涵盖 CPU 预览、参数绑定、配置契约、初始 AMR、注册案例与资源复用 session。
- GUI 分支的跟踪树不含 Studio 前端；本地未跟踪 `studio/` 是独立工作，未纳入本次提交。

完整继承比选择性摘取更合适：这些 Core API 共同消费新的初始化、EOS、参数与资源接口，
后续提交依赖先前提交。P1 沿用 `driver/InitialMesh.h`、`amr/RefinementThermodynamics.h`
和 `core/InitialStateConversion.h` 的共同路径，没有再造旧 main 的初始化实现。

GUI 已记录的 18/18 CPU 检查沿用
[PREVIEW_SESSION_HANDOFF](../../src/api/PREVIEW_SESSION_HANDOFF.md)，本轮没有重跑。
该记录只适用于其冻结源码与范围；本轮另行验证 P1 实际影响的核心路径。
GUI 基准内的严格配置解析、CGS 默认值等既有行为来自上述提交，不是 P1 重构引入。

## 2. 已落地的职责

| 文件 | 唯一职责 |
|---|---|
| `src/driver/Driver.h` | 启动、宏步主循环、Strang 顺序、regrid/输出时机；由 GUI 基准的长模板收拢至 174 行，行数只是结果。 |
| `DriverRuntime.h/.cpp` | 运行资源、topology registry、ledger、clock、backend、block handles、storage generations 的唯一 owner。 |
| `DriverBoundary.cpp` | backend-local ghosts 与显式 Host materialization；Host 非 Current 边界仍由对应积分器负责。 |
| `DriverRegrid.cpp` | prepare/migrate/publish/rollback/retire 协调；树、迁移数学与执行器保持原有归属。 |
| `DriverIO.h/.cpp` | 输出可见性、检查点相位与日志适配；复用既有 IO writer。 |
| `DriverStages.h` | 共用 EOS 模板阶段绑定和 scratch，统一 Burn 半步执行体，保留两半步独立归约身份。 |
| `StageScheduler.h` | 增加全域 Hydro 准备契约、阶段输入时间；原有权重、slot、ghost、reflux、发布与轮转规则保留。 |
| `src/grid/ScalarFieldView.h` | 独立于 fluid/species packing 的借用标量视图；布局、Host/Device、storage generation，不拥有或搬运内存。 |
| `src/physics/gravity/GravitySolveTypes.h` | 每块密度输入身份、全域请求元数据、场发布/失效身份，不含求解算法。 |

Boundary/Regrid 共享 Runtime 的声明，避免空转发头；阶段绑定共用依赖和生命周期，暂不分散成多个小头。
非模板 Driver 实现加入 `arch_solver_dispatch`，其消费者可以取得完整依赖，CPU-only 构建不需 CUDA SDK。
Fortran 来源数学、EOS/反应网络数学、API 协议、检查点格式和已有物理选择未因 P1 改写。

## 3. 冻结的准备与字段契约

顺序是：**验证所有输入 → 一次全域 prepare → 验证完成 → 发放阶段发布凭证 → 原执行器 → 原发布/边界/轮转流程**。
准备失败或仍 pending 时，不执行 patch/batch，也不发放该 Hydro 阶段的输出凭证。
请求借用完整 block handle span 和 ledger，不允许保留跨阶段的临时引用。

| 方法 | 输入 slot | 物理采样时间 |
|---|---|---|
| Euler | Current | `t` |
| RK2 | Current、Scratch | `t`、`t+dt` |
| SSPRK3 | Current、Scratch、Next | `t`、`t+dt`、`t+dt/2` |

字段身份包含全部块的 slot/state version/storage generation、topology epoch、阶段时间、G、
算子/BC/精度配置版本，以及输出 storage generation 与完成凭证。不能用首块版本代表全域。
失败发布保留旧元数据，但旧场只有在完整输入身份仍匹配时才可消费。

当前 none/external 路径不绑定准备服务；`gravity_type=self` 仍拒绝。
`GravitySolveRequest` 此时只建立密度与身份边界，不包含实际几何/BC/算子实现。
MeshHierarchy、Poisson、MG、phi/g 存储和消费、真实 regrid/restart 失效接线在 P2–P4 随首个实现补齐，
不能将单元测试里的失效元数据当作生产引力闭环。

## 4. 本轮验证与边界

完整证据和复现方式见 [P1 CPU 验证记录](../../validation/gravity/results/selfgravity-p1-20260921/README.md)。

| 检查 | 结果 |
|---|---|
| GCC 13 / Debug / OpenMP ON / CUDA OFF / KLU OFF 主程序与受影响目标 | 构建通过；末次增量构建约 46.9 秒、受监控进程峰值 RSS 约 1.88 GiB，无 swap 增长。不是完整构建或性能基准。 |
| 核心 CTest | 5/5：state residency、shared stage scheduler、新 gravity stage contract、topology transaction、compute backend。 |
| 架构审计 | 完整候选源码的干净快照通过；工具测试 102/102。 |
| CPU 数值对照 | 10 个案例，各比较 step 0/2/4/5，共 40 份检查点。none/external、Euler/RK2/RK3、混合 AMR、RKL1/RKL2、Helm+aprox13 Burn/ENUC 覆盖；场量绝对/相对误差均为 0。 |
| 重启 | 扩散/燃烧各覆盖中途 `resume_after_regrid=true` 和终态 `false`，共 4 组；新程序读取旧基准检查点，与旧程序恢复结果及新程序连续运行均通过比较。 |
| GUI 既有测试 | 沿用 GUI 分支记录，未重跑。 |
| CUDA / KLU / 性能 | 未执行 CUDA 编译、运行、sanitizer、加速比或 KLU provider 验证；按本轮授权后置/不在本阶段。 |

现有 checkpoint comparator 的目标名含 CUDA，但本轮只复用既有二进制进行 Host HDF5 比较/元数据读取，
没有构建或运行 CUDA 求解器。时间/控制器/输出序号比较沿用原比较器规则；终态重启采用其显式输出序号修正规则，
没有为通过回归放宽场量、ENUC 或 burn limiter 容差。

审计修正了两处 GUI 整合后的陈旧判断：严格布尔异常的源码锚点，以及 schema 导出 `definition.fallback`
被误认为隐藏 CPU fallback。新增正反例，实际 `cpu_fallback()` 仍拒绝。
原工具直接扫工作目录会纳入 `build-cpu` 和其他 worktree，范围修正留待独立维护；本轮用全部候选源码的干净快照复验。

## 5. 下一步边界

下一包从 P2 的单层 CPU Poisson/MG 开始，先冻结 G/单位、离散算子、BC/零空间、残差范数与预算，
再建立有实际消费者的 hierarchy/operator/solve report。KLU/cuDSS 继续作为按需粗层/参考 provider，
不复用 burn 的 `N*N` slots 作全域矩阵；FFT/Green FFT 不进入本轮。

P1 为 CUDA 保留共享控制流、设备视图和完成语义，不能据此声称 CUDA 编译正确或已加速。
后续统一检查新增 `.cpp` 的 CUDA 构建接线、阶段绑定、regrid 事务、Host/Device 传输和资源退休，
再以同误差预算测端到端加速。正常 device 边界准备不应新增 D2H。

GUI 后续同步以本分支完整历史为准；本轮不改 API 响应、schema 或 session 资源契约。
未来 self 支持矩阵、phi/g 输出和可视化字段必须在物理验收后同步给 Core API，不能提前宣告可用。
