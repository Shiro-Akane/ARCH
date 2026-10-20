# ARCH现代化主计划

本文件保留现代化结案状态与证据；后续唯一推进表为[发布计划](ARCH_RELEASE_PLAN.md)，REL-2已完成，REL-3首批CPU及Hotspots修复证据保留，另补CPU耦合／内建NSE／四类短续算与实际Cellular／RZ归因，原科学出口仍未关闭；REL-4未启动；当前来源与成本见发布计划，不沿用本文件旧执行物身份。**R1–R8现代化已在采用范围最终签收（2026-10-14）**，最终模块闭环、来源、验收及后置见[R8签收证据](docs/development/modernization/R8FinalAcceptance-20261014.json)。阶段原始成果保留：[R1–R4报告](docs/development/modernization/R1R4Foundation-20261013.zh-CN.md)／[机器索引](docs/development/modernization/R1R4Foundation-20261013.json)、[R5构建](docs/development/modernization/R5BuildEfficiency-20261013.json)、[R6共享bounds](docs/development/modernization/R6SharedBounds-20261013.json)、[R67原语性能](docs/development/modernization/R67RzPerformance-20261013.json)、[R7生产集成](docs/development/modernization/R7CompletionAndR8Readiness-20261013.json)。不新增日期人读报告，不反写旧签收或失败。

## 当前状态

| 项目 | 结论 |
| --- | --- |
| 工作区／分支 | `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，`compute/optim`；恢复时核对[AGENTS](AGENTS.md)、Git状态和worktree |
| 基础阶段 | **R1–R4已按记录范围完成，R4.3四组新输入双后端签收通过** |
| 既有成果 | **R5已完成并采用最小RKL测试职责拆分**：原binary／CTest保持，完整覆盖和数值签收通过 |
| 最终状态 | **R8完成并最终签收；18类模块职责闭环，采用／保留／独立后置分别登记，无关联阻断** |
| 保留边界 | 原计算优化／发布科学出口独立，RZ／Cellular等未决不因基础阶段完成而关闭；托管CI未运行 |

## 目标与硬约束

得到更简单、可维护、高效且数值可靠的科学代码库；全仓检查不等于全仓修改，保留现状也是有效结果。

1. CPU/GPU核心数学、数值方法和物理模型保持共享权威实现；后端专属代码须有执行、内存、设备或实测性能理由。
2. 既有物理定义、数值行为、误差预算和性能非回归标准保持；不为速度、参考解差异或review建议放宽。
3. 保持[O7契约](docs/development/ConfigurationContractPlan.zh-CN.md)：活动关键缺项／显式错误拒绝，辅助默认、模型定义与显式auto来源可追溯，禁止静默回填。
4. **无需向后兼容，旧设施可直接退役**；同步当前消费者、构建、验证及文档，不保留无必要兼容层／双轨；不支持的输入／重启产物明确拒绝。
5. 文件安排按[实际所有者](docs/development/ImplementationOwnership.md#directory-and-file-placement)；冻结行为注释按[既有规范](docs/development/CommentAndDocumentationStyle.md#comments-in-behavior-frozen-source)，原始结果／锁定字节和历史身份保持。不按大小或文件数拆分。
6. 沿用`work`环境与已有资源护栏，重负载串行；实际WSL约7.7GiB、GPU8GiB，避免持续Swap。异常慢可受控截断并记未完成／待优化，不能记PASS或缩小原终点。
7. H5批次前、明显增长或I/O异常查实际宿主E盘，约余1GiB预警并停止新增大输出，不高频查盘；必要LFS表复用本地固定副本，不自动fetch／pull。
8. Codex负责设计、数值判断、唯一共享owner、独立验收和集成；使用已有项目及全局skill/MCP，不修改全局设施或另建治理框架。
9. 验证随真实风险调整，复用匹配源码／执行物／输入身份的旧证据；不为小改动重复完整回归。重大硬契约／数值标准变化仍须具体决定，普通已授权实现不重复请求批准。

## 阶段与出口

| 阶段 | 状态 | 当前出口／后续条件 |
| --- | --- | --- |
| 0 前置 | 完成 | 路径／环境恢复、旧进度核对、Studio集成及分支整理；[原记录](docs/development/archive/modernization/Preflight-20261012.zh-CN.md) |
| R1 全仓现状 | 完成 | 静态模块、调用、构建、第三方／生成／历史与文件归属索引，不是全路由运行证明 |
| R2 可复现基线 | 完成 | 原代表数值、运行／目标构建／RAM和O7范围齐备，完整工程冷构建等未测范围保持 |
| R3 审计与排序 | 完成 | 共享核心保留；输入／组门槛优先修复；RKL归因与未活动身份结论限定采用 |
| R4 验证保护与CI基础 | 完成 | 工具93项、静态配置46项和四组49对实际比较通过；最低必要Tooling发现沿用，托管CI未运行 |
| R5 构建／开发效率 | 完成，最小拆分已采用 | core测试局部重编译约省55.8%，完整集合峰值RAM降20.3%；保留全部覆盖，聚合构建未观察到恶化 |
| R6 核心渐进重构 | 已采用范围完成 | 共享bounds与浮点邻居的最小替换采用；其余审查区域保留现状，不要求全仓重构 |
| R7 运行优化与集成 | 已采用范围完成 | 独立原语收益保留；当前生产CPU短宏步／自身fresh、四CPU代表及真实CUDA owner通过；构建身份漂移已修复 |
| R8 最终收敛 | 完成／已最终签收 | 18职责、当前14补验／累计38独立CPU CTest、754Tooling、最终身份与独立review通过；原独立发布科学出口不转签 |

R5–R7可交错或跳过，但不能绕过对应风险的验证。每轮依实际授权范围推进，不将旧执行物或短测转签新源码、完整路由或长程出口。这里的最终阶段为R8；历史O8物理边界模块沿用其原归属，不另开同名整治阶段。

## RKL测试构建效率（R5）

先在隔离工作区评估，再按独立源码／数值／收益验收的精确字节采用。原`arch_cuda_diffusion_rkl_parity`及`diffusion_rkl_parity`保持；core保留主入口、冻结参考和统一失败计数，Tabular3D／4D失败与恢复检查移入单独CUDA单元，共用一个测试私有support头。CMake只增加源文件，数学／FP旗标／链接／重负载job pool及O7不变。

| 测量范围 | 原单文件 | 完整拆分后 | 判断 |
| --- | --- | --- | --- |
| 全部测试对象及ELF冷态 | 485.67秒 | 469.46秒 | 单次观测无明显总成本恶化，不宣称稳定冷构建提速 |
| core测试局部修改 | 同一原单TU完整动作，成本按上行等价推断 | **214.77秒实际重编译＋链接** | 约省55.8%；Tabular对象字节／hash／mtime保持 |
| 完整集合峰值单进程RAM | 约1477MiB | 约1177MiB | **降低20.3%，约300MiB**；三次构建均无护栏停止，采样Swap增长0 |

共享`DiffusionKernels.cuh`／`DiffFlux.h`或support头修改仍使两个对象重建，按已测完整集合的相同动作推断成本；没有再重复全编译，也不宣称全项目增量提速。计时统一使用GNU time，护栏的独立时间与RAM采样原样保留。测试ELF增加约8.88MiB、对象合计增加约8.93MiB，作为局部编辑与RAM收益的明确代价接受，不据此开启新一轮内核去重。

原基线、候选冷构建和增量重链的三个实际执行物版本均通过完整10入口；32原函数／类型、冻结数组和预算保持，**40条RKL＋8条算子误差记录逐行相同**。没有生产代码或配置变化，不重复70键O7负对照、R3归因或旧长程；托管CI未运行。执行物来自同旗标、固定库的本轮隔离目标图，源码与当前集成内容逐字节绑定，不能冒充整个项目的新CMake构建。

本包在此关闭，证据仅维护上述一个[R5机器索引](docs/development/modernization/R5BuildEfficiency-20261013.json)，不另建人读日期报告。后续只有真实编辑范围、构建身份或资源表现变化才重新评估；R6、RZ／Cellular科学出口均不自动启动。

## 共享物理限值检查（R6首包）

只领取明确重复的`CORE-R6-01`：`RzCellPolynomial.h`原私有`bounds_valid`与已经包含的`arch::state::valid_bounds`有相同的六项条件、类型、Host/device标注与短路顺序。删除副本，唯一调用原位改用共享函数，保留“caller bounds可与baseline不同，state::validate单独不能拒绝NaN”的注释。**只改一个现有生产头文件，净减12行，无新文件、依赖、抽象或兼容别名。**

原bounds默认值、非法值拒绝、六节点、54次减半、EOS与全部后续数学流程保持，O7及严格FP不变。独立源码／真实数值签收通过后才精确集成；证据集中在上述[R6机器索引](docs/development/modernization/R6SharedBounds-20261013.json)。

| 验收范围 | 实际结果 | 适用边界 |
| --- | --- | --- |
| 原／新完整Host几何叶测试 | 原native profile 7例、source EOS 4例及6类非法bounds保留；322行科学输出相同，两份新ELF的SHA相同 | 从真实CPU Release配方重建2TU，固定库依赖核实未触及改头，原GCC13／FP保持 |
| 原／新短Host／真实CUDA探针 | 4096组bounds：414合法、3682拒绝、105有效profile；每版本8192行位记录相同，非法bounds仍在1次中心读取后拒绝 | 运行时上传；只覆盖此次bounds及局部标量profile／六节点，不代签完整RZ路线 |

这是维护性收敛，不作运行或编译提速主张。原CUDA smoke未运行上述非法bounds检查，没有借它代签；没有新永久测试、全CUDA工程编译、旧长程、R3归因或R5成本重测。生产头已变化，未来生产应用验证需重新匹配源码／执行物；本轮签收的是新构建叶测试和短GPU探针。

较大的轴标签映射、三条Dispatch启动链及有合法语义差异的适配层保留／后置，不顺带归并。**首包在上述范围关闭；后续重构由下节联合包的实测热点驱动。**

## RZ共享原语与运行成本（R6／R7联合首包）

本轮采用最小R6／R7联合包：仅在FiniteRingBoundaryMath.h中将普通binary64内域的nextafter改为等价位步进；特殊值与边界仍走原函数。Gauss2／Gauss3交集、AGM、误差门槛、细分顺序、65536 box上限、全局工作预算、memo生命周期及O7均保持不变。旧quartet测试将origin=0.3直接判为不合格，与实际存储矩形契约不符；修正测试并加入共享边移动1 ULP必须拒绝的负对照，不修改生产资格。

16×16派生非均匀源使用原生产预算函数，每次进程只执行4个环边界调用；ABBA两对的环边界合计均值7.694754秒→4.259244秒，减少44.6474%。GNU time整进程均值7.695秒→4.765秒，减少38.0767%；候选整进程范围为4.26–5.27秒，保留观测波动。两侧平均RSS均为11796 KiB。另组4×4固定预算合计均值18.586686秒→8.709307秒，减少53.1422%；两组不混合平均。

原／新科学输出、当前预算、身份与工作计数逐行一致。72764对语料在四种Host舍入模式和真实CUDA上各291056项比较均无差异；最终11组Host数学入口及原9lane CUDA raw owner均通过。scratch初始化仅占0.2931%，停止该优化假设。该首包仅签收共享原语、数学owner与短环边界成本；后续生产短测见下节，原RZ五声穿越／长程自身fresh与其他发布科学出口仍独立保留。

证据统一见[RZ机器索引](docs/development/modernization/R67RzPerformance-20261013.json)，含原／新源码与执行物、原CMake配方、独立签收、失败fixture修正和复现边界；本地原始产物留在`output/modernization/r67-rz-20261013/`，不新增日期人读报告。

## 当前源码集成验收（R7收尾）

R7批次于当前工作区重配构建 CPU 生产 ARCH，未沿用旧主目录 ELF 签收新源码。24 项当前 CTest 代表入口通过；Cellular 检查首次因缺少参考辅助程序失败，补建重验通过并保留首次失败记录。四个 CPU 应用均达原物理终点，30 个 Data 数据集逐位相同、69 个旧数值指标精确一致，修复账本为零。单一分辨率结果不代签收敛组或 CUDA 应用。

R7新增 RZ 短算例为 16×16、256 单元、RK2、自引力孤立边界，终点 700 秒；gravity_rtol=1e-8 是本次显式选择的新短输入，不可冒用旧长程身份。四次对照均实际到达 2 个宏步与 8 次重力求解，原残差门槛与非计时诊断全数保持。按实际 RZ 全环体积计算的最大质量漂移为 8.5446e-19，满足原 1e-12 门槛；原／新 Plot 物理与原生网格数据逐位相同。新 ELF 从自身第 1 步 checkpoint（398.19867758046195 秒）续算至 700 秒，终态原生 checkpoint 的 24 个 dataset、33 个 attribute、2104 个元素严格相同，未排除任何节点。Plot 的 SourceIdentity 差异单独保留；原生 checkpoint 无构建身份拒绝字段，不可推断其实现了该门槛。

R7串行 BAAB 两对观测中，原／新整应用均值为 19.57→12.805 秒（减少 34.57%），样本范围为原 18.71–20.43 秒、新 11.65–13.96 秒。因原／新 Studio 构建选项及路径不同，此仅为生产短算例观测，不作孤立归因、稳定全应用或 GPU 提速主张；R67 首包独立 direct owner 收益仍单列。当前源码原 CUDA reduction／gravity owner 真实通过，保留 raw／typed 预算、全局顺序与 join，未新增内核并行或删除同步。

R7另修复已复现的构建身份问题：自动生成的 BuildIdentity.generated.h 曾作为自身输入导致无源码变化时身份漂移。仅在原 cmake/project/BuildIdentity.cmake 排除该派生产物，保留真实源码与生成网络头；新回归在旧模块失败，修复后 41 项 provenance 测试通过，三次实际无变化配置身份相同，后续构建无动作且 ELF 哈希与时间戳保持。身份版本、字段、严格比较、O7 及数学标准未变。此修复改善增量构建与 Plot 来源校验，不新增 checkpoint 构建身份规则。

当前CPU应用的首次183个构建动作耗时281.52秒，峰值单进程约2226MiB、owned进程树约4423MiB；j2最低可用约1559MiB，未增长Swap或触发护栏，后续构建改用j1。上述是当前CPU应用目标及其依赖，不是全项目冷构建。CUDA只构建原gravity／reduction目标的两个NVCC单元及必要依赖，耗时134.91秒、峰值单进程约1161MiB；原实际GPU测试1.70秒通过，不重做R5构建归因。详细编译配方、失败、资源与执行物身份均在[收尾证据](docs/development/modernization/R7CompletionAndR8Readiness-20261013.json)。

## 全仓模块闭环（R8）

R8按真实目录／文件所有者展开18类职责，全部所有者路径已核验。每项分别采用、保留或独立后置；没有因“全模块”而强行修改未受影响代码，也不以未测热点假设扩范围。源码、第三方／派生代码、生成物、运行数据与历史区域沿用R1归属；冻结注释沿用职责／数学／生命周期规范。

| ID／职责 | 当前验收或匹配复用 | 最终取舍 |
| --- | --- | --- |
| M01 配置、problem注册、dispatch | 当前configuration_input／resolved_execution_plan；原O7关键拒绝／25辅助来源与当前API | 保留严格输入契约，无关键缺项或显错回填；A04未活动身份保持 |
| M02 Driver调度、data、事务、后端 | 当前runtime_probe／compute_backend／默认gravity-stage，R7scheduler／真实宏步与fresh | 保留既有状态／源／事务所有者，不增加调度框架 |
| M03 Hydro、重构、通量、物理限值 | R6同序bounds与真实Host/GPU探针；当前PPM／Sod原终点及limiter／flux代表 | 采用bounds去重；共享核心保留，不转签新收敛组 |
| M04 Grid、度量、常数／物种／诊断 | 当前curvilinear_metrics、原生RZ体积质量与字段；未改物理常数按原证据保留 | 不改坐标、面积／体积、CFL或物理定义 |
| M05 AMR拓扑／ghost／transfer／reflux | 当前exchange／topology／flux-surface代表，未改Host混层／迁移／fresh原证据 | 保留；uniform-root短RZ不代签混层Device路线 |
| M06 EOS、表源／closure、device owner | 当前tabular_strict；R7 Helm／NativeTabular与R5 Tabular失败恢复 | 保留单一EOS权威及原文件安排，不重审全部表域 |
| M07 Burn、ODE continuation、线性代数 | 当前sparse_ode_continuation／KLU161，未改强反应与能量耦合原证据 | 保留；不重复41秒参考轨迹或新增ODE优化 |
| M08 网络／NSE／生成适配器 | R7 generated_nse；当前生成工具测试与真实registry身份控制 | 保留built-in／可选包分工，不主动生成或下载全部网络 |
| M09 Diffusion／RKL／应力与测试职责 | R5原10入口／40RKL＋8算子；当前CPU原终点与真实CUDA reduction消费者 | 采用两TU最小拆分及其ELF代价；生产Diffusion不再拆分 |
| M10 Gravity／Poisson／MG／RZ | 当前默认gravity-stage、composite contract及quartet；R67数学／位oracle、R7真实CPU短fresh与CUDA owner | 采用共享邻居优化；预算／顺序／memo保持；composite ci未重跑 |
| M11 物理边界／transport／用户callback | R7 boundary_plan与当前默认gravity-stage真实边界／事务 | 保留原O8模块归属、stage-time／axis／内部接口约束 |
| M12 HDF5／Plot／原生Checkpoint | 当前plotfile_publication故障注入；R7严格自身fresh及真实来源 | 保留严格发布／兼容；Plot身份与CHK物理控制不同，不新增build-id gate |
| M13 Core API／inspection／session／资源 | 当前inspection／session／verified-file／exact-sample cache；R7配置／preview | 保留真实Core协议；检查接口不等于完整运行资格 |
| M14 Studio host／renderer／desktop | 整树与采用的双亲merge相同；385测试及lint／build日志按原身份复用 | 无UI漂移，不重做GUI；人工WSLg仍未签 |
| M15 构建／依赖／严格FP／身份 | 当前CMake真实来源、网络变更／恢复控制、最终无变化配置／无动作构建，两ELF保持 | 采用自输入修复；CPU/GPU编译库不混用，无完整CUDA重编 |
| M16 runner／oracle／资源护栏 | 当前完整754Tooling无skip、架构审计；完整组拒绝／来源／失败元数据 | 采用R4组门槛和原护栏，保留原失败与有效replay，不造第二套验证设施 |
| M17 CI／固定数据／许可证 | 工作流同已验R31版本；四离线控制、三EOS＋SodH5指针／副本SHA及大小匹配 | 本地下载0；托管CI／actionlint未执行，不将离线验证转签平台服务 |
| M18 文档／档案／布局／技能 | 当前三入口、目录职责及来源索引；12旧机器记录保留 | 修正R4历史／当前路径混淆；仅一主计划＋本次机器索引，不改全局设施 |

## R8最终签收

R8已完成18类实际职责的闭环核对：各模块分别登记采用、保留或独立后置，已有优化均与当前源码、构建入口及消费者绑定。本轮未新增生产数学或算法改动；O7关键缺项／显式错误拒绝、CPU/GPU共享数学、原数值门槛和严格FP均予保持。R5局部构建与RAM收益、R6去重、R67共享原语及R7构建身份修复均已落地，继续保留其已签代价和适用范围。

本轮补建12个当前CPU目标，42个动作共用时157.46秒；新增14个原CTest入口全部通过，CTest报告43.02秒，其中gravity_stage_contract为32.81秒。与R7匹配证据合计38个独立CPU CTest通过，76个注册项并未全跑。当前完整工具套件754项通过、无跳过，架构审计通过；真实CUDA owner、四CPU数值终点、RZ短宏步及严格自身fresh按未改身份复用。真实网络输入变更／恢复会改变／恢复构建身份，自身派生产物不会造成漂移；最终无变化配置后构建无动作，CPU/CUDA执行物身份保持。

本轮所有构建与运行均正常完成，未触发内存、Swap、压力或超时护栏，无截断结果被记为通过。Studio整树、固定LFS资产和CI工作流与已核版本一致，修正了旧R4复现路径与当前入口的混淆。最终状态为R1–R8现代化在已采用范围内签收；原RZ五声穿越／长程fresh、完整Device发布科学、Cellular科学差异、托管CI／actionlint与人工平台验收仍分别保持未签，不转写为本轮通过。

| 签收项 | 结论与证据 |
| --- | --- |
| 实际改动及所有者 | 采用包均已落地；R8新增验证／文档收尾，没有新增生产数学或算法；[最终机器索引](docs/development/modernization/R8FinalAcceptance-20261014.json)绑定具体文件／调用／构建归属 |
| 数值与接口 | R7有效源／执行物／输入身份复用，R8必要补验实际通过；原标准、O7、strict FP及完整RKL覆盖保持 |
| 成本与停止 | R5局部编译／RAM和明确ELF代价、R67独立收益、R7整合观测分开；本轮正常完成，无截断PASS，不继续无证据优化 |
| 来源与资料 | 两执行物和构建身份最终保持；12旧JSON原字节保留，原始失败／返修完整；独立源码／模块／运行签收及最终一致性QA通过 |

C04／A03、CORE-R6-01、P-RZ-01、P-RZ-02与身份自输入修复在相应范围关闭；C01／C06／C08保留。C05／A05仅在出现具体阅读障碍时重开。历史分类见[基础阶段取舍](docs/development/modernization/R1R4Foundation-20261013.zh-CN.md#后续取舍哪些推进哪些保留)。

## 后续独立工作与分工

以下为独立需求或原发布科学出口，不是R8未完成项，也未被本轮豁免／记PASS；没有自动启动新阶段。

| 条件工作包 | Codex | W1 | W2 | W3 | 启动与停止 |
| --- | --- | --- | --- | --- | --- |
| P-RZ-03 Device成本 | 真实应用需要时定独立范围 | 先测单lane、上传、join实际占比 | 原Device消费者与CPU科学对照 | 核全局预算／顺序、借用与失败join | 无明确收益保留，不直接并行faces／删同步／外推GPU提速 |
| A04／C02／C03／C07科学或身份 | 单独冻结真实需求及原终点／标准 | 原owner定位，不恢复旧设施 | 原长程／耦合／Cellular／fresh资格，慢可受控截断 | 复核原门槛与完整审计，不转签短测 | 旧未完成保持未完成；身份变化仍需明确版本／契约决定 |
| 平台／发布验收 | 单独安排第二平台／托管执行 | 原CI与离线缓存保持 | clean runner／完整Device应用和public fresh | 人工GUI／平台UAT与来源核对 | actionlint、托管CI、人工WSLg及完整发布科学须有真实执行后才能签收 |

## 验收、资料与接续

- 每包明确问题证据、唯一写入范围、依赖、原标准、验证和回退；worker交付须独立验收，非阻断建议不扩范围。
- R4批次的旧build源与当时harness／input路径差异已真实处理，复现以[机器记录](docs/development/modernization/R1R4Foundation-20261013.json)为准，不照搬失效的旧`--source-root`模板。后续生产代码变化重新匹配执行物和来源。
- 旧日期人读统一在[现代化档案](docs/development/archive/README.md#modernization-checkpoints)；旧机器JSON保持原路径／字节，旧人读路径通过当前索引的迁移表解释，不反写失败或历史签收。
- 后续主要更新本文件及实际证据入口；完成后再归档失效说明，不创建多份当前进度、重复规则或兼容文档。
- **R1–R8现代化在采用范围最终签收，本轮到此结束。** 后续按上述独立需求接续；旧长程、完整Device应用与平台／CI科学出口保留真实未签状态，不扩大本轮结论。
