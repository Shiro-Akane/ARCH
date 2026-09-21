# physics/selfgravity：P0/P1 交接

日期：2026-09-21 CPU 封包；2026-09-22 补充 GPU 验证。实施参考：[主计划 v0.5](SelfGravityImplementationPlan.zh-CN.md)。
源码提交：`95b858fc9f97147a8bced5146dd229dcc3d08df4`。
CI/验证流程提交：`74a755e596a29dce7f13ac7864ee3692553d7166`。
CPU 门槛已完成并封包；按后续授权补齐的 P1 CUDA 门槛也已通过，P2 尚未开始。

## 1. 基准与 GUI 整合

- main：`01cc4f723e674d47fe23850e7c0fef221e92e98c`。
- GUI Core：`d98f6f6e853ccb23eaa916ab1d20356019087622`，在上述 main 之后有 6 个线性提交。
- 工作目录 `/home/shiroakane/ARCH`，工作分支 `physics/selfgravity` 直接从 GUI Core 基准创建，因此完整继承其历史。
- 继承提交为 `40b7704d`、`47517d1c`、`91a46f8f`、`5e96d4f0`、`97a2b50c`、`d98f6f6e`，涵盖 CPU 预览、参数绑定、配置契约、初始 AMR、注册案例与资源复用 session。
- GUI 分支的跟踪树不含 Studio 前端；本地未跟踪 `studio/` 是独立工作，未纳入本次提交。

完整继承比选择性摘取更合适：这些 Core API 共同消费新的初始化、EOS、参数与资源接口，
后续提交依赖先前提交。P1 沿用 `driver/initialization/InitialMesh.h`、`amr/refinement/RefinementThermodynamics.h`
和 `core/problem/InitialStateConversion.h` 的共同路径，没有再造旧 main 的初始化实现。

GUI 已记录的 18/18 CPU 检查沿用
[PREVIEW_SESSION_HANDOFF](../../src/api/PREVIEW_SESSION_HANDOFF.md)，本地没有专门重跑。
该记录只适用于其冻结源码与范围；本轮另行验证 P1 实际影响的核心路径。
后续自动 CPU CI 仍运行完整已配置 inventory，包含其适用的 Core/API 检查。
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

### 4.1 已封包的 CPU 对照

下表保留前一包的范围；完整证据和复现方式见 [P1 CPU 验证记录](../../validation/gravity/results/selfgravity-p1-20260921/README.md)。

| 检查 | 结果 |
|---|---|
| GCC 13 / Debug / OpenMP ON / CUDA OFF / KLU OFF 主程序与受影响目标 | 构建通过；末次增量构建约 46.9 秒、受监控进程峰值 RSS 约 1.88 GiB，无 swap 增长。不是完整构建或性能基准。 |
| 核心 CTest | 5/5：state residency、shared stage scheduler、新 gravity stage contract、topology transaction、compute backend。 |
| 架构审计 | 完整候选源码的干净快照通过；工具测试 102/102。 |
| CPU 数值对照 | 10 个案例，各比较 step 0/2/4/5，共 40 份检查点。none/external、Euler/RK2/RK3、混合 AMR、RKL1/RKL2、Helm+aprox13 Burn/ENUC 覆盖；场量绝对/相对误差均为 0。 |
| 重启 | 扩散/燃烧各覆盖中途 `resume_after_regrid=true` 和终态 `false`，共 4 组；新程序读取旧基准检查点，与旧程序恢复结果及新程序连续运行均通过比较。 |
| GUI 既有测试 | 沿用 GUI 分支记录，未重跑。 |
| CUDA / KLU / 性能 | 此 CPU 封包时未执行 CUDA 编译、运行、sanitizer、加速比或 KLU provider 验证；后续 GPU/CI 补充见第 4.2 节。 |

现有 checkpoint comparator 的目标名含 CUDA，但本轮只复用既有二进制进行 Host HDF5 比较/元数据读取，
没有构建或运行 CUDA 求解器。时间/控制器/输出序号比较沿用原比较器规则；终态重启采用其显式输出序号修正规则，
没有为通过回归放宽场量、ENUC 或 burn limiter 容差。

审计修正了两处 GUI 整合后的陈旧判断：严格布尔异常的源码锚点，以及 schema 导出 `definition.fallback`
被误认为隐藏 CPU fallback。新增正反例，实际 `cpu_fallback()` 仍拒绝。
当时原工具直接扫工作目录会纳入 `build-cpu` 和其他 worktree，因此用全部候选源码的干净快照复验。
后续 `74a755e5` 独立修正遍历范围，保留未跟踪生产代码与用户模块检查，新增正反例；日常不再需要导出快照。

### 4.2 GPU 与流程补充

构建、实际命令、覆盖范围及 CI 结果见
[P1 GPU 验证记录](../../validation/gravity/results/selfgravity-p1-gpu-20260921/README.md)。
CUDA 使用 Release/GCC 12，与 CPU 历史基线配置分开记录；不声称已经做过拆分前后 GPU 逐位比较。
P1 GPU 已通过：22/22 CTest 无跳过；6 个常规案例、42 次 CPU/CUDA 执行、21 次比较，
最大后端场绝对差 `1.4433e-15`；6 个固定终点解析/守恒复核通过。
active-ENUC+全输运覆盖 12 条路线、9 次同/跨后端重启比较及两种检查点相位；
3 个动态/曲线 AMR smoke 和 4 次 memcheck/racecheck 通过。完整预算和适用范围见关联记录。

全部工具测试 355/355、零跳过；Host-only 新构建比较器复核原 40 对 CPU 检查点通过，
错步反例仍拒绝。远端 `74a755e5` 的完整 Tooling + CPU Release CI 已通过。
CI 改动限定为分支接入、按需 LFS 下载、Host 比较器解耦及完整报告核对，未改变原数值预算。

### 4.3 GPU 封包后的目录与 include 整理

在 `c95f606f` 完成 GPU 证据封包后，按后续授权完成 294 项职责分类迁移；内部引用统一以
源码根目录为准，测试使用测试根目录，用户算例采用 `<UserInterface.h>` / `<GlobalDefs.h>`。
[目录交接与映射](layout/README.zh-CN.md)列明稳定入口、GUI 接续、构建和不迁移的理由。

全部配置 CPU 目标编译通过；355 项工具检查无跳过；CPU/CUDA CTest 清单分别保持 53/128 项。
本轮未重跑 GUI 专项和数值验证，也未重新编译 CUDA；旧 GPU 证据仍绑定整理前源码。
CGS 已由 GUI Core `97a2b50c` 交付，直接复用；修正了仍称 IdealGas 可选任意单位的活动文档。
11 个算例源码摘要在逐一确认实现不变后更新，单位映射不变，记录见目录交接。

## 5. 下一步边界

本轮停在 P1。后续获准推进 P2 时，从单层 CPU Poisson/MG 开始，先冻结 G/单位、离散算子、BC/零空间、残差范数与预算，
再建立有实际消费者的 hierarchy/operator/solve report。KLU/cuDSS 继续作为按需粗层/参考 provider，
不复用 burn 的 `N*N` slots 作全域矩阵；FFT/Green FFT 不进入本轮。

P1 的 CUDA 结论必须以第 4.2 节实际记录为准；接口声明本身不构成运行证据。
P6 新引力求解器仍须重新验证物理误差、传输、资源与性能，使用同误差预算测端到端加速。
正常 device 边界准备不应新增 D2H。

GUI 后续同步以本分支完整历史为准；本轮不改 API 响应、schema 或 session 资源契约。
未来 self 支持矩阵、phi/g 输出和可视化字段必须在物理验收后同步给 Core API，不能提前宣告可用。
