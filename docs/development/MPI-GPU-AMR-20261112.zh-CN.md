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

当前路径只接受 Cartesian 同层边界。mixed-level 在任何 storage 回调前共同拒绝；
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

## G2 的具体入口

G1 完成后，先在原 ExecuteRemoteCoarseFinePlan 的请求/回复流程接入设备存储。
源 owner 使用现有 CoarseFineCellPlan 及 CUDA coarse/fine gather 生成最终 ghost
值；收到完整回复并共同通过检查后，目的 owner 再 scatter。这样保留原 interpolation、
restriction 和守恒配方，不需要给本 rank 建立远端设备块，也不回传整个流体状态。

先验收双 rank、二维五叶 mixed-level 拓扑的三个槽和角点，再接入现有 flux register /
reflux 与 Euler、RK2、RK3 的完整宏步。设备端单侧失败必须在下一次通信前共同
退出，失败恢复保留已接受的 interior、拓扑、池及 CUDA store。随后才将 Host
迁移/同胞合并事务接到 CUDA StoreTransaction，并处理接收容量与退休事件。
这些是后续工作，当前增量不宣称已执行。

## 服务器资源与恢复

代码、构建、测试和证据位于 gpu-273312；Windows 仅发送控制命令。
CUDA Release/Debug 分别使用 build-mpi-cuda-release、build-mpi-cuda-debug，
配置独立 ARCH_RUNTIME_OUTPUT_DIRECTORY，不覆盖已有 CPU 构建。

本次仅归档并移除两组过期 MPI 展开副本：

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
