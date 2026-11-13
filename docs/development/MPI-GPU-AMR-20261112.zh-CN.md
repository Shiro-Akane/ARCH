# GPU MPI 与 AMR 应用接入（2026-11-12）

## 起点与范围

本阶段从已验收的 CPU Host MPI 提交 4aae33e4 开始，包含朋友 main 的
2e05e66e。双方仓库使用同名分支 MPI/gpu-amr-integration；上一阶段分支保留。
自有 main 已包含 CPU 阶段，后续 GPU 增量先在本分支验证。

用户已授权继续 GPU MPI 与完整 AMR 集成。这里的“完整”是后续验收目标，
不是当前能力声明。CPU 双 rank、二维 Cartesian、level 0/1/2 的迁移与重网格
结果见 [前阶段记录](MPI-NextSteps-20261101.zh-CN.md)；不能据此开放任意
MPI-AMR 或 GPU 应用。

## 实施次序与验收

| 步骤 | 落点与具体工作 | 开放条件 |
| --- | --- | --- |
| G1：GPU Host staging | 复用 ExchangePlan 和现有 CUDA gather/scatter；仅传交换切片；先验证 MPI 与设备完成顺序，再接入现有 GhostExchange / CudaBackend / residency | 字段、species、全部槽和角点与独立 oracle 对照；不分配远端设备块；失败不能发布有效 ghost；实际流量及峰值内存可核对 |
| G2：跨 rank mixed-level GPU | 复用 coarse/fine cell plan、通量注册与 reflux 配方；设备准备紧凑包，Host MPI 传输，原设备执行器消费 | 最小五叶跨 rank 案例，Euler/RK2/RK3 宏步，原守恒/字段门槛，单 rank 失败共同恢复 |
| A1：AMR 生命周期 | 将已验收的 Host 所有权事务与 CUDA StoreTransaction 对齐；明确 Current/Next/Scratch、接收预留、临时峰值、旧 view 和退休事件 | 一级/二级迁移、细化/粗化与连续演化；失败保留旧字段/拓扑/分配；旧设备资源等待所有消费者完成 |
| A2：应用调度与容量 | 在现有 DriverRegrid 内组织同胞、传播标记和选择可行 owner；有真实需要时直接调整原池容量与 handle 失效规则 | rank 数/层级/空分区逐项扩展；预算不足共同拒绝或回滚；查询和运行共用 ExecutionPreflight |
| A3：输出、重启与应用开放 | 复用科学序列化，增加 shard 集合、manifest 发布与重启完整性；分别登记已支持的物理配置 | 缺 shard、身份不一致、中断发布、重启字段/守恒及资源边界通过后，才开放相应应用组合 |

不另建 MPI 分配器、时间推进系统或影子 ownership。进程与 communicator
契约可以在原设施内适度集中。Host staging 先实现；device-aware MPI、跨节点、
多 GPU 与长程物理演化需要独立证据。数学公式、物理定义和数值门槛保持原样。

## G1 首个增量：同层字段传输见证

[现有 ExchangeKernels](../../src/cuda/hydro/boundary/ExchangeKernels.cuh)
增加可独立调用的 gather/scatter；原本地 phase 调用同一对函数，仍先快照再写回。
CUDA scratch 统一为现有 GhostExchange 的 cell-major 顺序：每个单元的
rho、三个 momentum、energy、enuc、species；双向对照之外另逐字核验发送包
是否符合原 Host MPI 顺序。该检查在原 field-major 排列上确实失败，修正后重跑，
原失败日志保留。只调整字段搬运顺序，不改变物理数值运算。

启动边界检查负操作数、空/不匹配 payload、字段数、乘法和 CUDA grid 溢出。
计划 lowering、地址/容量验证与生命周期仍属于调用方，低层 launcher 不伪装成
完整分布式执行器。

[真实双进程测试](../../tests/cuda/amr/test_cuda_mpi_staging.cu)
只为本 rank 分配设备块。跨 rank 路径为：

1. 同一非默认流上的 gather 与 D2H。
2. 完成事件确认 D2H 后，Host 缓冲进入 MPI。
3. MPI 完成后才提交 H2D 与 scatter。
4. scatter 完成后才使用下一轴 donor、复用缓冲或读回验收。

本 rank 的交换调用原 combined launcher，不经 MPI。三维周期拓扑按 X/Y/Z
顺序包含前一轴写入的角点 donor。独立 Host oracle 使用每一阶段的完整快照，
逐位核验所有字段、ghost 和未参与交换的槽，包括负零及 NaN payload。
维度 1/2/3、species 0/4/37、同层 level 0/2 共 18 个布局；每个布局三个槽、
三轮重复，共 162 次槽交换、每 rank 324 次 phase 对照。
level 2 在这里仍是同层布局，不是 mixed-level 验收。

[Host 测试夹具](../../tests/cuda/amr/CudaMpiStagingPlan.cpp) 使用普通 C++
编译器调用原计划构建器；避免 NVCC 12.8 对 defaulted C++20 comparison 的
链接限制。未改生产拓扑类型或数学实现。

首个提交 2bc272fd 没有把 MPI 插入生产 CudaBackend 的 ghost/residency 发布链，
没有执行跨 rank GPU Hydro 宏步。当时 G1 的 Runtime 接入及 collective 失败恢复
尚待完成；下节记录后续 Runtime 增量。测试夹具中的 Host 全域 oracle 只用于验收，不是生产 staging 方案。
应用 GPU MPI / MPI-AMR 预检继续关闭。

## 首个增量验收

服务器 H100-20C 上的 Release 与 Debug 双进程 CTest 均通过，无跳过；
每个构建覆盖上述 18 布局、162 次槽交换、每 rank 324 次 phase 检查。
每 rank 每个构建的通信字段 staging 为 7,408,512 字节，不包含测试 oracle
读回和元数据；不是性能结论。两个 rank 共享一张 GPU，不能推导多 GPU 或多节点。

受影响生产 CudaBackendExchange.cu 已分别按两种配置的真实编译命令编译。
629 项 tooling 全部通过且无跳过；CUDA OFF 与 MPI OFF 的条件注册均核对。
尚未重建整个 CUDA 应用，也没有 compute-sanitizer 结果。
源码、二进制、JUnit 与范围见
[结构化摘要](../../validation/backend/results/mpi-gpu-staging-20261112/summary.json)。
初始工具环境错误和报文检查失败的日志均保留，未删除失败项或降低门槛。

## G1 第二个增量：Runtime 同层边界接入

本增量复用原 GhostExchange 的 owner 目录、X/Y/Z 配方、cell-major 报文及
MPI_Alltoallv。新增的同步 storage 回调只负责 begin、gather、scatter，不持有
拓扑、字段或 communicator。原 Host 路径仍执行原字段循环；CUDA 后端在原
exchange_scratch 中准备切片，并在每次 D2H / H2D + scatter 后等待原流完成。
没有新增 MPI 内存池或设备 ownership 管理器。

DriverRuntime 使用现有 ledger 和 scheduler 完成边界发布。缓存查询由所有 rank
共同决定，任一 rank 需要刷新时，其他 rank 也参加。刷新前使旧 ghost 不可读，
保留 interior 版本和 token 高水位；只有全部轴及每 rank 的发布前检查成功才发布。
版本、slot、epoch 必须一致；completion token 属于各 rank 的本地时钟，不能因
不等量分块的上传次数不同而要求其数值相等。

G1 提交 9f655aac 当时只接受 Cartesian 同层边界。mixed-level 在任何 storage 回调前共同拒绝；
用户边界、GPU Hydro 宏步、reflux、设备迁移与应用开放仍由后续阶段单独验证。
GPU MPI 和 MPI-AMR 的 ExecutionPreflight 门槛未改。

[Runtime 双进程见证](../../tests/cuda/amr/test_cuda_mpi_runtime.cpp) 链接原
arch_driver_runtime 和完整 arch_cuda_backend。三块 X 根网格形成不等量 rank
存储；Host 镜像在上传后覆盖，所有字段、species、ghost 与其他槽通过独立设备
snapshot kernel 对照原串行 Host boundary recipe。另覆盖非对称缓存、单侧错误、报文容量、
存储 generation、禁止写 active cell、重叠目的区、交换失败后的重试，以及重复 Host
materialization 的缓存。

CPU 既有跨入口失败测试曾发现新增标识扩大公共报文造成 MPI_ERR_TRUNCATE。
修正保留原 18-word 公共头，在入口 tag 一致后再校验 storage/版本；失败日志
保留。CPU CI 追加 storage begin/gather/scatter 单侧拒绝、Host/Device 模式不一致、
mixed-level 拒绝，以及 ledger ghost 失效后的 token 高水位与 pending-transfer 检查。

首个提交 2bc272fd 的双方手动 CI 已通过：
[朋友仓库](https://github.com/Shiro-Akane/ARCH/actions/runs/43121730586)、
[自有仓库](https://github.com/Arsenic-er/ARCH/actions/runs/43121734160)。
本增量单独记录构建、实机数据和 CI 状态，不沿用前一提交的成功状态。

## Runtime 增量验收记录

Release 与 Debug 均已完成完整 arch_cuda_backend / arch_driver_runtime 构建，
并在 H100-20C 上以两个 MPI rank 执行 Runtime 与原低层 staging 见证，无跳过。
Runtime 每个构建覆盖 18 个布局：1/2/3 维、species 1/4/37、outflow/periodic；
三个槽、两轮重复，共每 rank 252 次全状态对照、59 次双 rank 拒绝检查。
这里对照的是所有逻辑网格单元；Host materialization 另外验证对齐填充区保持未写入。
原低层见证继续通过 18 布局、162 次槽交换、324 次 phase 对照。

Runtime 边界调用的累计传输计数如下，两种构建一致。H2D 包括元数据，
D2H 只计边界调用内的字段回传；初始化、独立 oracle 读回和最终 materialization
不计入这张表。它是测试流量记录，不是扩展或性能结论。

| rank | H2D 字节（含元数据） | D2H 字节 |
| --- | ---: | ---: |
| 0 | 371,845,968 | 370,281,600 |
| 1 | 770,105,232 | 766,945,920 |

刷新后 cudaMemGetInfo 观测到的整卡最大已用量为 2,197,422,080 字节，
包含两个进程的 CUDA 上下文和测试分配；不是每 rank 用量，也不是瞬时峰值测量。
两个 rank 共用同一张 GPU，没有多 GPU 或多节点结论。迁移接收预留、容量耗尽
及瞬时峰值仍由 A1 的资源验收覆盖。

CPU Release/Debug、MPI ON/OFF 共 56 项相关 CTest 通过且无跳过；
追加 source-version 不一致用例后，四种构建的协议回归也再次通过。
629 项 tooling 全部通过且无跳过。CUDA OFF 或 MPI OFF 不注册 GPU MPI 测试。
已有 CUDA AMR exchange 与 regrid transaction 在两种构建下也全部通过；结果记入
[本增量结构化摘要](../../validation/backend/results/mpi-gpu-runtime-20261112/summary.json)。

以下测试开发失败记录保留：先于 Runtime 构造初始化分区；
串行参考调用原 Host recipe，保留生产 Runtime 的 communicator 校验；
独立 snapshot 按 Interior/Ghost 租约读取；materialization 对照逻辑单元，
另外断言 padding 未被写入。没有放宽数值门槛或删除有效字段检查。
生产缺陷 MPI_ERR_TRUNCATE 的原失败记录和修复后的四种 CPU 回归也保留。

本地证据位于 /home/ubuntu/projects/ARCH-mpi-evidence-gpu-runtime-20261112；
包括命令、原失败、最终 JUnit、源码/二进制哈希及传输计数。
本增量尚未执行 GPU Hydro 宏步、mixed-level、reflux 或迁移；
没有 compute-sanitizer 结果，也未构建/开放完整 CUDA MPI 应用。
新提交 CI 在推送后单独触发，最终状态保存在同一证据目录。

## G2 的实施顺序

G1 完成后，先在原 ExecuteRemoteCoarseFinePlan 的请求/回复流程接入设备存储。
源 owner 使用现有 CoarseFineCellPlan 及 CUDA coarse/fine gather 生成最终 ghost
值；收到完整回复并共同通过检查后，目的 owner 再 scatter。这样保留原 interpolation、
restriction 和守恒配方，不需要给本 rank 建立远端设备块，也不回传整个流体状态。

先验收双 rank、二维五叶 mixed-level 拓扑的三个槽和角点，再接入现有 flux register /
reflux 与 Euler、RK2、RK3 的完整宏步。设备端单侧失败必须在下一次通信前共同
退出，失败恢复保留已接受的 interior、拓扑、池及 CUDA store。随后才将 Host
迁移/同胞合并事务接到 CUDA StoreTransaction，并处理接收容量与退休事件。
下节记录 ghost 交换的落实；完整宏步与迁移另行验收。

## G2-1：跨 rank GPU 粗细 ghost 交换（2026-11-13）

本增量使用原 ExecuteRemoteCoarseFinePlan 的请求/回复、owner 校验和完整性检查。
本地 donor 与远端请求的 donor 都通过原 CoarseFineCellPlan 及 CUDA gather 计算，
完成后回传紧凑的 6+species 字段包。目的 owner 在全部回复通过检查后，
复用 G1 的 scatter 写 ghost，再执行原二维角点补齐顺序。只为本 rank 分配字段。

原 CUDA coarse/fine lowering 由本地完整交换和源端 gather 共用；launcher 的
gather-only 参数必须显式传入。共享数学库及 CUDA 数值 kernel 主体逐字核对未变，
插值、限制、composition closure、几何权重和错误阈值均沿用原实现。

原 upload_initial_state 同时承担字段上传和 flux/reflux 准备，会把远端通量依赖
带进独立边界测试。现将通量准备明确为 prepare_device_flux_plans；完整应用及
原有测试仍在上传之后立即调用它，顺序不变。原全局拓扑 registry 让每 rank
在有细化块时一致拒绝尚未支持的分布式 Device flux/reflux 准备，
避免某个 rank 因本地通量计划为空而误认为已经就绪。边界测试单独验收状态上传
与 ghost 操作，并检查 flux/reflux 仍不可用；没有开放应用预检。

新增 mpi_cuda_mixed_2 与同层见证共用原 Runtime 测试文件，五叶网格先通过
已验收的 Host 细化事务构造。rank 0 持有一个 coarse leaf，rank 1 持有四个 fine leaf；
覆盖二维 Cartesian、species 1/4/37、outflow/periodic，共六种组合。
每种构建共每 rank 102 次全状态对照和 41 次双 rank 拒绝检查，
包括三个槽、非对称缓存、写入完成后单侧拒绝、源端无效密度、恢复重试、
Host materialization 与 padding。Host 镜像覆盖为哨兵，禁止从旧 Host 字段取得答案。

同层、active cell、未刷新槽、padding 继续逐位比较；
粗细 ghost 运算使用已有 test_cuda_amr_exchange 的 2e-13 相对尺度门槛，
另检查 species 有限且位于 [0,1]，没有放宽原门槛。
这是边界操作验收，不是守恒时间演化或 GPU Hydro 宏步验收。

失败记录保留在 /home/ubuntu/projects/ARCH-mpi-evidence-gpu-mixed-20261113：
固定根网格构造限制、测试父状态不满足物理可接受性、初始上传耦合远端通量绑定、
rank 间通量就绪状态不一致，以及源端调用遗漏 gather-only 参数。
最后一项曾使旧 scatter 访问无效目的索引；修正为显式参数后重新执行完整实机矩阵。
这些失败没有通过删检查、放宽数值门槛或修改公式解决。

Release 与 Debug 的 GPU MPI、原 CUDA AMR exchange/regrid 共 10 项 CTest 通过且无跳过。
CPU Release/Debug × MPI ON/OFF 的 coarse/fine、exchange 与生命周期回归共 24 项通过；
629 项 tooling 通过且无跳过。CUDA OFF 或 MPI OFF 不注册新增 GPU MPI 用例。

G1 基线 9f655aac 的双方 CI 已在本轮核对全部成功：
[朋友仓库](https://github.com/Shiro-Akane/ARCH/actions/runs/43177651764)、
[自有仓库](https://github.com/Arsenic-er/ARCH/actions/runs/43177664080)。
G2-1 新提交另行触发 CI，不沿用该结果。

源码、JUnit、二进制、通信字节与最终回归结果见
[本增量结构化摘要](../../validation/backend/results/mpi-gpu-mixed-20261113/summary.json)。
同一 GPU 上另有实验，本轮时长及 cudaMemGetInfo 总量不代表独占性能或本项目峰值。
数据和构建保留在服务器；本增量没有进行额外文件清理。

下一步 G2-2 接原通量捕获、跨 owner register 汇总和 reflux 提交：
先确保缺包/重复包/错误 owner/单侧计算失败不能提交，再分别验收
Euler/RK2/RK3 的跨 rank GPU 宏步。之后才进行设备迁移、重网格与输出/重启。
本次尚未执行这些步骤，完整 GPU MPI/AMR 应用仍未开放。

## G2-2a：GPU 面通量捕获、MPI 注册与原 CUDA reflux（2026-11-13）

这个增量在原 CudaBackend 中准备本地源面的紧凑缓冲，复用已有面捕获 kernel；
每个 Hydro stage 完成后，D2H 按原 FluxRegistrationPlan 的操作顺序生成原始值。
远端目的块不参与 Device lowering，也不分配远端流体存储。现有
FluxRegister::ApplyDistributedRegistrationStage 继续拥有 owner、配方、
阶段权重、收包完整性及累加顺序。粗网格 owner 将接受后的 register 面上传，
再调用原 CUDA reflux kernel。未新增 MPI 内存池、分配器或推进管理器。

原 amr_initial_flux 面缓冲在这里复用为每阶段的源面捕获区；与 RKL 初始算子缓存
并用会被拒绝。原 amr_flux_register 仍保存 reflux 输入。上传必须匹配原 Host
register 的拓扑和已完成阶段，新 Hydro stage、上传失败或一次 reflux 调用都会
使旧上传凭据失效。设备流完成后才能将包交给 MPI 或发布消费完成。

原 ExecuteDistributedReflux 增加实际 storage 消费回调。Host 路径仍执行原
CommitReflux；Device 路径先复用原几何/布局验证和公共 21-word 身份头，再调用
storage。Host/Device 入口混用在回调前共同拒绝；只有全部回调成功才发布完成。
回调异常不自动恢复已经写过的 Device 字段，调用者必须恢复快照才能重试。
因此 DriverRuntime 的分布式 flux 准备门槛、应用 ExecutionPreflight 均继续关闭。

新增 mpi_cuda_flux_2 使用原五叶二维 Cartesian 拓扑，两个 rank 分别拥有
一个 coarse leaf 和四个 fine leaf。species 1/4/37 × outflow/periodic 为六个布局。
对同一 Current 独立重复执行原 Euler stage，注册权重依次为 0、1/2、1/6、2/3；
它覆盖零权重和累加，但不是完整 RK2/RK3 时间演化。每份原始通量对照原 Host
HLLC/PCM 求解器，累加结果及最终 reflux 对照原 Host 实现，沿用已有
2e-13 × max(1, |a|, |b|) 门槛；ENUC、非目标槽及 ghost 保持逐位比较。
Host 镜像在捕获和注册期间覆盖为哨兵，reflux 对照输入仅在最后恢复，未重新上传。

失败用例覆盖单侧 Hydro 绑定缺失、报文缺值、NaN、错误 owner、重复阶段、
过期/重复捕获、错误完成阶段、过期上传凭据、入口模式不一致及上传完成后的
单侧拒绝。上传拒绝用例尚未执行 reflux kernel，不能替代“部分 Device 字段已写入”
后的全宏步恢复验收。CPU 回调见证首轮错误调用了受保护的串行入口；已修正为
测试专用独立存储使用原串行执行器，生产 MPI 入口保护保留，失败日志保留。

H100-20C 上 Release/Debug 的 GPU MPI 与原 CUDA AMR exchange/regrid 共 12 项
CTest 通过且无跳过；每种构建的新通量见证均通过 54 次对照和 198 次共同拒绝检查。
CPU Release/Debug × MPI ON/OFF 共 18 项相关回归通过，629 项 tooling 通过且无跳过。
CUDA OFF 或 MPI OFF 不注册 GPU MPI 用例。没有 compute-sanitizer 结果。

G2-1 基线 a786735b 的双方 CI 已核对全部成功：
[朋友仓库](https://github.com/Shiro-Akane/ARCH/actions/runs/43326173851)、
[自有仓库](https://github.com/Arsenic-er/ARCH/actions/runs/43326178223)。
本增量推送后另行触发对应提交的 CI，不继承基线结论。
[结构化摘要](../../validation/backend/results/mpi-gpu-flux-20261113/summary.json)
记录 JUnit、二进制和源码哈希；原始证据及保留的失败日志位于
/home/ubuntu/projects/ARCH-mpi-evidence-gpu-flux-20261113。
后续 G2-2b 才把这条通量路径纳入原 Runtime 的 Euler/RK2/RK3 宏步及快照恢复；
之后依次验证 Device 迁移、重网格、容量和输出/重启。当前没有完整 GPU MPI-AMR、
多 GPU、多节点、长程守恒或性能扩展结论。

## G2-2b：原 Runtime 宏步与 Device 失败恢复（2026-11-13）

原 Runtime 的 HydroAttempt 现在同时保护 Host 与 Device 路径。CPU 保留原字段、
slot permutation、ledger、FluxRegister、边界上下文和控制器快照；CUDA 后端用原
DeviceStateStorage 和同一有序流保存三个槽。所有分配完成后才复制并发布快照。
失败时先排空流，恢复原物理分配与槽位，再恢复 Runtime 元数据；成功时释放快照。
没有独立的 MPI 时间推进器、内存池或 ownership 管理器。

DriverStages 继续调用原 Euler/RK2/RK3 scheduler。每个真实阶段执行 GPU 求解后，
按原 descriptor 权重把紧凑面通量交给原 MPI FluxRegister；旋转后的粗网格 Current
接受原 CUDA reflux。所有本地失败先共同结束，再进入下一次通信。最终 EOS 验证
在 Device 单元上调用原 state::validate_eos，只读回每块状态码，未改任何物理公式
或容差。三个槽的备份是设备内复制，不用于 Host 计算替代。

各 rank 的初始化上传次数不同；宏步开始及失败恢复时取已发出 scheduler 标识的
全局高水位，避免重用或把本地上传次数误当物理时间。实际 dt、时间、step、epoch、
species 和 Host/Device 身份仍必须一致。物理时间只在原控制器接受步骤后推进。
宏步内禁止重建 backend 绑定、替换 flux plan、改变拓扑、暴露未接受 Host 字段。

新增 mpi_cuda_macro_2 使用原五叶二维 Cartesian 拓扑、HLLC/PCM/IdealGas，
两个 rank 分别持有一个 coarse leaf 和四个 fine leaf。三种积分器 ×
species 1/4/37 × outflow/periodic 共 18 个布局，每个布局接受 16 个连续时间步，
穿插 15 种失败后在同一个 Runtime 重试，不重置字段。Current 逐字段对照原串行
CPU 积分器；周期守恒沿用 CPU 的 2e-11 绝对加 2e-12 相对门槛。
Host/Device 算术对照仍为原有 2e-13 尺度门槛。

失败覆盖真实 stage 写入后、reflux/rotation 后、无效密度与组分、通量凭据丢失、
提前改变物理时间、不同 dt、非法 solver、重复事务、强制 materialization、
重网格、重新绑定和设备 store/flux plan 替换。恢复验证三个 Device 槽的逻辑字段、
完整 Host 镜像逐位不变，另检查 ledger、通量凭据、边界上下文、repair、
控制器及存储 generation。每种构建的预期计数为 306 次字段对照和 270 次共同拒绝。

旧 mixed-level 测试的“所有分布式 flux 准备必须拒绝”属于已过时结构约束，
现替换为 stale topology identity 拒绝，保留未准备 register 的拒绝与全部字段检查。
CPU 原来的失败原因断言仅同步去掉 Host 专属措辞，22 类宏步失败和动态迁移/
重网格组合检查均保留。原数学库、物理目录、CUDA hydro/AMR 数值主体未修改。

故障测试开发时，Euler 最后阶段不执行输出 ghost 回调，原注入点未触发。
改用原 scheduler 已有的阶段完成回调，确认实际 kernel 执行后再注入失败。
所有原失败日志保留；未删失败项或放宽门槛。工具测试使用既有完整服务器环境，
系统 Python 缺依赖和 conda Python 缺 pidfd 的环境失败也单独保留。

Release/Debug 的 GPU MPI 与原 CUDA AMR exchange/regrid 共 14 项 CTest 通过；
CPU Release/Debug × MPI ON/OFF 的 residency、scheduler、flux/reflux 和完整宏步/
迁移/重网格组合共 22 项通过；629 项 tooling 通过，全部无跳过。
两种 GPU 构建均完成 306 次字段对照和 270 次共同拒绝。最终 JUnit 保存全部
18 个布局的输出，不沿用早期成功日志的截断摘要。

验收记录见 [结构化摘要](../../validation/backend/results/mpi-gpu-macro-20261113/summary.json)；
原始构建、失败、最终 JUnit 和哈希保存在
/home/ubuntu/projects/ARCH-mpi-evidence-gpu-macro-20261113。
基线 48958aa3 的双方 CI 已成功：
[朋友仓库](https://github.com/Shiro-Akane/ARCH/actions/runs/43333249921)、
[自有仓库](https://github.com/Arsenic-er/ARCH/actions/runs/43333298110)。
新提交必须另行核对，不能继承基线结论。

此阶段开放的是原 Runtime 的 native 宏步路径，应用 ExecutionPreflight 仍关闭。
单张 H100 上两个 rank 的结果不代表多 GPU、多节点或长期扩展结论。下一步在
原 StoreTransaction 内拆分退休事件准备与不可抛异常的发布，让所有 rank
共同确认后再改变 ownership；随后验证迁移、重网格、接收容量、分布式输出和重启。

## 服务器资源与恢复

代码、构建、测试和证据位于 gpu-273312；Windows 仅发送控制命令。
CUDA Release/Debug 分别使用 build-mpi-cuda-release、build-mpi-cuda-debug，
配置独立 ARCH_RUNTIME_OUTPUT_DIRECTORY，不覆盖已有 CPU 构建。

2026-11-12 这一轮仅归档并移除两组过期 MPI 展开副本：

- ARCH-mpi-evidence-multidim-20261101：4,676 个文件。
- ARCH-mpi-evidence-optimization-20261101/regression：4,602 个文件。

9,278 个文件全部在服务器压缩保存、逐文件 SHA-256 读回核对，并实际恢复
每组最大文件比较哈希。删除前重新核验原文件内容和身份，未触碰当前工具链、
基线二进制、其他实验或 Git 历史。备份不是 Git 推送的替代说明，
而是包含原始字段/日志的独立完整 tar.gz。

备份、manifest、恢复验证与逐文件删除日志保留在
/home/ubuntu/projects/ARCH-mpi-storage-20261112。
多维归档 SHA-256：
42c15f288a532d694af647aa3093b019a4311dc3a96a52647e97a659d72b0e01。
回归归档 SHA-256：
ec57fab3889ee96777e81883d1acf54be981166f76c9cfb997c862ed55c5be9c。
恢复多维归档到 /home/ubuntu/projects；回归归档顶层为 regression，
应恢复到 /home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101。
先检查目的地不存在或为空，再解包；按相应 manifest 重新验证。
清理收据的可用空间从 10,931,126,272 增为 17,386,180,608 字节；
并发项目可能影响该磁盘观测，实际删除文件与压缩大小另有完整记录。

## 2026-11-13 旧构建清理

按用户要求，在继续通量开发前完成旧产物清理。八、九月份 19 个旧目录中的
693 个未被 Git 跟踪的 ELF、静态库和对象文件，逻辑大小 10,127,341,636 字节，
压缩保留为 4,304,451,534 字节。整个归档逐文件读回核对 SHA-256，实际恢复最大
成员核验后，重新检查源文件内容、身份和进程使用情况，再按清单逐文件删除。

清理前后的源码 HEAD、未提交状态和 diff 哈希一致；当前 MPI 构建、工具链、
原始实验数据、未提交源码及唯一归档均保留。没有整目录递归删除。
此前退役清单中的安装归档已不存在，本轮没有重复处理这些包。

删除收据观测可用空间从 15,248,486,400 增至 31,803,547,648 字节，期间有其他
任务的磁盘变化，也存在 XFS 共享，不能将这一差值全部归因于本次清理。
完整清单、归档、恢复说明和删除日志保存在
/home/ubuntu/projects/ARCH-mpi-storage-20261113；
[清理摘要](../../validation/backend/results/mpi-storage-20261113/summary.json) 随源码发布。
