# MPI 下一阶段实施计划（2026-11-04 更新）

## 当前起点与状态

2026-11-04 再次 fetch 后，上游 main 仍为 2e05e66e（v1.4.1）；固定网格 CPU MPI 候选为 e43183d0，扩大实验和证据提交为 c6b0d628。A1 全局拓扑身份与所有权已在 55708157 完成，A2 远端邻接在 d7bff58e 完成。B1 实际跨 rank 粗细 ghost 交换已在 cbdf0b73 完成，其 GitHub CI 41545717485 已核实成功。本次继续 B2a：跨 rank 通量注册与 reflux 算子边界。B2 的完整 mixed-level 时间推进仍未闭合，数据迁移和 CUDA MPI 尚未完成，应用预检继续拒绝 MPI-AMR。

开发、构建、测试、原始证据和 Git 发布仍只在 gpu-273312 / gpu-273312-ts（当前 100.97.101.5；直连 150.65.181.202） 的 /home/ubuntu/projects/ARCH-mpi 进行。旧 ARCH 工作区与历史实验不覆盖。现有实现达到“已有自动回归和扩展证据的 CPU 固定网格 MPI 候选”，没有达到完整 MPI 支持。

上一轮已完成共享 ExecutionPreflight、查询/真实运行的一致检查、MPI ON/OFF CI，以及 1/2/4/8/16 进程扩大实验。四种 Debug/Release × MPI ON/OFF 构建、22 项相关 CTest、110 组逐位回归对照、20 项预期失败场景、629 项工具测试通过；代码 PR #25 的 10 项检查通过。扩大实验共 62 次运行、60 组逐位对照；16 进程中位计算加速比分别为二维 11.58×、三维 12.91×。这是共享单机的特定工作负载结果，不能替代 AMR、GPU、多节点或长程演化验收。

完整证据见 [执行预检与扩大实验报告](MPI-Preflight-20261102.zh-CN.md) 及其关联 JSON。该报告中“原池和数学实现未变”描述已完成版本的事实，不是下一阶段的永久设计限制。

## 已完成：A1 身份与所有权

[TopologyIdentityRegistry](../../src/driver/runtime/TopologyIdentityRegistry.h) 继续拥有 UID、epoch 和发布事务；新增的 [同目录实现](../../src/driver/runtime/TopologyIdentityRegistry.cpp) 仅在既有拓扑边界收集逻辑叶元数据，直接由 DriverRuntime 初始化与 DriverRegrid 调用。没有新建 MPI 管理器、分配器、远端 FluidState 或数学实现。

每个 rank 保存全局逻辑身份与 owner，只有本地块拥有 pool-index lowering 和 residency handle 列表。首次采用和新增叶按全局逻辑 key 排序分配 UID；owner-only 变更保留 UID、推进 epoch，本地槽重绑定不改变 UID/epoch。完整叶覆盖通过整数 sibling 折叠检查，拒绝缺口、重叠和重复身份。序列化显式使用五个 uint64 字段，owner 来自发送进程，池槽号不跨进程传输；检查 MPI count 上限及单 rank 输入错误，后者由共同归约拒绝。

[五叶测试](../../tests/host/amr/test_topology_ownership.cpp) 在真实双进程中调用同一个生产收集函数，覆盖初始归属、单叶 owner 往返、粗化后的 UID 退役、本地槽重绑定、旧候选/旧绑定拒绝、单 rank 错误以及空本地叶集合。迁移与粗化在这里是**身份元数据事件**，没有搬运流体字段；注入的 finalizer 失败只验证原 registry 的本地发布不变性，尚未建立跨 rank 原子提交。空 rank 仅为元数据边界测试，现有应用的非空固定分区约束保留。

[固定网格 Host 测试](../../tests/host/amr/test_mpi_fixed_grid.cpp) 改为通过生产 registry 获取身份，替换原先手工拼装 handle 的夹具；原有所有字段、species、三种状态槽和 1D/2D/3D 通信检查保留。[CI](../../.github/workflows/ci.yml) 的 MPI ON/OFF inventory 加入所有权测试，pull_request 范围同时覆盖现有 MPI 分支作为 base 的堆叠 PR，使 #26 的代码增量得到真实回归。

这一版每个 rank 收集并持有 O(全局叶数) 元数据，暂存和事务候选也计入该成本；它是小案例的具体实现，未作大规模目录或性能结论。rank 间域定义仍依赖现有启动输入一致性检查。A2 补齐远端邻接与全局 face 2:1 检查；局部容量/迁入预留、跨 rank 成功协调及字段迁移继续属于 B/C；DriverRegrid 的本地发布逻辑尚不能当作分布式重网格已完成。

### A1 验收（2026-11-03）

- Release MPI ON/OFF 完整应用重新构建；启动/预检/通信/所有权共 12 项 CTest、受影响的单进程 AMR/水动力/拓扑消费者共 8 项 CTest 通过。Debug MPI ON 的 8 项及 OFF 的 3 项相关契约测试通过；本轮未重建完整 Debug 应用。
- 复用未改的生产积分脚本，对照冻结的独立串行基线：1D 的 27 组与 2D/3D 的 28 组，共 55 组按全局坐标重建后逐位一致；积分脚本另有 10 个预期拒绝场景通过。没有放宽数值门槛，也未重新声称 16-rank 性能。
- 工具测试 629 项通过、无跳过；架构审计、actionlint、CI inventory/结果完整性与文档链接检查通过。55708157 的 GitHub Actions 运行 41515171065 已通过 Studio/Host、Tooling、CPU Release、MPI fixed-grid 和 CI required 五个作业。
- 原始输出、输入和日志分为两个压缩归档，全部 2,340 个文件逐一读回校验，并实际重开 HDF5；仅在归档验证后移除本轮 RAM 暂存。服务器磁盘保留 20 GiB 下限。没有删除另一对话审核的历史目录或合并 Python 环境。
- 证据目录为 /home/ubuntu/projects/ARCH-mpi-evidence-ownership-20261103，包含 source.json、validation-summary.json、acceptance.json、JUnit、逐文件 manifest 和 archives。二进制在提交前构建，内嵌 Git 标识仍来自 25a514cf 的 dirty 工作树；source.json 记录编译时输入与二进制哈希，发布前核对代码字节一致，不能把内嵌标识当作新的提交号。

| 原始证据归档 | 字节 | SHA-256 |
| --- | --- | --- |
| production-fixed_grid.tar.gz | 38026448 | 21ffbe813237a1a482243733a2306a09b59a52942d96d9b9a6deb83713ea1ae0 |
| production-multidim.tar.gz | 34171923 | 82ed992d82205871f4b15b31315eb3f22d3061af102cf13c87335d6c4b5fc617 |


## 本轮实现：A2 远端邻接与原交换计划

[BlockHandle.h](../../src/amr/topology/BlockHandle.h) 统一既有逻辑 key，并提供仅含逻辑坐标、handle、owner 和可选本地池槽的 BlockTopology 记录。原 [Block::FaceNeighbors](../../src/amr/storage/Block.h) 直接替换只存本地 ids 的表示，没有并列保留影子邻居表。registry 在拓扑边界导出这些记录；远端记录的 pool_index 为 -1，不分配远端 FluidState。

[AmrTree::UpdateNeighbors](../../src/amr/topology/AmrTree.h) 使用同一逻辑坐标查找同层、粗一层与细一层邻居。完整全局叶集合路径检查 owner、本地绑定、handle/epoch、face 2:1 覆盖和邻接互反；全部验证成功后才替换本地缓存。局部构造/回滚仍由同一实现处理尚未获得 UID 的本地记录，不另设 MPI 拓扑算法。全局叶集合本身的完整体积覆盖仍由原 registry 验证。自动分布式细化标记传播尚未实现，遇到远端邻居且需要传播时拒绝。

[GhostExchange](../../src/amr/exchange/GhostExchange.h) 的原粗细计划生成器可读取远端逻辑端点，并调用原有 coarse injection / fine average 盒坐标配方和 cell-plan 编译器；公式、权重、字段顺序及容差没有修改。本地同层复制计划只绑定本地存储。固定网格现有 MPI X 面执行器从同一邻接记录核对 peer 和 epoch，保留原来的打包次序、缓冲、tag、请求与完成顺序。A2 提交时远端粗细流体执行和 reflux 均在写入前拒绝；B1 的后续扩展见下节，应用 MPI-AMR 预检仍保持关闭。

[新测试](../../tests/host/amr/test_topology_neighbors.cpp) 在真实双 rank 中使用生产元数据收集、原 MemoryPool/AmrTree、原本地细化与迁移配方生成最小 mixed-level 网格；每个 rank 另构造不调用 MPI 的同拓扑串行参考。覆盖 1D/2D/3D × 周期/非周期六种几何，逐项对照邻居顺序、UID/epoch、owner、本地绑定、粗细操作及所有 cell-transfer 记录，并验证旧 epoch、2:1 违规、无效维度输入和失败后缓存保持。三个状态槽的六个原生字段与 species 均用于远端执行拒绝后的不变性检查。

本测试的细化仍是本地准备操作，之后共同发布身份并建立邻接；不是跨 rank 字段迁移或一致提交验收。夹具每 rank 池容量为 16，供 3D 父子共存使用，不能作为应用全局 max_blocks=16 的资源预算验收。局部容量耗尽、迁入预留和跨 rank 回滚仍由 C 阶段解决。

这版每 rank 保留 O(全局叶数) 身份元数据，并在刷新时暂存 O(全局叶数) 邻接图；本地每块缓存六个固定容量 face。当前 GCC 14 x86-64 构建中 BlockTopology 为 48 字节、FaceNeighbors 为 200 字节，每块六面由 144 增至 1,200 字节（增加 1,056 字节）；这不是各 rank 的 RSS 或流体内存峰值测量。没有据此宣称大规模目录或性能提升。CUDA 尚未编译或实机验收，本次也不改动设备执行路径。

### A2 验收记录

验收运行与原始证据保存在 /home/ubuntu/projects/ARCH-mpi-evidence-neighbors-20261103/final；上级目录的开发运行已标记为被最终修订替代。

- Release MPI ON/OFF 完整应用重新构建；两种 Release 配置共 31 项 CTest、两种 Debug 配置共 21 项相关 CTest 通过。Debug 本轮只重建相关测试目标，没有宣称完整 Debug 应用验收。
- 复用未改的积分脚本和冻结的独立串行基线，1D 27 组及 2D/3D 28 组共 55 组全局坐标重建后逐位一致；10 项预期拒绝场景通过。没有修改物理、公式、容差或验收阈值，没有新增 16-rank 性能结论。
- 工具测试 629 项通过、无跳过；架构审计、actionlint、CI inventory/结果完整性和文档链接检查通过。GitHub 新提交的 CI 仍须按 #26 当前 SHA 单独查看，不能沿用 A1 的成功状态。
- 两个原始归档全部 2,340 个文件逐一读回校验，并实际重开 HDF5；仅移除已验证的本轮 RAM 暂存。归档控制脚本曾误用缺少 h5py 的系统 Python，改用原 MPI 工具链 Python 续验，先前通过的实验没有因此重跑，失败与续验记录保留。
- 最终二进制内嵌 55708157/dirty 构建身份；source.json 保存二进制和实际参与构建的代码、测试及 CI 文件哈希，发布后绑定提交。文档在实验完成后补齐，不把预提交二进制标为新提交构建。
- 原邻居索引字段的替换覆盖原消费者；远端粗细流体执行、reflux、跨 rank 迁移/一致提交、CUDA、多节点及长程演化仍未验收，应用 MPI-AMR 预检继续拒绝。

| 归档 | 字节数 | 文件数 | SHA-256 |
| --- | ---: | ---: | --- |
| production-fixed_grid.tar.gz | 38046231 | 1549 | ca15b473358ac80ead6298dd4bbdd7c0420c03df28c7d03f1cb1da6acdce73f0 |
| production-multidim.tar.gz | 34174145 | 791 | fb8be3f03c536741b936ed3fe97f346091a97d47dcd8312ebde7446910a92554 |

## 本轮：B1 实际跨 rank 粗细 ghost 交换

[GhostExchange](../../src/amr/exchange/GhostExchange.h) 仍拥有交换的准备、通信与写入顺序。原本地粗细执行中的几何 lowering、gather 和 scatter 提取到 [CoarseFineHostExecution](../../src/amr/exchange/CoarseFineHostExecution.h)，本地和远端 source owner 共用。数学叶文件没有改动；gather 的算术主体忽略空白并将暂存类型改名后，与 d7bff58e 逐 token 相同，原运算和累加顺序保留。该文件是现有 Host 配方的具体执行实现，不是新的通信或资源管理层。

目的 rank 按现有计划发出源/目的逻辑端点、UID、epoch 和 face 请求；不传池索引。source owner 检查归属、互反邻接后用同一 face/cell 配方计算，只返回目标 ghost 单元的六个原生场和 species。保留原先“收集全部结果后统一写入”的原则：每个可失败的准备/分配/gather/接收解码阶段均先汇合失败状态，所有 rank 都准备成功才 scatter。MPI 调用错误中止 WORLD；普通准备错误共同退出，通信器可继续用于下一次有效调用。这不提供进程崩溃下的分布式原子恢复。

首版为 CPU Cartesian 集合算子，使用当前 WORLD communicator，要求所有参与 rank 有非空本地块，在初始化线程、Host worker 已结束后按同一顺序调用。头信息核对协议、维度、species、状态槽、epoch、块布局及根域；count/displacement 检查 INT_MAX 上限。没有粗细任务的 rank 仍参加阶段汇合。当前 Alltoall 请求计数和每次临时面结果暂存服务这个小案例，尚无大规模通信、RSS 峰值或性能结论；没有分配远端 FluidState，没有改原池容量策略。

[原生测试](../../tests/host/amr/test_coarse_fine_exchange.cpp) 使用与邻接测试共用的 [拓扑夹具](../../tests/fixtures/amr/partitioned_topology_fixture.h)。覆盖 1D/2D/3D、周期/非周期、非均匀和常量场、三个状态槽；本轮跨 rank 界面沿 X，不能据此宣称远端 Y/Z 面或完整棱角次序已验收。逐位比较全部字段和 species，并独立检查常量、所有非目标单元及池活动块数保持不变。空粗细工作也进入集合调用。单 rank 旧计划/handle、字段长度、无效密度、槽不一致、species 不一致和错误 owner 必须让所有 rank 在任何写入前拒绝，之后有效交换继续成功。此前“合法远端粗细执行必须拒绝”的旧结构断言改为“旧计划必须共同拒绝”，字段不变性和 reflux 拒绝检查均保留。

本轮没有打开 ExecuteExchange 的非根块同层 MPI 路径，也没有接入分布式 reflux 或 scheduler；因此这只是实际粗细面算子验收，**不等同完整 ghost 棱角同步、守恒时间推进或 MPI-AMR 应用支持**。固定根网格仍走原有 X 面通信，不额外调用空粗细集合操作。下一步 B2 才让远端细面贡献进入原 FluxRegister，并闭合同层/粗细交换与 reflux 的执行顺序；随后 C 验证细叶迁移、容量预留和一致提交，D 再按证据开放入口。

### B1 验收记录

最终证据在 /home/ubuntu/projects/ARCH-mpi-evidence-coarse-fine-20261103/final；上级开发运行已标记被最终版本替代。重复逻辑块的原拒绝检查在复查中补回，并增加空计划也必须拒绝重复逻辑块的测试；修正期间的编译失败日志保留，未计为通过。

- Release MPI ON/OFF 完整应用构建通过；Release 34 项、Debug 24 项，共 58 项原生测试通过。Debug 只重建相关测试目标。CUDA/KLU 均为 OFF。
- 未修改的生产积分脚本对照冻结串行基线：55 组按全局坐标重建后的逐位比较、10 项预期拒绝均通过。
- 629 项工具测试无跳过通过；架构、文档、actionlint、CI inventory/结果完整性检查通过。CI 已要求 MPI ON/OFF 必跑新粗细交换测试。A2 提交 d7bff58e 的 GitHub CI 已另行核实全部成功；B1 提交 cbdf0b73 的托管 CI 41545717485 于本轮核实成功；后续提交仍须独立查看。
- 两个原始归档的 2,340 个文件全部读回核对哈希，并实际重开 HDF5。仅清理验证后的本轮 RAM 暂存，保留此前证据及失败日志。
- 二进制仍带实际构建时的 d7bff58e/dirty 身份；source.json 记录二进制与构建使用的代码、测试、CI 文件哈希，native-builds.json 记录四配置测试程序哈希，发布时与最终提交逐文件核对。文档在测试后补齐，不伪称二进制已在发布提交后重建。
- 数学叶文件、物理定义、容差和验收阈值未改；没有新 AMR/GPU/多节点性能结论。完整 mixed-level 时间推进、reflux、迁移/一致提交、GPU 及输出/重启仍待验收。

| 归档 | 字节数 | 文件数 | SHA-256 |
| --- | ---: | ---: | --- |
| production-fixed_grid.tar.gz | 38094903 | 1549 | 94ba90f72bcbb54f834d1ba06641ad2da8646228aa12293b2d0b7b208119cfce |
| production-multidim.tar.gz | 34187846 | 791 | fa58e8ab94e53930e1cd95fe641fd0d11ca499792cf7d7fb0bf1d0007c7d57bd |


## 本轮：B2a 跨 rank 通量注册与 reflux 算子

[AmrFluxPlan](../../src/amr/flux/AmrFluxPlan.h) 继续拥有粗细面配方，在原计划中加入 owner 和预期远端细面贡献。远端 Grid 只含几何元数据：从 [Block::MakeLogicalGrid](../../src/amr/storage/Block.h) 复用原 InitGeometry 的构造过程，不创建远端 Block 或 FluidState。粗面索引和面配方接受 Grid，原面积、角动量矩、字段顺序和 registration_coefficient 保持原样。串行与分布式 source owner 共用 [GatherCoarseFineFluxValues](../../src/amr/flux/AMRFluxRegistering.h) 读取原生面缓冲。

在所有 Host worker 结束后的显式集合边界，原 [FluxRegister](../../src/amr/flux/FluxRegister.h) 验证该 rank 的全部 local routes，然后发送预期配方指纹、长度及原始 FP64 位模式。接收端从自己的粗细邻接独立生成预期细面配方，检查精确覆盖、指纹和长度；池索引不传输。粗块 owner 将本地与远端来源按原 source endpoint/axis 次序排序，再执行原注册循环。没有用浮点 MPI reduction 代替原来的加法顺序，也没有新增 MPI 注册器、分配器或时间推进系统。

注册、reflux 的可失败准备分别先在所有 rank 汇合；成功后才修改 arena 或流体字段。寄存器自身记录连续阶段编号和是否已 reflux，Clear、HostSnapshot 及 rollback 同时处理这些元数据。零权重阶段不写入通量、不制造 HasData，但仍推进阶段编号并绑定当前拓扑。reflux 核对调用者给出的阶段总数、dt、状态槽和新鲜拓扑，验证所有本地字段长度后执行原校正循环；重复阶段、漏报阶段数、重复 reflux、reflux 后继续注册及绕过集合边界的串行调用均拒绝。MPI 调用错误中止 WORLD；普通准备错误在所有 rank 退出且不写入，随后有效调用可继续。

[新测试](../../tests/host/amr/test_flux_reflux_exchange.cpp) 复用已有最小 mixed-level 拓扑夹具，覆盖 1D/2D/3D × 周期/非周期 × 平衡/常量跳变/非均匀面通量 × 三个状态槽，并覆盖六种无粗细工作的配置。每阶段权重为 0、1/2、-1/4、3/4；每次注册后逐位对照全部流体/species 通量，reflux 后逐位对照三槽所有字段和 species。常量界面跳变的质量、三分量动量、能量和两组分 rhoX 积分另用面积 1 的独立预算检查，使用已有 2e-12 守恒预算；平衡/空工作必须所有字段逐位不变。非目标槽、ENUC、ghost 和 fine 字段包含在全字段对照中，池活动块数保持不变。

失败测试覆盖单 rank 缺 route、短 payload、NaN、权重/编号不一致、旧 handle、双方合法局部配方却缺少一个接收面，以及错误槽、字段长度、重复调用和 rollback 后恢复。另以不分配大缓冲的数值边界测试检查 packet count 和 displacement 的 INT_MAX 上限、负计数及溢出拒绝。既有邻接测试中“合法远端 flux plan 必须拒绝”改为“未完成集合阶段的 reflux 必须拒绝”；原字段不变性断言保留。开发时新积分夹具误将 Ie/Je/Ke 的半开上界写为闭区间，导致读取越界；已修正新夹具循环，失败日志和诊断留在证据父目录，没有修改算法或放宽门槛。后续生产回归还发现旧固定网格的空 ApplyReflux 被误接入新阶段检查；现保留旧空操作入口，新增明确的 ApplyDistributedReflux 集合入口。全部六种空网格夹具都检查旧入口无副作用，mixed-level 绕过集合入口仍拒绝。该失败运行已归档逐文件读回校验，修正后重新验收。

这是 **CPU Cartesian 的集合算子和阶段提交边界**，使用 WORLD/FUNNELED，并要求非空本地块和一致调用次序。当前具体消费者是原生测试；生产 RegisterCoarseFineFluxes 对 distributed route 仍要求集合边界，应用 MPI-AMR 预检继续关闭。调用者的 gather/本地求解失败还须在完整 scheduler 中统一协调；EnergyReceipt 生命周期、非根块同层 MPI、棱角顺序和 accepted-step 尚未接入。本次跨 rank 界面仍沿 X，没有验收远端 Y/Z 面、多层/子循环、真实分布式流体演化、GPU、迁移、重启或性能。

当前按 peer 使用 Alltoallv，暂存每阶段 raw values、包和编译后的贡献，开销随该 rank 的界面配方条目增长。没有测量峰值 RSS 或证明大规模 AMR 扩展性；夹具仍是每 rank 16 个池槽，不代表应用全局容量预算。下一步 B2b 闭合非根同层/粗细交换与原 scheduler 的阶段完成顺序，再进入 C 的细叶迁移、局部预留和一致提交。

### B2a 验收记录

最终证据在 /home/ubuntu/projects/ARCH-mpi-evidence-reflux-20261104/final-fixed-grid。父目录保留开发阶段记录；final/ 和 final-checked-counts/ 已标记被后续修订替代。最终目录内也保留邻接测试未初始化寄存器组分时的失败记录；补齐原有 EnsureSpecies(2) 后，仍严格检查正确的拒绝原因和全部字段不变性。

- Release MPI ON/OFF 完整应用重新构建；Release 39 项、Debug 31 项，共 70 项原生测试通过，无跳过。Debug 只重建相关测试目标；CUDA/KLU 为 OFF。
- 未修改的生产积分脚本对照冻结独立串行基线：55 组全局坐标重建后的逐位比较与 10 项预期拒绝全部通过，包括曾暴露空 reflux 入口回归的 Sod 双进程案例。
- 629 项工具测试无跳过通过；架构、文档、actionlint、MPI CI inventory/结果完整性检查通过。新 flux/reflux 测试进入 MPI ON/OFF CI；新提交的托管 CI 必须按其 SHA 独立核对。
- 两个生产归档共 2,340 个文件逐一读回核对哈希，并实际重开 HDF5；仅清理已验证的本轮 RAM 暂存。此前失败的固定网格运行另外归档 62 个文件并读回核对，不覆盖旧实验。
- verify_arithmetic.py 对照 cbdf0b73，核对原逻辑几何、面配方、gather、注册写入与 reflux 写入的算术主体；忽略空白、改名和显式存储绑定提取后逐 token 相同。数学叶文件、公式、既有容差和验收阈值未改。
- 预提交二进制内嵌 cbdf0b73/dirty 身份；source.json 和 native-builds.json 记录实际源码及四配置程序哈希，发布时按文件绑定提交。文档在实验完成后补齐，不把这些二进制伪称为提交后重建。
- 完整 mixed-level 时间推进、远端 Y/Z 面、迁移与一致提交、GPU、分布式输出/重启、多节点和性能仍未验收。固定网格旧空 reflux 不增加 MPI 集合调用；新集合入口由后续 scheduler 明确接入。

| 归档 | 字节数 | 文件数 | SHA-256 |
| --- | ---: | ---: | --- |
| production-fixed_grid.tar.gz | 38084135 | 1549 | 37eb2a0fdcec9ca90cc0bc96b40d69e901887f6c1479cc019479bd5d6d19f041 |
| production-multidim.tar.gz | 34188710 | 791 | f7e7f5fde66ab846177ae690e931c4d351f855500572b9cd0d56a1898b9d0d20 |


## 本次审核确立的约束

- 高可维护性、可读性和职责清晰优先于维持旧内部接口。可以修改原有 AMR、拓扑、本地存储、MemoryPool 分配、交换计划、CUDA storage/residency 和执行边界。
- 不为了旧接口另建配置系统、影子块、MPI 分配器、拓扑管理器或时间推进器。远端拓扑描述不拥有远端 FluidState；发送/接收暂存只服务现有执行器。
- 每项基础设施调整先记录真实消费者、具体缺口、职责与资源影响，再实现；不预先搭建没有消费者的通用通信框架。
- 物理定义、数学公式、数值容差和既定验收阈值保持不变。若必须触及数学实现，先提交文件/符号、改动依据、数值影响和验证方案供审核，获准后实施。
- 可以替换约束过时内部结构的测试断言，但必须列出原因及替代验证。物理、守恒、字段、失败处理和资源边界检查继续保留，不能删除失败项或放宽门槛。
- 此前“AMR 最后”“不改内存池”的阶段性约束由本次审核更新：固定网格三步已完成，现在允许启动最小分布式 AMR 接入。完整 AMR、GPU 与分布式输出/重启仍按各自验收逐步开放。

## 源码审阅发现与修改归属

以下记录 e43183d0 的起点限制及职责归属；A1/A2/B1 的已实现范围见上节，其余仍为待实现设计：

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

这是完整演化与迁移案例的设计，尚未完成该案例的整体验收。A1/A2 与 B1 已由下述原生夹具分别验证元数据、邻接和粗细面交换；先闭合 CPU Cartesian 二维、两进程、一个粗细层差、同步时间推进，再扩大层数、源项或 CUDA。

1. 建立 2×1 个 level-0 根块。rank 0 持有左根块，rank 1 将右根块细化为四个 level-1 叶块，共五个活动叶块。先用原生块尺寸、全周期边界，检查内部与周期接缝两处粗细交界；两个方向可以连接同一 peer，不能依赖不同 peer 来消除消息歧义。
2. 复用已有 [重网格字段夹具](../../tests/fixtures/amr/regrid_migration_fixture.h) 与 [组分夹具](../../tests/fixtures/amr/amr_composition_test_cases.h)。传输测试覆盖带符号 ENUC 和 trace species；演化另用满足既有 EOS/可容许性条件的恒定态与周期平滑流，不能将传输标记场直接当成物理解。
3. 构造同一全局叶网格的单进程原生执行基线，再比较双进程 ghost、面贡献、reflux 后字段和 accepted state。预先固定细化/迁移事件，禁止测试期间两份运行自行选择不同网格后只比统计量。
4. 在完整 accepted step、reflux 完成且通信/后端租约均已结束的边界，将一个 fine leaf 从 rank 1 迁到 rank 0。这样必须真正支持非根块条带的所有权；搬运整条根块带不能充当该项通过。
5. 再将该叶迁回，让四个 siblings 在同一 owner 上执行既有 restriction/coarsening，恢复两个根块。首个单元不实现跨 owner siblings 的分布式 coarsening；未满足聚合条件时必须明确拒绝，不能遗漏远端 siblings。
6. 成功配置先采用全局 max_blocks=16，并记录实际每 rank 容量。额外构造局部容量不足与迁入预留不足的失败配置，不能因为全局空闲足够就假设每个 rank 都能成功分配。

实施顺序及完成条件：

| 顺序 | 实现与直接消费者 | 通过条件 |
| --- | --- | --- |
| A1：身份与所有权元数据（已完成） | 原 registry 与 DriverRuntime/DriverRegrid；真实两进程五叶元数据夹具 | UID、owner、epoch 规则一致，本地存储绑定隔离；唯一叶覆盖及旧绑定/候选检查通过。不是字段迁移验收。 |
| A2：远端邻接（已完成） | AmrTree、Block 邻居描述及原计划生成器 | 本地/远端端点并存，邻接互反、2:1 平衡、周期双向与旧 handle 检查通过。 |
| B1：粗细 ghost 字段交换（已完成） | GhostExchange、原粗细 cell 配方与 Host 执行 | 实际双 rank 结果与串行逐位一致；常量、trace species、三槽、空工作、单 rank 失败不写入与恢复；限算子面交换 |
| B2a：通量注册/reflux 集合算子（本轮） | 原 flux 配方、FluxRegister、AMRControl | 原始面值跨 rank、粗 owner 原次序累加、独立界面预算、全字段逐位对照、单 rank 失败不写入与阶段回滚 |
| B2b：完整交换/推进（下一步） | 原同层/粗细交换与 scheduler | 补齐非根块同层通信、棱角顺序、gather/求解失败协调及 accepted-step 对照，保留既有物理/守恒门槛 |
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
| MPI/scaling-16 / #26 | 继续承载扩大实验、当前设计及接下来的最小案例增量。它已经包含 #25；本轮在 A1 身份/所有权、A2 远端邻接和 B1 粗细交换之后增加 B2a 通量注册/reflux 算子，沿用 #26 并同步更新标题/说明。 |
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
