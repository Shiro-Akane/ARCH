# ARCH 发布推进计划

本文件是唯一当前推进表。现代化和二维柱坐标重植按各自原范围结案；过程记录集中在[历史档案](docs/histroy/README.md)，原始科学收据不改签。当前任务是可靠发布、清晰文档与可维护的验证流程，不另开全仓架构重构。

**当前：REL-6候选验收已闭合；REL-7 正在准备 v1.3.0 正式源码发布。** 候选生产源码为`7e1dca167bd5`，CPU／CUDA干净构建、安装迁移、必要科学资格与实际Studio链路已签收。详见[候选验收收据](validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json)；本机恢复`output/release/rel6-20261020/progress.json`。工作区`/home/shiroakane/.codex/worktrees/d7a6/ARCH`，分支`compute/optim`。构建产物与随后记录提交分开记身份；托管出口只认[PR当前提交的检查](https://github.com/Shiro-Akane/ARCH/pull/12/checks)。

## 发布目标与判定

正式版本应保证：**当前公开声明的全部科目，在明确适用的模型、组合、后端与平台上正常完成，没有已知未解决的阻断性缺陷，满足既有数值标准。** 未知输入不是无条件承诺；公共功能不能为赶进度自动改标experimental或移出范围。

- 保持CPU/GPU共享数学、物理定义、严格浮点、原误差预算和性能非回归。ENUC原门为`5e-9`＋`5e-12`；角动量、质量／电荷及各科目守恒沿其原`1e-12`标准，不相互替代。
- 保持[严格配置契约](docs/development/ConfigurationContractPlan.zh-CN.md)：活动关键键缺失／显错必须拒绝；辅助默认、具名模型及auto来源可追溯。未活动显式值仍为input，省略且无默认才为null；`SimConfig::Get`第二参数不是恢复默认值的授权。
- 几何与可选引力解耦。Grid／公共几何负责布局、测度、边界正则性和几何源；引力负责场、求解、边界、做功及资源发布。关闭引力零求解／专属数值数组；未知活动路由拒绝。不恢复旧RZ专用文件、状态、AGM、memo、savepoint或GlobalDefs字段。
- 旧接口、配置、脚本和重复设施无需兼容层；同步当前消费者并明确拒绝不支持的输入／产物。原生CHK不新增build/source/profile门，Plot和新chart按原身份契约校验。
- 当前支持范围为既定Linux／WSL2和选配Studio；Windows原生、一般辐射输运或未验GPU平台不新增承诺。既有近原点限制不能掩盖非法状态、配置误读、守恒或重启缺陷。
- 冻结JSON、科学结果、失败记录与历史源码保持原路径和字节；人类历史只在有备份、消费者和路径映射的范围整理。截断、skip、空选择和未执行不写成PASS。
- 阻断包括正确性／数据完整性缺陷、公开路径不能完成、必要证据缺失、静默回退及说明无法实际使用。非阻断性能建议和文字润色不无限延长发布。

## 阶段、分工与出口

W0为Codex，负责标准、唯一集成和独立验收；W1核科学／共享契约，W2核测试／构建／设备／CI，W3核文档／注释／Studio说明。职责槽不要求始终并行；同文件只设一个写入者。真实DPS与Gemini沿既有全局MCP有界委派，执行成功不代替验收。

| 阶段 | 任务与方法 | 分工 | 出口／当前状态 |
| --- | --- | --- | --- |
| REL-1 现状与覆盖 | 核模型、功能路由、输入、文档、测试与旧证据身份 | W0定界；W1科学；W2构建；W3文档 | 已完成概览，不代签全部科学能力 |
| REL-2 最小复现 | 修13份活动输入和消费者后冻结；复现CPU短例、轻图编译与并行成本 | W1标准；W2执行；W3说明；W0验收 | 已完成本包，原标准保持 |
| REL-3 CPU科学 | 补公共模型、新chart、显式中心输入及受影响fresh；复用适用终点 | W1／W2执行；W3归一；W0验收 | 已按声明CPU范围闭合；下方限定资格继续有效 |
| REL-4 CUDA集成 | 核真实设备索引、资源、stream、AMR／fresh及公共集成；定点修复原ENUC问题 | W2设备；W1数学；W3说明；W0验收 | 已按本机单GPU原范围闭合，不代表最终分发包 |
| REL-5 文档与验证收敛 | 合并主题、归档历史、补源码可读性；按独有断言收拢Tooling／Studio／CI | W3文档；W2测试CI；W1常量依据；W0验收 | 已闭合：本地整理、归档和完整真实托管验收通过 |
| REL-6 候选版本验收 | 冻结候选，干净配置／所需构建／安装；从实际包核CPU／CUDA、科学资格、成本与平台 | W2产物；W1科学；W3实际说明；W0最终review | 已闭合：实际候选与适用旧证据联合准入，原标准保持；SNIa联合证据经用户授权 |
| REL-7 发布与结案 | 整理支持矩阵、变更、限制、资产许可及复现入口；准备合入、标签和发行物 | W0协调；W1／W2／W3确认出口 | 进行中：用户已授权本轮合并 PR、创建 v1.3.0 标签并正式发布；完成后以远端状态和结案记录签收 |

## 候选验收结论

2026-10-20从无Git目录的源码归档完成CPU／CUDA候选验收；1117份冻结源码／测试／配置与25份实际生成网络文件在末尾复核一致。定点修复只涉及公共柱坐标／rphi压力平衡、Runtime安装和桌面启动／关闭；完整面通量、几何／引力分工和原门槛保持。各科目按受影响范围补验，原长程证据按实际归属复用。

| 项目 | 已签收 | 范围与保留事项 |
| --- | --- | --- |
| 共享压力平衡 | 原高压单步输入与rphi对照在CPU／CUDA上径向动量均精确为零，原`2e-8`＋`1e-12`及ENUC门通过；四种受影响Ideal chart通过 | 复用stage压力，相关Ideal CUDA缓存增量16字节／存储单元；原NOT_MET记录保留 |
| 旋转／AMR／扩散 | 非零旋转、三通道Ideal RKL1／2、Helm反应／热扩散及AMR对照通过；四组自身fresh差异为零；边界力矩平衡残差最大`7.31e-17` | 原角动量`1e-12`计入真实边界扭矩；Helm该算例实际仅热通道非零，不冒称三通道均活动 |
| 生成网络及安装 | `custom:nse_alpha`九平衡点／三ODE及实际20次运行、10组CPU／GPU比较通过；NSE启停差异及源能／电荷闭合通过；两套Runtime各14文件迁移后实际运行 | 三ODE完整轨迹使用CUDA包的CPU／GPU模式；独立CPU包另验Helm短跑。仅安装RUNPATH变更，执行物摘要分别保留；系统依赖仍需具备 |
| Studio | 388项零跳过、lint、typed build及实际包Configure／Build→Preview／AMR→Run→Restart→Plot／Inspector全部通过；10→104步续算12组物理／网格数据逐值一致 | 实际Linux／WSLg平台；缺少显式续算配置会拒绝；正常及受控异常关闭已核验；不代签Windows原生 |
| SNIa联合资格 | 当前安装CPU包20步零修补、严格fresh差异零；另两步观察到4个接受热扩散half。原主终点／守恒／fresh与获准L2重复重网格机制联合准入 | 用户明确授权联合证据；原主算例重网格FAIL及thermal GAP保留，不把短跑或L2改签为原主终点 |
| 全模型／解析终点 | 16模型逐项列出继承与新验范围；补热点单布局4.1ns CPU／GPU终点，以及原生PPM／RKL解析终点和原收敛组 | PPM／RKL2末对阶约4／2，原门2.7／1.8；热点功能／续算不等同独立点火机制证明；不声明全方法交叉组合 |

2026-10-20用户明确授权上述SNIa联合证据准入；这不把原主算例的一次重网格改签为≥2次，也不降低原终点或数值门槛。

本机关闭编译缓存后，CPU干净应用GNU墙钟172.62秒，`main.cpp`字节不变的代表重编译34.67秒、纯重链26.86秒。CUDA首次达到3600秒护栏上限受控截断（退出124），保持原图／源码／参数续建686.63秒后完整成功；GNU分别3689／704秒，与护栏时钟差异保留，不拼出虚假连续样本。续建与27秒、约94MiB的CPU诊断重叠，不能作为隔离cold提速依据。CUDA代表重编译／纯重链外层墙钟42.24／30.56秒；所有样本无swap增长。完整CUDA编译吞吐留独立优化项，不在本轮边改边测。

匹配工作量下，原生二维柱坐标／rphi成本比：CPU约1.06–1.10，GPU约1.01–1.07，原2倍报警线和CPU绝对锚点均通过；小规模方差不支持稳定提速声明。实际包、数值、资源和托管原始成本集中在收据索引与本机`cost-ledger.json`。

候选源码`7e1dca167bd5`的真实PR／push CI、汇总门与CodeQL均成功，CodeQL无新增告警；同步记录提交后仍核对PR当前head。实际平台范围为Linux／WSL2、单张RTX3060Ti／sm86及可选Linux Studio；此为候选验收窗口，当时未合入、打标签或发布。后续正式发布见本轮记录。

## 正式发布准备

2026-10-20 用户授权完成 REL-7：合并 `compute/optim` 指向 `main` 的 PR#12，版本号 `v1.3.0`，沿历史 GitHub 源码发布模式；不附预编译科学库或原始 H5。中英文说明按 `docs/releases/` 的既有格式，重点为可选 Studio GUI、共享架构整理与关键配置静默回退的移除；允许默认值／auto 的来源、平台和科学限定仍如实说明。

当前分工为 DPS 核发布声明与原收据的一致性、Gemini 核冻结双语文案、Codex 负责独立验收与 Git／发行操作。远端 main 的预合并树已确认未额外改变生产或 Studio 实现；不为文档发布重跑既有长程、CUDA 全编译或旧 RZ 验证。Windows 原生工具入口不扩大 ARCH 的计算平台声明。 `main` 已确认受保护，保持正常 PR 合并，不绕过规则或直接强推。托管 CI 补 main 与显式调度入口；本次一次性发布 workflow 等当前 head 的 PR／push CI 和完整 CodeQL 均通过后，以正常 API 合并 PR，完全遵守保护规则。main 全部文件树必须与该扫描 head 完全相同，再显式调度 main 的完整 CI；通过后生成带注释的 `v1.3.0` 标签与源码 Release。CodeQL 按相同完整文件树明确继承，不冒称新增扫描；候选源码／收据及原数值标准保持。

顺序为：文案／链接与源码身份核对 → 正常提交推送 → PR 当前 head 的适用 CI／CodeQL → 合并 main → 核合并提交的适用检查 → v1.3.0 标签与 GitHub Release → 验证公开状态并回写结案。源代码标签、候选执行物与发布后记录分别保留身份。临时恢复与双模型产物归 `output/release/rel7-20261020/`；既有科学收据不重写。中英文说明见[发布索引](docs/releases/README.md)，托管操作与最终结案统一记入[发布记录](validation/backend/results/release-readiness/REL7ReleaseRecord-20261020.json)，当前尚未标记为已发布。

## REL-5整理与验收

| 工作包 | 已完成内容 | 证据／边界 |
| --- | --- | --- |
| 测试职责 | 八个Python wrapper合为一个`tooling_contracts`；CPU75→68、CUDA142→135，其余注册命令／属性逐项相同 | 完整Tooling619项通过；原593个ID、9个迁移稀疏方法保留，新增17项控制。注册数不是本轮数值通过数 |
| Studio | 完整383项零跳过、lint和typed build通过；136文件生成JS AST等价，20文件排版，Host／桌面／浏览器注释补齐 | `npm test`唯一持有完整选择、TAP与文件并发1；build包含类型检查，Host子集不在CI重复跑 |
| 源码说明 | 593份C++／CUDA及inc建立归属索引；20份定点补注释，非注释token相同；另显式复核7个模型的CGS单位印记，补3个无量纲参数 | 保留既有Workflow；解释数学、常量类别、单位和生命周期，单位元数据仍按实际源码SHA严格绑定；不编造经验参数最优性，不冒称全算法重新证明 |
| 文档归档 | 按已确认清单迁移323份记录至`docs/histroy/`，合并三个冗余索引；当前主题和历史入口分开 | 148个固定路径排除；157份冻结历史、3475份入场结果、原协议及原执行物保持。详细映射和SHA见整理收据 |
| CI与资产 | 保守判定适用范围；Helm表和Studio14KiB夹具统一定向缓存、离线物化及大小／SHA校验 | 原Helm缓存身份保持，本机零LFS下载；actionlint和本地控制通过；真实托管资产准备、完整Tooling／CPU／Studio及汇总门通过；具体命中率与RAM未采集 |

本地GNU墙钟：CPU／CUDA重新配置4.78／6.46秒，归档后Tooling CTest31.90秒、Studio测试47.92秒、lint9.47秒、typed build5.44秒。Studio三项护栏树峰为383032／1077920／726560KiB，无swap增长、无截断。最终真实托管步骤墙钟：CPU构建371秒、完整CTest117秒、低密度矩阵7秒，Tooling21秒、Studio测试55秒、lint＋typed build18秒，全部通过；这是API时间戳，GNU／RAM及缓存命中未知。此前失败、定点修正和逐项成本保存在收据及本机`cost-ledger.json`；缺少同条件完整前后样本，不宣称全CI提速。

原四个工具失败仅在内存测试夹具中补第7步采样与当前输入SHA，真实资格器、科学清单及冻结协议未改。启动前输出预留拒绝、观察器纠正和原失败保留。真实托管暴露的退役旧断言、验证器旧chart／2π口径、Preview非法输入和单位印记已定点修正；源项／求解器数学及原门不变。Studio显式准备CMake／Ninja／C++／mold；真实CPU完整库存和低密度矩阵已通过，Studio夹具保留全部身份断言，将1秒等待改为有限5秒，本地完整383项通过。纯Node测试改动仅触发Studio／Tooling，混合及未知路径仍保守全跑。7个失效单位印记经源码单位复核后手动更新，源码SHA等式与全部current断言保留；旧CHK／Plot拒绝扩充。独立输出的受影响CPU应用与3测试目标共79动作，GNU237.16秒、采样树峰2.59GiB、无swap增长；4个既有CTest19.59秒及原3／6步边界／AMR／JENS fresh通过。不编译CUDA、不重跑长程；旧ELF原位保留，新应用有独立身份。Vite现存大chunk提示只留后续成本候选。

2026-10-20 PR#12定点补充：三处独立参考乘法在首个操作数显式提升为`long double`；Host Hydro复用FluidState的长度校验，保留现有`int`索引范围，负数／溢出／布局不匹配在分配和输出前拒绝。原门槛、O7和CodeQL规则不变。既有CPU曲线坐标／Poisson两项及真实CUDA曲线坐标smoke均通过，零跳过；两组编译19.27／44.27秒，测试4.01／1.21秒，无swap增长。明细在整理收据的`post_closure_followups`，最新托管结果以[PR当前检查](https://github.com/Shiro-Akane/ARCH/pull/12/checks)对应的提交为准；该定点补充当时未启动REL-6，后续候选验收见上节。

## 已完成证据与限定资格

| 入口 | 适用范围 |
| --- | --- |
| [候选验收](validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json) | 冻结生产源码7e1d、实际CPU／CUDA安装包、生成网络／受影响科学资格、16模型继承表、Studio实际链路和成本 |
| [发布概览](validation/backend/results/release-readiness/REL1Overview-20261014.json) | 原库存、输入及文档／源码快照；不反写后续修复 |
| [当前CPU签收](validation/backend/results/release-readiness/REL3CurrentCpuClosure-20261019.json) | 16模型、受影响初始化、显式中心输入、原终点及自身fresh的声明范围；REL-2及原CPU依据由收据追溯 |
| [当前CUDA签收](validation/backend/results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json) | 本机17设备项、壁面／stream／AMR、公共模型、NSE／RKL、原私有Cellular24步／25CHK及两后端fresh，原ENUC门保持 |
| [柱坐标综合准入](validation/backend/results/release-readiness/Cyl6ComprehensiveAdmission-20261019.json) | CYL-0至6原方法／EOS及必要耦合；七份收据在[共同数学owner](src/numerics/curvilinear/README.md#axisymmetric-cylindrical-contract)逐项索引 |
| [旧实现退役](validation/backend/results/release-readiness/RzRetirementAcceptance-20261018.json) | 原退役与历史身份，不给旧RZ长程转签新实现 |
| [现代化结案](ARCH_MODERNIZATION_PLAN.md)／[此前完整发布计划](docs/histroy/release/QualificationThroughCuda-20261019.md) | 保存已完成过程、原测量、来源及未决项；当前行动只以本表为准 |

**历史限定与当前处置：**原高压近零动量`NOT_MET`保留，REL-6已用修复后的实际候选、原输入及原门通过，并非缩放正例代签。SNIa原main重复AMR `FAIL`／thermal `GAP`继续保留；当前准入采用用户明确授权的原主终点／守恒／fresh与L2机制联合证据。详见候选收据，不能回写旧收据为PASS。

保留CPU／CUDA应用SHA为`fe09bf2e251a…`／`b1143d8aa6a2…`，Host比较器`13e91fba6e38…`；完整身份查原收据。原409／515项源码匹配和无动作构建只属于REL-4冻结窗口。原CPU／CUDA增量构建72.11／3040.45秒，CUDA52动作的共享NSE头扇出／ptxas成本留后续冻结评估，不在整理窗口边改边测。

## 二维柱坐标：已完成的公共实现与契约

沿已有Grid与三维柱坐标`(r,z,phi)`作轴对称约化，保留三动量和旋转；公式、轴序、V/A/W/T、轴线奇偶、全环积分、可选引力及chart身份统一见[公共曲线坐标契约](src/numerics/curvilinear/README.md#axisymmetric-cylindrical-contract)。不再维护平行RZ实现或阶段说明；REL-3.5已退役。

模型从Grid的`(r,0,z)`物理点初始化；`center_x/center_z`为径向／轴向中心，`center_y=0`，活动中心键显式必需。130份均匀源派生输入只补`center_z=0`，旧输入不冒称当前配置通过。原角动量`J=sum(mom_w*W)`和边界／体力矩`1e-12`保持；T/W输运不能重复叠加方位几何源。

无引力二维成本对照保持匹配单元／patch、EOS、stage、线程、输出与固定步数。冻结报警线为同候选rphi的2倍，原绝对锚点上限527.86／471.93ns每cell-stage，基线最大CV2.33%；不能拖慢rphi、改CFL或物理终点达标。三维约化与不同Green函数引力不是同工作量墙钟比较。原成本证据适用范围不扩大。

## 全科目覆盖与验收范围

公共模型库存为`Sod`、`Sedov`、`SmoothAdvection`、`RT`、`Gaussian`、`DiffusionMode`、`ExternalGravity`、`GravityBox`、`JeansWave`、`BurnOneZone`、`BurnGradient`、`CellularDet`、`SNIaCoupled`、`CooperativeHotspots`、`UserBoundary`、`UserGravity`。库存不代表全部未验，也不代替公共算法路由的资格矩阵。

| 科目／路径 | 已签收依据与后续变更回查范围 |
| --- | --- |
| Hydro／通量／重构／积分 | 公开方法、激波／平滑锚点及原完整收敛组；不拆组绕过门槛 |
| 几何／AMR／边界 | 适用chart的轴／极点、ghost、守恒转移、reflux、粗细面、回滚及重网格；近原点限制不得虚构安全域 |
| EOS及表 | IdealGas／Helmholtz／Tabular等实际路由的表域、单位、反演、拒绝及CPU/GPU资格；数据来源和许可齐全 |
| 扩散／RKL／线性求解 | thermal／viscous／species、材料限制、独立oracle、边界功及失败恢复；DenseLU／KLU／可选cuDSS按声明核验 |
| 网络／ODE／NSE | 内建网络、BE_NR／ROS4／BD及合格NSE沿原收据；实际候选另完成生成包custom:nse_alpha三ODE、NSE启停及能量／电荷闭合；弱表／稀疏按各自实际依赖限定 |
| Cellular与必要耦合 | 原Flash2D138步、公开Cellular200步和当前原私有24步已按原身份验收；保留ENUC、守恒与fresh，不重复原长程或放宽标准 |
| 外场／自引力／Jeans | 复用新chart和保留坐标适用场／能量／耦合；必要原终点和续算按实际发布缺口验，不转签旧RZ |
| 配置／IO／Core API | 保留缺项、显错、非法组合、来源、字段／单位及失败传播；CHK与Plot分别按真实身份契约，必要跨后端续算实际验证 |
| CUDA公共集成 | 复用单GPU原索引／壁面／内存／stream／AMR和终点证据；其他设备、toolkit、多GPU及整应用GPU峰值尚无本轮资格 |
| Studio／平台／分发 | 候选388项与实际Configure→Build→Preview／AMR→Run→Restart→Plot／Inspector、拒绝与关闭恢复已签收；仅核实Linux／WSLg，CLI独立可用 |
| CI／构建／性能 | 真实托管、干净环境和实际包必须有匹配身份的结果；区分cold／无动作／增量／运行／资源，不能用旧局部提速外推完整应用 |

每个缺口只保留一个当前问题项，写明owner、影响声明、下一动作、证据及关闭条件。旧报告中的空复选框和“待验”不自动成为新任务。

## 文档系统的目标安排

| 内容 | 唯一当前owner／入口 |
| --- | --- |
| 入门、运行、配置与排错 | 根README、[指南](docs/guides/README.md)、[参数参考](docs/Reference.md)；中英文语义一致 |
| 数学、物理、符号及适用域 | `docs/physics/`及源码／模型既有owner；保留方程、单位、离散、假设、误差与来源 |
| 贡献、模块归属与注释 | [开发入口](docs/development/README.md)、[实现归属](docs/development/ImplementationOwnership.md)、[写作规范](docs/development/CommentAndDocumentationStyle.md) |
| 怎么测／验证了什么 | [测试入口](tests/README.md)／[科学验证](validation/README.md)；收据和原始结果按各自身份解释 |
| Studio、API与法律／资产 | [Studio](studio/README.md)、[Core API](src/api/README.md)、既有EOS及`docs/legal/`owner |
| 人类历史与固定证据 | [histroy统一索引](docs/histroy/README.md)；固定路径例外单列，不建同量占位页或第二套history树 |

一个主题按读者／用途设权威，原始证据与数学说明可以分开。当前标题不依赖开发代号；真实协议字段、发行版本和精确命令按需保留。整理只用保留、合并、必要历史、删除，不复制平行总表。

## 源码注释与魔法数字清理

覆盖第一方`.cpp/.cu`及承载共享数学／接口的`.h/.hpp/.cuh/.inc`，第三方、生成物和冻结源码分开登记。按既有Workflow解释职责、前置条件、真实流程、输出／失败及资源生命周期；公式旁说明符号、单位、约定和有效域。

非显然数值区分物理常数、上游拟合、容差、种子、迭代／工作预算和设备布局参数，附近写含义、来源及修改影响；不编造推导，不给显然0／1堆注释。纯注释与常量提取分验，值、类型、精度和运算顺序保持；源字节／BuildId变化不能反写旧执行物证据。详细要求仅在[既有规范](docs/development/CommentAndDocumentationStyle.md)维护。

## 测试与CI的精简目标

| 层次 | 职责和触发 |
| --- | --- |
| 日常／PR | 一次完整Tooling、保守影响范围内的完整非Tooling CPU库存／低密度矩阵、一次完整Studio；文档做静态核查，未知路径保守执行 |
| 模块／真实后端 | 受影响科学owner、独立oracle、AMR／边界／IO／fresh及真实CUDA生命周期；复用原runner，不为参数另建重编译程序 |
| 最终发布资格 | 冻结候选的完整所需清单、公开模型／算法及必要耦合、未决科学资格、实际包／平台／托管结果；长程不进每PR |

[CI入口](.github/workflows/README.md)定义适用性与完成检查。main PR及工程分支push均执行同一汇总门；三个必需job真实成功才能汇总；`NOT_APPLICABLE`不产生虚假JUnit／TAP。空组、缺项、失败、skip、TODO、取消和截断均不能通过。先映射独有断言再合并；独立参考不能复用生产算法制造共同错误。保留已测RKL职责拆分，不合回超重单元。

## 执行边界与最终签收

- 使用conda `work`；pidfd资源工具使用系统Python。WSL约7.7GiB／RTX3060Ti8GiB，重型j2／CUDA heavy pool1、科学OMP4并尊重owner显式OMP1；轻任务按已测资源并行，不在计时窗口改源码／配置／输入。
- 异常缓慢可受控截断，保存步数／物理时刻、计数、墙钟、资源及已有数值结果；记未完成／待优化。非阻断性能假设最多两次有界尝试；正确性失败继续阻断。
- H5批次前或明显增长／IO异常时查实际宿主`/mnt/e`；约1GiB预警并停止大输出，保持写入预留，不高频查盘。复用已校验本地表与fixture，禁止自动LFS pull／fetch；原始H5／CHK不外发。
- 最终候选先冻结再测cold、无动作、规定编辑／重链及运行成本，记录命令、线程、编译器／FP、来源、缓存、GNU和进程树RAM／swap；未知为null，重叠时间不相加冒充总墙钟。
- REL-6已按2026-10-20冻结候选闭合；适用证据、16模型、平台边界、成本和历史失败集中在候选收据。本轮负责人已明确授权进入 REL-7 完成合入、标签和源码发布；完整CUDA编译吞吐保留为后续独立优化项。
- 最终发布要求全部声明路径无阻断、科学标准满足、实际包／说明／CI身份一致、无已知当前断链、源码职责可理解、非显然数值有解释；局部测试或阶段结案不代签这一结论。

## 进度维护方式

只更新本文件的阶段状态、问题摘要、下一动作和证据链接。明细留既有`validation/`机器记录，原始日志留本机；保留失败与限定资格，不新增逐轮人读日报、层层索引或平行总计划。REL-6已闭合，恢复`output/release/rel6-20261020/progress.json`；候选收据记录冻结身份与实际结果，随后文档提交不冒称本地重编，不改签旧收据；REL-7 本轮已获授权，正式闭合以远端合并／标签／Release 和适用托管检查为准。
