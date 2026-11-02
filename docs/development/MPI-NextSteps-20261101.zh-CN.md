# MPI 下一阶段实施计划（2026-11-03 审核后更新）

## 当前起点与状态

上游 main 为 2e05e66e（v1.4.1），固定网格 CPU MPI 候选为 e43183d0；扩大实验和证据提交为 c6b0d628。本次修改只更新设计与验收计划，不代表下面的 mixed-level、迁移或 CUDA MPI 已实现。

开发、构建、测试、原始证据和 Git 发布仍只在 gpu-273312 / 150.65.181.188 的 /home/ubuntu/projects/ARCH-mpi 进行。旧 ARCH 工作区与历史实验不覆盖。现有实现达到“已有自动回归和扩展证据的 CPU 固定网格 MPI 候选”，没有达到完整 MPI 支持。

已完成共享 ExecutionPreflight、查询/真实运行的一致检查、MPI ON/OFF CI，以及 1/2/4/8/16 进程扩大实验。四种 Debug/Release × MPI ON/OFF 构建、22 项相关 CTest、110 组逐位回归对照、20 项预期失败场景、629 项工具测试通过；代码 PR #25 的 10 项检查通过。扩大实验共 62 次运行、60 组逐位对照；16 进程中位计算加速比分别为二维 11.58×、三维 12.91×。这是共享单机的特定工作负载结果，不能替代 AMR、GPU、多节点或长程演化验收。

完整证据见 [执行预检与扩大实验报告](MPI-Preflight-20261102.zh-CN.md) 及其关联 JSON。该报告中“原池和数学实现未变”描述已完成版本的事实，不是下一阶段的永久设计限制。

## 本次审核确立的约束

- 高可维护性、可读性和职责清晰优先于维持旧内部接口。可以修改原有 AMR、拓扑、本地存储、MemoryPool 分配、交换计划、CUDA storage/residency 和执行边界。
- 不为了旧接口另建配置系统、影子块、MPI 分配器、拓扑管理器或时间推进器。远端拓扑描述不拥有远端 FluidState；发送/接收暂存只服务现有执行器。
- 每项基础设施调整先记录真实消费者、具体缺口、职责与资源影响，再实现；不预先搭建没有消费者的通用通信框架。
- 物理定义、数学公式、数值容差和既定验收阈值保持不变。若必须触及数学实现，先提交文件/符号、改动依据、数值影响和验证方案供审核，获准后实施。
- 可以替换约束过时内部结构的测试断言，但必须列出原因及替代验证。物理、守恒、字段、失败处理和资源边界检查继续保留，不能删除失败项或放宽门槛。
- 此前“AMR 最后”“不改内存池”的阶段性约束由本次审核更新：固定网格三步已完成，现在允许启动最小分布式 AMR 接入。完整 AMR、GPU 与分布式输出/重启仍按各自验收逐步开放。

## 源码审阅发现与修改归属

以下是 e43183d0 的当前限制及下一阶段设计，不是已完成能力：

| 具体缺口 | 现有职责归属与拟议调整 | 必须保留的验证 |
| --- | --- | --- |
| [AmrTree](../../src/amr/topology/AmrTree.h) 的 InitRootGrid 限制固定根网格；UpdateNeighbors 使用本地 FindBlock；[Block::FaceNeighbors](../../src/amr/storage/Block.h) 保存本地池索引 | 在原拓扑描述中明确逻辑块、owner rank、handle 和本地可选存储绑定；远端邻居不能伪装成本地池索引。更新依赖邻居的计划生成者，直接处理远端端点 | 全局唯一归属、叶覆盖、邻接互反、2:1 平衡；同一 rank 同时有本地/远端邻居 |
| [TopologyIdentityRegistry](../../src/driver/runtime/TopologyIdentityRegistry.h) 目前按本地观察分配 UID、推进 epoch；仅 owner 变化也不会自动改变逻辑 key 集合 | 扩展现有 registry 与 [BlockIdentityAuthority](../../src/amr/topology/BlockHandle.h) 接收全局一致的 UID/epoch 决策；迁移保留 UID，owner 改变触发拓扑发布；池索引始终是本地 lowering | 不同 rank 的相同本地槽号不能碰撞；跨 rank 迁移保持身份；旧 epoch、旧存储 view 必须被拒绝 |
| [GhostExchange](../../src/amr/exchange/GhostExchange.h) 的远端路径只打包 level-0 X 根块面；粗细计划读取两端本地存储 | 复用 [AmrTransferPlans](../../src/amr/transfer/AmrTransferPlans.h) 和 [CoarseFineCellPlan](../../src/amr/exchange/CoarseFineCellPlan.h) 的数学配方；将几何/逻辑端点与存储绑定分开，在现有交换执行器中传输需要的面数据 | 原始字段/species、三种状态槽、粗细两方向、周期同 peer 双方向、棱角及消息完整性 |
| [AmrFluxPlan](../../src/amr/flux/AmrFluxPlan.h) 的构造和编译把参与端点绑定到本地块/寄存器 | 让原 flux plan 保留远端来源与粗面目的；只搬运同一配方需要的贡献或原始面值，在粗块 owner 上按既有运算顺序完成寄存器累积及 reflux | 细面覆盖完整、每份贡献只应用一次；漏项/重复拒绝；守恒和字段对照保持原门槛 |
| [MemoryPool](../../src/amr/storage/MemoryPool.h) 一次预分配全部槽，没有批量预留；[PreparedRegrid](../../src/amr/topology/AmrTree.h) 在旧块尚存时逐个分配新块 | 先在原池补容量/可用槽观测与成组预留，PreparedRegrid 消费预留；不足时在发布前一致失败。确需扩容或容量重分配时修改原池自身，见下节 | 局部耗尽、迁入预留、旧/新共存峰值、部分分配失败清理、旧 view 失效、全局资源上限 |
| [DriverRegrid](../../src/driver/runtime/DriverRegrid.cpp) 与 [TopologyTransaction](../../src/amr/topology/TopologyTransaction.h) 的提交/回滚只保证当前进程顺序 | 在原事务执行处加入跨 rank 准备状态汇合和提交前检查；最后发布回调仍只执行预备好的 noexcept 状态切换 | 不允许某 rank 已发布而其他 rank 尚在可失败步骤；失败时不继续演化或发布成功输出 |
| [DriverIO](../../src/driver/io/DriverIO.cpp) 目前各进程独立生成 plot_run_id；[OutputPath](../../src/io/OutputPath.h) 只分开 rank 目录 | 复用原 run identity、科学 writer 和文件发布设施；统一运行 ID，后续补 shard 集合 manifest/完成标记与重启协议 | 相同运行/步数/时刻/epoch、覆盖无缺漏重叠；缺 shard、重复 shard、混入旧结果、损坏文件必须拒绝 |

## 下一个可交付单元：两进程、五个叶块

这是最小验收案例的设计，还没有新的通过结果。先完成 CPU Cartesian 二维、两进程、一个粗细层差、同步时间推进，不同时扩大到任意维数、层数、源项或 CUDA。

1. 建立 2×1 个 level-0 根块。rank 0 持有左根块，rank 1 将右根块细化为四个 level-1 叶块，共五个活动叶块。先用原生块尺寸、全周期边界，检查内部与周期接缝两处粗细交界；两个方向可以连接同一 peer，不能依赖不同 peer 来消除消息歧义。
2. 复用已有 [重网格字段夹具](../../tests/fixtures/amr/regrid_migration_fixture.h) 与 [组分夹具](../../tests/fixtures/amr/amr_composition_test_cases.h)。传输测试覆盖带符号 ENUC 和 trace species；演化另用满足既有 EOS/可容许性条件的恒定态与周期平滑流，不能将传输标记场直接当成物理解。
3. 构造同一全局叶网格的单进程原生执行基线，再比较双进程 ghost、面贡献、reflux 后字段和 accepted state。预先固定细化/迁移事件，禁止测试期间两份运行自行选择不同网格后只比统计量。
4. 在完整 accepted step、reflux 完成且通信/后端租约均已结束的边界，将一个 fine leaf 从 rank 1 迁到 rank 0。这样必须真正支持非根块条带的所有权；搬运整条根块带不能充当该项通过。
5. 再将该叶迁回，让四个 siblings 在同一 owner 上执行既有 restriction/coarsening，恢复两个根块。首个单元不实现跨 owner siblings 的分布式 coarsening；未满足聚合条件时必须明确拒绝，不能遗漏远端 siblings。
6. 成功配置先采用全局 max_blocks=16，并记录实际每 rank 容量。额外构造局部容量不足与迁入预留不足的失败配置，不能因为全局空闲足够就假设每个 rank 都能成功分配。

实施顺序及完成条件：

| 顺序 | 实现与直接消费者 | 通过条件 |
| --- | --- | --- |
| A：远端身份与所有权 | AmrTree/registry/原计划生成器；两进程五叶拓扑夹具 | 全局 UID、owner、epoch 一致，远端端点不拥有本地 FluidState；唯一覆盖、邻接和旧 handle 检查通过 |
| B：粗细面交换与 reflux | GhostExchange、原 transfer/flux 执行器与原 scheduler | 与单进程同拓扑同序运算对照逐位一致；独立粗细贡献、守恒、组分测试继续通过；周期双向和角区正确 |
| C：迁移与提交 | PreparedRegrid、MemoryPool、TopologyTransaction、DriverRegrid | 一个细叶跨 owner 往返，accepted 字段/身份不丢失；容量、故障、stale view 与提交顺序检查通过 |
| D：应用入口与回归 | 原 ExecutionPreflight、真实运行和现有 CI | A–C 完成后只开放明确通过的能力范围，查询与运行一致；四种构建、固定网格及受影响单进程 AMR 回归通过 |

A–C 可先由直接调用生产设施的原生测试夹具验证，不能以测试绕过检查为依据宣布应用支持。不得提前删除所有 MPI-AMR 拒绝条件；预检开放必须与真实启动、资源和错误路径的能力匹配。

## 所有权、迁移与提交的具体设计

### 身份与计划

初始小案例可在重网格边界交换紧凑的全局叶元数据，由原拓扑/身份设施按全局逻辑 key 的确定顺序分配新增 UID，并共同推进 epoch/high-water mark；不复制远端场数组。首次采用和后续新增使用同一规则，幸存/迁移叶保留 UID，已退役 UID 不复用，计数溢出直接失败。初版全局元数据交换仅服务这个实际消费者，不作为大规模可扩展目录已经完成的证据。

owner 变更即使不改变叶 key 集合，也必须触发新的拓扑发布；只发生本地存储重绑定时，按原存储 generation/lease 机制失效旧 view，不通过改写 UID 掩盖存储生命周期。所有计划携带 epoch、原操作顺序和字段布局；消息标识还必须区分阶段、状态槽、方向与事务，检查计数和布局后再 scatter。

细化/粗化建议在拓扑 owner 中汇合；跨 rank 的 2:1 平衡检查不能只扫描本地邻居。首次只执行预定五叶变换；扩展自动 AMR 时再用同一拓扑规则传播标记直至全局稳定。几何、restriction/prolongation 权重与 reflux 符号仍由现有数学配方计算。消息到达顺序不能改变累加顺序。

### 资源与原池调整

全局 max_blocks 的已声明资源边界继续有效。分别观测活动叶、实际分配槽、各 rank 容量和字节峰值，不能把三者混为一谈。

初版先在现有 MemoryPool 内提供“检查需求并批量预留、成功转交、失败释放”的能力，由原 PreparedRegrid 持有未发布预留。保留旧 accepted 源块直到迁移成功且全体可提交。上例右根细化时旧父块与四个子块共存，右 rank 至少需要五个块槽；全局叶数五不能用来证明五个实际槽就足够。迁移时发送方旧副本与接收方新副本也会同时占用槽。

若真实案例证明固定容量分摊阻碍迁移，优先在原池中实现按需分段存储/容量调整，并由原拓扑事务调整未使用容量配额，不再增加 MPI 专用池。需要先测量并比较两种具体代价：连续容器扩容会使 Block/view 地址失效，要求静默边界统一重绑定；分段存储可稳定槽地址，但改变遍历局部性和容量管理。选型依据必须包含分配次数、RSS、迁移峰值、旧 view 检测与现有单进程性能，不为尚未出现的需求提前实现两套路径。

字节峰值必须同时计入旧/新块及各状态槽、species、源状态备份、flux register、发送/接收缓冲、拓扑元数据；CUDA 阶段还包括 pinned staging 和 Device 旧/新 store。需要扩容也不能静默超出资源预算或改变 max_blocks 语义；如确需改变用户可见预算模型，先单独列出变更和兼容影响。

### 分布式提交顺序

1. 在 accepted step 边界排空旧 epoch 的 halo、reflux、设备事件与租约；保留可恢复的旧 accepted 状态。
2. 各 rank 完成候选拓扑、全局身份/owner、预算和发送/接收长度的准备。所有可预见的分配失败先转为准备状态，按相同顺序汇合；没有一致成功就不投递迁移载荷。
3. 预留目标槽，按原生字段布局迁移；收到完整有效载荷后，在候选状态上执行既有迁移/边界/粗细交换，并准备 ledger、lowering 和缓存失效信息。
4. 全体确认最终可提交后，才进入本地 noexcept 发布。发布回调不能临时分配、复制或调用可失败通信；旧源释放和缓冲复用必须等待请求/事件以及对端接收完成。
5. 进入下一步或输出前完成提交确认。发布前错误清理未提交资源；通信/进程故障或发布后的不可恢复错误中止整个作业，不宣称普通 MPI 提供进程故障下的分布式原子恢复，也不继续使用混合 epoch。

沿用现有 TopologyTransaction 的 Prepared/Migrating/ReadyToCommit/Committed/Aborted 状态，不在外围复制第二套事务状态机。跨 rank 阶段协调由原 DriverRegrid 负责，状态错误与 MPI 失败进入已有错误终止边界。

## CUDA、输出与运行环境的后续范围

- CUDA 先走可移植 Host staging：原设备执行器按共同交换配方打包，D2H 完成后才能发送；MPI 接收完成后才可 H2D/unpack，设备完成事件后再发布原 residency/completion token。每一阶段的缓冲、旧 store 和租约存活到所有消费者完成。Host staging 可复用已有工作区/生命周期，不能变成每步全域镜像。取得实机正确性、内存与时间证据后再考虑 device-aware MPI。
- 输出复用现有 HDF5、科学身份、临时文件和发布设施。运行开始由一个 rank 生成并广播共同 run ID；保留各 rank 的执行镜像/源身份作一致性检查，不能用共同 ID 掩盖不同二进制或输入。
- 首个集合协议限共享可访问输出目录：各 rank 发布不可变 shard，集合清单记录 schema、run ID、步数/物理时刻、epoch、字段/species 布局、全局逻辑覆盖、预期成员及校验。只有全部 shard 已验证，才原子发布集合完成标记；读者拒绝未完成集合。独立节点磁盘另需明确收集/可达性协议，不能假定本地 rename 等于全局发布。
- 重启先支持相同 rank 数与所有权布局，复用现有 checkpoint compatibility/原生字段检查；更换 rank 数的重分区恢复单独验收。记录 UID 高水位、拓扑/控制器状态及所有必要原生字段，禁止从 plot 派生量猜测缺失 checkpoint 字段。
- main 当前已请求并验证 MPI_THREAD_FUNNELED。后续仍只由初始化线程执行 MPI，OpenMP worker team 必须结束后再通信；一个任意 worker 的 critical 区不能满足 FUNNELED。进程环境、communicator、线程等级和错误终止可在现有入口/运行上下文中适度集中，不创建通用并行运行时。
- 当前跨 rank 启动校验只比较 problem 名与参数文件内容。扩展前核对实际执行镜像/构建身份、相关科学资产及字段布局、MPI 能力与后端选择；节点/CPU 信息记录为部署证据，不因硬件不同就改变物理算法。身份检查在分配/发布输出前完成。

## 验收与证据要求

- 原有 MPI-OFF、MPI-ON 单进程、固定网格 1/2/4 进程、预检/真实运行一致性、失败退出及覆盖检查继续执行；16 进程性能证据保持原样，不用新测试重命名覆盖旧结果。
- 新的确定性打包/迁移标记场逐位检查；同一 mixed-level 网格按既有次序执行的单/双进程字段逐位对照。若出现浮点次序差异，先定位次序并提交证据，不以增大容差通过。
- 守恒、物理可容许性、species 归一与 trace 行为、时间/步数、reflux 覆盖使用既有独立测试及原阈值。可更改的是本地索引等内部结构断言，并同时增加远端 owner/handle 对应的等价行为验证。
- 对准备分配失败、局部耗尽、接收长度/epoch/字段不符、重复/遗漏贡献、延迟接收、旧 handle/view、部分写入和单 rank 失败分别验收。要求无错误继续推进、无成功集合标记、无永久挂起，已完成证据不能被失败运行覆盖。
- 先冻结新案例输入、二进制/源码身份、拓扑事件表、判断标准和资源上限，再运行。每次结果记录所有 rank 的退出状态、全局逻辑坐标、原始字段与资源数据；按既有归档/hash/readback 流程保存原始证据。
- mixed-level 正确性、动态 AMR、CUDA staging、分布式输出/重启、多节点性能、长程演化分别记录完成状态；某项通过不代替另一项。性能优化在正确性闭合后以测量决定，继续保留全部重复样本并分开计算、通信与输出时间。

## 分支与审阅入口

复用当前两个 MPI 分支，不为设计、单个案例或每次实验新增分支：

| 分支 / PR | 当前职责与推进方式 |
| --- | --- |
| MPI/execution-preflight / #25 | 保留已验收的固定网格候选及其 CI，供现有集成审阅。必要修复仍在此分支提交；不合并 main，除非得到明确合并指令。 |
| MPI/scaling-16 / #26 | 继续承载扩大实验、当前设计及接下来的最小案例增量。它已经包含 #25；当前本次增量只有文档，后续有代码时同步重写 PR 标题/说明以反映实际内容。 |
| codex/mpi-fixed-grid-validated-20261102 / #24 | 已被上述历史包含。2026-11-03 核对时 #24 已关闭，朋友仓库旧分支已移除；个人仓库旧引用仅作历史快照，不再推进。 |

所有新提交仍同步推送双方仓库。同一修改只在一条分支实现；必要时通过正常合并带入后继分支，不对两个分支分别维护两份实现。#25 集成后，再以实际 main 状态整理 #26 的 base 和差异；不为更名增加额外并行开发线，不随意重写已审阅历史。

## 历史证据索引

以下记录按各自日期和源码身份保留，不能把历史阶段约束当成当前永久要求：

- [迁移与保全](MPI-Migration-GPU273312-20261101.zh-CN.md)：迁移起点 29972a1、记录 cb97c71，旧工作区保留。
- [多维固定网格](MPI-Multidimensional-20261101.zh-CN.md)：b94b187 对照独立串行 main 5dc6d7b，108 组比较、22 项 CTest、20 项拒绝检查。
- [单机及双服务器验证](MPI-Scaling-20261101.zh-CN.md)：42+4 组逐位对照及 2 项双机 Host 测试；不是新 AMR 或集群性能证据。
- [早期通信测量](MPI-Communication-20261101.zh-CN.md)：CFL 集合调用微基准改善，不等价于应用总耗时改善。
- [最新主线整合与通信验收](MPI-Controlled-20261102.zh-CN.md)：此前较长强/弱扩展、双服务器放置与 X 面双向通信的独立记录。
- [预检、CI 与扩大实验](MPI-Preflight-20261102.zh-CN.md)：本计划当前候选及 16 进程证据。
