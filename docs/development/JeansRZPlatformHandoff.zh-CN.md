# Jeans、RZ 与第二平台验证执行细则

2026-10-01。本文配合[联合交付入口](StudioConfigurationHandoff.zh-CN.md)，
将 **O7.1–O7.5 的实现与受影响验证、部分 O9 的数据生产**交给同一位合作者。
[主计划第 2、3、5 节](ComputeOptimizationPlan.zh-CN.md)继续定义数学和科学验收，
本文细化工作顺序、复用位置、评审材料与第二台机器的执行方式。
所有新增能力仍待实现；本文件不是新机实测结果或已冻结的科学输入包。

## 1. 可以承担到哪一步

| 范围 | 合作者的完整交付责任 | 维护者的责任 |
| --- | --- | --- |
| O7.1 | JENS 共享计算、AMR 标志／生命周期、输出、配置／Studio 接入、测试 | 声速和分辨率定义、边界状态、参考与预算 review |
| O7.2 | RZ 坐标描述、公开接口保持、能力／单位及输入迁移 | 几何语义和旧极平面迁移的物理等价性 |
| O7.3 | 度量、现有源项、输运及轴线接合，CPU/CUDA 共用 | 离散平衡、旋流、分量／面积／体积约定 |
| O7.4 | AMR／椭圆算子／孤立边界的实现和工程验证 | 环体引力的数学路线、近远场误差分配、独立参考及科学签收 |
| O7.5 | 输入／API／GUI／IO／checkpoint 同步与完整短回归 | 当前支持域、兼容性拒绝和验收结论 |
| 部分 O9 | 在批准的冻结模型上运行 CPU/GPU 短 benchmark、30–60 分钟量级长轨迹、续算及汇总 | 选择科学终点／规模／预算，分析物理差异和跨平台意义 |

**实施责任可以委托，科学判据必须事先明确。**
对方可修改必要 C++、CUDA 适配、测试、输入和文档，不限于前端或代跑命令。
普通工程问题按约定自主解决；需要改变 EOS、源项、分裂、离散或参考预算时，
提交最小重现和备选方案，由维护者给结论，不让无物理分析背景的合作者自行猜测。

O8 用户 BC 的数学与实现、O9.3 外部代码比较、O10 网格重设计不包含在本次委托。
现有边界下的 O9 子集在相应 O7 短检查和科学预算通过后可以先执行；
需要新 BC、时变外势或尚未具备收支诊断的轨迹保持待前置完成。
这批数据不能直接把整个 O9 或所有模块任意组合标成通过。

## 2. 执行与 review 顺序

1. 完成 O7.0 配置和 G 迁移，固定有效输入、Core/API 与源码／binary 身份。
   GUI 启动／构建可按联合计划并行；数值验证从命令行执行，不等待 plt 界面完成。
2. 按阅读清单确认数学所有者，提交“当前调用方 → 修改点 → 参考 → 测试”映射。
   O7.1 与 RZ 各有独立提交，不能用一批大改动混合所有错误来源。
3. O7.1 先 CPU 单元／集成；O7.2–O7.4 顺序完成 CPU 几何、流体、AMR 和泊松短检查。
   轴对称边界在数学方案确认后接入生产；未确认时可做有界实验与独立验证工具。
4. O7.5 完成整条 RZ 路径、旧模型拒绝／迁移及前端语义同步，才发布为可运行能力。
   中间 checkpoint 可以推送 review，但不能把只改标签的 RZ 宣称完成。
5. CPU 阶段通过后统一编译 CUDA，复测共享数学、几何适配、regrid／续算及设备安全。
   可预编译未改的网络／EOS 对象；最终构建必须有完整依赖身份。
6. 冻结可执行文件、数据和 manifest，执行第二平台短 benchmark；
   确认规模及终点后执行本次 O9 长轨迹。计时、profiling、编译和 GUI 渲染错开。
7. 提交原始值、独立分析器输出、异常与覆盖／耗时账本；维护者科学 review 后标记对应出口。
   修改数学／输入会使受影响结果失效，纯文档变化不要求重新计时。

保留 O7.0 之后的旧能力基线和 O7.5 完成后的新基线，比较共同物理问题。
JENS 新增细化工作量、旧二维极平面与新 RZ 不属于同工作量／同模型，
另列结果，不能拿它们的步数减少宣称底层优化。

## 3. O7.1：JENS 的实际工作包

必须阅读主计划第 2 节及[配置完整性清单](ConfigurationContractPlan.zh-CN.md)；
`JENS` 是现有字段名，不重命名或再添加平行 `Jeans` 开关。
定义为 `N_J=lambda_J/max(h_active)`，`lambda_J=sqrt(pi*c_s^2/(G*rho))`：
共享 CGS G、已接受状态总正密度、当前 EOS／组分声速及真实物理间距。
不得使用周期扣均值密度、块宽度、最小 dx 或一般 EOS 的固定 gamma 替代。

| 实现点 | 现有位置与复用要求 | 必需证据 |
| --- | --- | --- |
| 数学叶函数 | [physics/diagnostics](../../src/physics/diagnostics)、[RefinementIndicatorMath](../../src/amr/refinement/RefinementIndicatorMath.h)、[GridMetrics](../../src/grid/GridMetrics.h) | 一个 CPU/device 可调用公式；IdealGas 独立缩放、一般 EOS 及低密度参考 |
| 指标与标志 | [amr/refinement](../../src/amr/refinement)、[AmrTree](../../src/amr/topology/AmrTree.h) | 均匀零曲率但欠分辨也细化；多指标任一细化、全部允许才粗化 |
| 粗化／发布 | 既有 regrid 事务和保守迁移 | 用候选父态及父尺度判断；阈值附近不来回振荡；原守恒预算 |
| 状态生命周期 | 已接受宏步、EOS 工作区、AMR epoch | 初始及每个接受宏步检查，必要细化在下一步前完成；不在未提交 RK／burn／RKL 子阶段改树 |
| 输出与身份 | 现有 plot 派生场、配置／checkpoint 身份 | 输出开关不改变推进；重网格／重启后重算，不能持久保存陈旧派生场 |
| CUDA | [RefinementIndicators.cu](../../src/cuda/amr/RefinementIndicators.cu) 和既有暂存／归约 | 同公式和标志判据；不为诊断反复下载全域，受影响分配有设备检查 |
| 配置／Studio | `jeans_cells` 与已有 refine_var／plt_variables／能力目录 | 条件必填、≥4、无静默推荐回填；单位 1；不可用／适用性明确 |

`jeans_cells` 是现有主计划中唯一新增的分辨率控制，归 AMR。
普通 Löhner 曲率阈值不用于 Jeans 格数；无新 G、rho_floor、cs_floor 或压力 floor。
达到层级／容量限制仍欠分辨必须失败并报告单元／层级／最小 N_J 和原因。
仅输出、触发 AMR、未启用三种路径分别检查；未启用时不增加遍历、EOS 或同步成本。

维护者先确认：一般 EOS 声速含义、诊断/AMR 的条件需求、父态规则与参考预算。
尤其“约四格”不是所有闭合、非线性碎裂或降维模型的充分收敛保证。
用 [JeansWave](../../simulation/JeansWave/README.md) 和
[GravityBox](../../simulation/GravityBox/README.md) 做集成，不另外复制初始化公式。
若沿用状态缓存，提交接受状态／组成／EOS／epoch 的失效列表，而不只展示缓存命中率。

## 4. O7.2–O7.5：RZ 的实际工作包

### 4.1 先冻结几何语义，再开放能力

2D cylindrical 采用 `(r,z)`；3D 仍为 `(r,z,phi)`。
`PointCoords.r` 继续是球半径，`r_cy/z_cy` 继续是柱坐标；
用户可取局部 `r,z` 别名。输入坐标和计算 geometry 分开，不能因重命名改变物理公式。

旧 2D cylindrical 极平面显式迁往已存在的二维 spherical 极平面语义；
不能仅改 x2 单位，或把旧 polar 的运行结果与新 RZ 直接相除。
是否属于可迁移输入由维护者检查密度、体积、边界和势核，
不能用替换字符串批量宣布等价。二维 spherical 不在本轮隐式改成 r–theta。

采用完整旋转环体的统一积分约定，主计划中原“建议”的约定在进入实现时冻结为：

```text
V   = pi*(r_right^2-r_left^2)*Delta_z
A_r = 2*pi*r_face*Delta_z
A_z = pi*(r_right^2-r_left^2)
h_r = Delta_r, h_z = Delta_z
```

内部可约去共同常数，但质量／能量／角动量、重力质量矩、边界通量、
HDF5 与分析器对外都采用同一完整环体约定。
不得一处使用单位方位角、另一处又乘 2*pi。

### 4.2 分模块同步清单

| 模块／所有者 | 必须同步 | 验收关注点 |
| --- | --- | --- |
| [GridMetrics](../../src/grid/GridMetrics.h)、[GridGeometryView](../../src/grid/GridGeometryView.h)、[Grid](../../src/grid/Grid.h) | 位置、体积、两向面积、间距、体积平均几何系数 | dr／dz 与真实环体一致；不能只改 CFL |
| [GeometricSources](../../src/numerics/integrator/GeometricSources.h) | 第二动量 z、第三动量 phi；无方位导数但保留旋流 | 静止恒压相消、z 平移、离心与角动量预算；保持一个源入口 |
| 既有扩散、梯度和诊断 | 标量梯度、矢量基、黏性几何、div／vorticity | 适用材料的解析检查；Helm 缺失的物性不会因坐标改动自动补齐 |
| [CoordinateSeamPlan](../../src/amr/exchange/CoordinateSeamPlan.h)／[Math](../../src/amr/exchange/CoordinateSeamMath.h) | 按几何识别方位和轴线，RZ 不走跨半圈 donor | 标量／z 偶、r／phi 奇；轴 r=0 与非零内边界区分 |
| AMR transfer／flux | 环体 restriction／prolongation／reflux 和粗细面唯一通量 | 同层与多层迁移、质量／角动量、真实混合网格、拓扑事务 |
| [elliptic](../../src/numerics/elliptic)／[multigrid](../../src/numerics/multigrid) | RZ 算子、粗层、体积范数、零空间／边界 | r=0 零面积正则面；独立 RHS／力／通量参考 |
| [GravityBoundary](../../src/physics/gravity/GravityBoundary.cpp)／[GravityWorkspace](../../src/physics/gravity/self/GravityWorkspace.cpp) | 环体质量与质量矩、近远场边界、几何身份 | 禁用 RZ 的二维对数势分支及点质量替代；不能只验残差 |
| [GravitySource](../../src/physics/gravity/GravitySource.h) | 接收同一面质量通量与正确几何量 | 引力功只计算一次，继续共用现有能量耦合 |
| [GridMetricsCache](../../src/cuda/common/GridMetricsCache.cu) 与 [CUDA gravity](../../src/cuda/runtime/gravity) | 上传／缓存／执行适配与身份失效 | CPU/CUDA 共用公式；轴线、重网格、续算及错误传播 |
| API、Studio、plot、checkpoint | x2 长度单位、切片、原生单元、几何语义身份 | 旧缓存失效；旧二维 cylindrical checkpoint 拒绝；新版实际继续演化 |

新增/拆分文件按完整职责决定，复用原所有者；Driver 保留组织流程。
不要为 RZ、CPU、GPU 各建立一份源项或 MG 公式。
新／修改 src 文件按[注释规范](CommentAndDocumentationStyle.md)写 Workflow、
子函数职责及公式来源；公式语义和接口变更同时更新用户文档。

### 4.3 O7.4 专门的科学复核点

RZ 是三维轴对称 Newton 引力：环体源的势和力依赖径向及轴向距离。
生产边界复用当前树和所有权，不能把全部环质量放到 `(r,0,z)`，
不能继续使用二维对数核，流体 reflecting 也不自动补域外镜像质量。

实现负责人先提交以下短设计，不要求他独立判断哪一种物理近似可以接受：

1. 环体源、cell-centered 势和面力的离散语义，以及现有矩/树/算子复用位置。
2. 远场多极矩及其阶数、近场积分或解析核来源、切换判据、轴线／接近源时的处理。
3. 边界截断、源积分、离散、MG 残差与后端舍入的分项误差预算。
4. 独立 oracle 如何生成、为何不调用待测生产实现、源几何/体积平均是否匹配。
5. 几何／AMR epoch／质量状态改变时哪些量重建，CPU/CUDA 如何共用。

维护者确认后实施。原则已确定不等于近场积分阶数、开角或误差预算已确定；
这些不能让对方从“测试能过”的最终数值倒推。

必需短验证包括：

- **制造解算子**：例如 `Phi=a*r^2+b*z^2`，有
  `L(Phi)=4*a+2*b`、`g_r=-2*a*r`、`g_z=-2*b*z`。
  现有 `A=-L` 离散对应 RHS 取负号；它在椭圆单测中提供解析边界，
  不借此提前开放 O8 的生产用户 BC。正确处理点值／体积平均语义。
- **独立球对称场映射至 RZ**：检验两向力与轴线，包含物理体积积分。
- **有轴向结构的环体源**：同一有限源模型的独立高精度积分检查近、远场；
  不以薄环参考去认证未说明近似的有限单元，也不以同一生产核互证。
- **AMR 与边界分离**：规则网格、真实混合层级、粗细面、反复细化／粗化；
  残差达标还须满足力／势／边界误差和守恒预算。
- **动力学**：恒压静止、z 平移、带旋流、解析或收敛参考下的自引力；
  质量与角动量变化按边界和源项收支解释。

### 4.4 迁移与开放出口

O7.2 的标签变更到 O7.5 的完整数学必须成套进入发布版本。
中间提交可留在开发分支供单元测试，Core 能力不得提前对外开放不完整 RZ。
配置扩展版本 3 和几何／checkpoint 身份分别处理；不认为一次配置版本升级已经保护所有旧文件。
旧几何续算给清晰错误，不自动猜测极平面与轴对称的转换。

扩展现有 `poisson_multigrid_*`、`composite_poisson_*`、`self_gravity_*`、
几何／refinement／checkpoint 测试与 CUDA 对应入口，记录具体子用例。
保留 1D 径向、3D Cartesian／柱／球及二维原极平面回归；
改变通用 `dim==2` 分支时列出所有消费者。旧物理参考继续适用于未改语义的模型。

RZ 去掉二维方位方向后不再有该方向的 `r*Delta_phi` 限步；
三维极点／角向小单元和几何源项限步仍在。
本轮不通过改变网格拓扑、放宽 CFL 或改积分分裂顺带追求新的速度目标。

## 5. 部分 O9：第二平台数据集

### 5.1 哪些先做，哪些等待

本次接入主计划 O9.1／O9.2／O9.4／O9.5 中**已具备内置边界和独立预算的子集**。
新机为 i7-14700K＋RTX 5070 Ti，分别记录 CPU 与 CUDA lane。
这提供平台复现、资源及长期稳定性证据；共享同一数学库的跨机一致不能替代独立物理参考。

| 样本组 | 短检查／benchmark | 长时轨迹 | 必须事前具备的条件 |
| --- | --- | --- | --- |
| Sod／光滑平移 | CPU 生产基线、后端检查；小问题负收益保留 | 不为凑一小时延长 Sod | 原有解析／收敛预算 |
| JeansWave＋自引力 | 稳定波、JENS 诊断／AMR 单独计成本 | 多个声学／引力周期，记录相位／振幅／能量与迁移 | 稳定波数、有限振幅误差、均值扣除／规范、批准的 N_J／AMR 历程 |
| 正式扩散驱动／Gaussian | 材料闭合一致的线性扩散锚点 | 多个扩散特征时间、热量和极值 | 独立解与时间／空间误差；无需新建关闭 Hydro 的全局开关 |
| BurnOneZone／燃烧驱动 | EOS／网络／ODE 耦合与守恒 | 点火、刚性及平衡附近的有效阶段 | 已批准热力学／网络身份和参考；单区不要求 GPU 加速 |
| CellularDet＋AMR | 已发布反应流代表点 | 前沿／组分／能量、反复 regrid、续算 | 固定扰动／种子、独立或收敛参考；不靠 O9.3 FLASH 结果作为必需输入 |
| RZ GravityBox | O7.5 短证据 | 轴线、z 力、旋流／角动量、AMR 和重启 | RZ 独立参考与适用边界；不得直接把旧径向初始化改名当 RZ 初态 |
| 2D／3D SNIaCoupled＋AMR | 四模块活动见证和整程计时 | 先冻结一个代表长轨迹；另一维度补短证据，必要时有依据地扩展 | 核／热／引力作用实际非零、分离收支与收敛预算；局部热点不冒充整星 |

这是覆盖清单，规模、终点和预算在冻结包中逐行确定。
优先完成引力、热扩散、燃烧的模块锚点，再用 Cellular、RZ 或四模块的少数代表轨迹
覆盖独有耦合，不把每一行乘上全部 EOS／方法／线程／几何组合。
一次轨迹可满足多条覆盖，但必须逐项给证据；“运行成功”不能替代独立模块贡献。

需要 O8 新用户 BC、未知域外质量反作用、未完成边界收支，
或没有独立误差预算的条目标 `pending-prerequisite`。
可以记录探索运行，正式计时／科学通过暂不签收，也不标成常规 CI 的 skip/pass。

### 5.2 开跑前的冻结包

合作者在现有 manifest／provenance 体系补齐下表，由维护者确认科学列后执行。
尚未给数值的列不能靠当前默认值或跑后观察填写。

| 必填内容 | 谁负责定案 |
| --- | --- |
| case、输入与有效参数、是否旧语义迁移，G／CGS／EOS／网络及数据身份 | 合作者整理，维护者确认物理语义 |
| geometry、边界、物理域、root cells、AMR 层级与实际最细尺度、JENS／regrid | 合作者整理，维护者确认可比性 |
| 物理终点与特征时间覆盖、CFL／ODE／Poisson／RKL 控制、输出／checkpoint 计划 | 维护者确认；合作者保证实际生效 |
| 独立参考来源、误差定义／权重、阈值、守恒及边界账本、模块活动见证 | 维护者提供或审阅参考；合作者封装执行 |
| 短测重复数、CPU affinity／线程、GPU Host 线程、资源上限、运行顺序 | 合作者按联合计划提出并冻结 |
| 同后端／跨后端续算分割点、CPU 与 GPU 的对应终点 | 双方确认；不能只匹配步号 |
| source／binary／库／配置身份、命令、输入生成与结果路径 | 合作者交付，工具自动检查 |

正式终点、输入与阈值冻结后不得为耗时或收敛方便调整。
科学有效的热／核／动力学尺度决定 t_end；短试跑仅帮助选择资源可承受的规模。
这些完整可运行输入、单条复现命令与分析器属于实施交付物；
本文是计划，尚未提供可以直接跑满一小时的已批准新 RZ 输入。

### 5.3 14700K 的线程与稳定运行约定

i7-14700K 为 **8 个 P 核、12 个 E 核、28 个硬件线程**；
不能把 28 线程写成 28 个同质物理核。
依据：[Intel 规格](https://www.intel.com/content/www/us/en/products/sku/236783/intel-core-i7-processor-14700k-33m-cache-up-to-5-60-ghz/specifications.html)。

- 先记录实际可见拓扑、逻辑 CPU 与核心/SMT 对应、内存通道／频率、OS／WSL 配额，
  BIOS／微码、功耗限制和散热状态。以已验证稳定设置为基线，超频／降压变体单列，
  不要求合作者为 benchmark 临时刷 BIOS 或改变硬件。
- 原生 Linux 且能可靠识别拓扑时，有限筛选：1 个 P 核线程、8 个 P 核各一线程、
  P 核 16 线程、全部 20 核各一线程、全部 28 线程。每组明确实际 CPU 列表。
  这些是候选，不预先断言 28 最快；不无限搜索全部绑核组合。
- WSL 如果只可靠暴露虚拟 CPU，就记录 vCPU 数和虚拟亲和性，
  不把任务管理器或编号推测当作已验证的 P/E 固定映射。
  使用可复现的 1／8／16／20／28 可用线程子集，不超出分配 vCPU；
  报告明确该限制，不因此强制改装原生 Linux。
- `OMP_PLACES=cores` 不表示“只用 P 核”，`OMP_NUM_THREADS` 也不等于完成绑核。
  核对 OpenMP 实际 places／binding，保留运行环境和亲和性输出。
  依据：[OMP_PLACES](https://www.openmp.org/spec-html/5.1/openmpse62.html)、
  [OMP_PROC_BIND](https://www.openmp.org/spec-html/5.1/openmpse61.html)。
- 正式测量前在代表场景筛选，随后固定线程数、绑定与策略。
  报告每样本最佳实测配置和共同固定线程配置；筛选数据与正式重复测量分开，
  不从同一批正式样本中挑最快一次作基线。
- CUDA 的 Host AMR／EOS 准备等同样消耗 CPU；记录其资源配置，
  不与独立 CPU 长轨迹同时跑并据此比较耗时。两台机器可以各自并行工作，
  同一台机器的正式 CPU/GPU lane 串行执行。
- 预热后记录温度、频率／降频线索、功耗、RAM／swap 与显存随时间变化。
  无法读取的指标记 unavailable；OS／驱动错误保存为平台证据，不直接归为 ARCH 数学故障。
  测量包含正常热稳态，不以反复冷却后挑最好成绩替代持续运行成本。

GPU 工具链、SM_120、CPU-only 与 CUDA-binary 两种 CPU 基线、至少三次成对交替测量、
绝对墙钟口径及负收益处理沿[联合计划第 7 节](StudioConfigurationHandoff.zh-CN.md#7-rtx-5070-ti新平台的执行协议)。
跨 3060 Ti／5070 Ti 主机是平台结果，不能声称单凭比值隔离了 GPU 硬件贡献。
H100 后续补测独立登记，不阻塞本阶段的已定义出口。

### 5.4 长时预算、重启与故障处置

- 首批主要 CPU 长轨迹以约 30–60 分钟为资源目标，不是硬性的物理终止条件。
  必须到冻结的 t_end；快则按实际完成记录，慢则按资源协议停止并保留恢复点及未完成状态。
  不能降低 CFL、扩大 max_steps 或延长已无活动的尾段凑一小时。
- 同一 .par 的 CPU/GPU 达到相同终点。若 GPU 很快完成，仍是有效加速；
  专门的 30–60 分钟 GPU 耐久轨迹另有身份，工作量不同就不计算对比加速比。
- 在预设物理时刻采样场、真实体积积分、模块计数、AMR 历程及资源。
  采样和输出频率在 CPU/GPU 一致；不能因 GPU I/O 贵就独自减少输出。
- 先在既有代表 checkpoint 证明同后端／CPU↔CUDA 恢复及继续演化，
  再对选定的 AMR／耦合长轨迹做完整续算至同一终点。
  不把所有长轨迹机械乘以所有续算组合；以独有覆盖说明选择。
- 长轨迹至少完成所选 CPU 和 CUDA 各一次；若出现漂移、非确定失败或明显噪声，
  对相关组定向复测。短 benchmark 保持独立重复批次；完整轨迹和续算耗时分别记录。
- 使用现有 [资源保护器](../../tools/run_memory_guarded.py) 与 checkpoint 机制，
  事先记录 RAM／显存／磁盘／墙钟边界。达到资源上限不能删输出后冒称完成。
- 非有限状态、非正密度、EOS／ODE 明确失败、残差超限或超预算漂移按既有科学门槛判定。
  记录首次异常时刻、单元／层级、输入与有效状态、残差目标、进程和硬件日志。
  不自动重启掩盖第一次失败，不放宽 floor／精度，不在最后一刻增加忽略错误开关。

## 6. 必须读什么、如何交回

在[联合交付入口](StudioConfigurationHandoff.zh-CN.md)的阅读顺序后，
按本任务补读下列资料；历史记录只用于找来源，当前规则以主计划和实际源码为准。

| 资料 | 对方需要掌握 |
| --- | --- |
| [自引力实施计划](SelfGravityImplementationPlan.zh-CN.md) | 域求解／发布／失效与 patch 只读消费，物理和 MG 的分工 |
| [曲线坐标计划](CurvilinearGravityPlan.zh-CN.md) | 当前已验收几何、算子符号、势／面力语义、奇点与边界；其中旧 2D cylindrical 语义由本轮 RZ 计划明确替换 |
| [实现所有权](ImplementationOwnership.md) | 共用数学、AMR 事务、CUDA 存储／调度、checkpoint，不重写并行实现 |
| [引力验证](../../validation/gravity/README.zh-CN.md)、[AMR 验证](../../validation/amr/README.md) | 独立参考、实际混合层级、粗细面和收支含义 |
| [扩散](../../validation/diffusion/README.md)、[燃烧](../../validation/burn/README.md)、[EOS](../../validation/eos/README.md) | 材料范围、参考来源与误差；看不到物理贡献时不能计作耦合 |
| [后端 manifest 与入口](../../validation/backend/README.md)、[重启验证](../../validation/restart/README.md) | 固定物理终点、严格续算、身份和现有分析器 |
| [SNIaCoupled 说明](../../simulation/SNIaCoupled/README.md) | 局部示例、四模块要求、Helm 热传导限制及几何差异 |
| [发布标准](CudaReleaseStandard.md)、[报告指南](../guides/Reporting.zh-CN.md) | 全部测量值、失败和限制的交付方式，CI 收束及复现身份 |

实施前与每个阶段结束后回查主计划及本文，不要求反复通读所有历史报告。
文件改动表同时列出直接实现和消费者、API／文档／测试，避免漏掉 CUDA 或读取工具。

证据仍放 `validation/backend/results/<日期与用途>/` 及所属物理模块结果目录。
**先在本机后处理，再上传精简结果；H5/HDF5、plt、checkpoint、完整场数组和 trace 不上传。**
严格执行[联合入口第 9.1 节](StudioConfigurationHandoff.zh-CN.md#91-先本机处理再交付结果)：
只交汇总 JSON/CSV、完整数据计算的误差／收支指标、计时表、必要图表和诊断片段。
原始文件留在本机持久目录；不通过压缩、改后缀、Git LFS、PR 附件或 CI artifacts 绕过。
扩展现有 manifest／provenance／后处理，不新造只识别这台电脑的 runner。
增添一个可读的 `scope-and-coverage` 表，至少包含：

- O7 子项／O9 模型、所报 commit、冻结输入与预算、独立参考来源和对应结果文件。
- `implemented / engineering-pass / physics-review-pending / accepted / failed / pending-prerequisite`
  分别表示实现、工程、待物理复核、通过、失败和前置缺失；这些为交付状态，不是新 Core 参数。
- 每条轨迹是否达到 t_end、终止原因、实际活动模块、AMR 层级／cell 更新和续算覆盖。
- 守恒及源／边界收支、状态修复、ODE 拒步／EOS 失败、泊松真实残差、
  空间／时间误差及后端差别；给实际值和冻结阈值，不能只有绿色勾选。
- CPU 线程／亲和性与 GPU Host 配置、逐次墙钟、初始化／推进／I/O 归因、
  温度／频率／资源的可用观测。重叠时间不得累加为总耗时。
- 所有未通过及科学待审项：首次异常、最小重现、已排除因素、拟议处理及需要维护者回答的问题。
- 本地持久数据编号、文件名／尺寸、科学身份和保留期限，用于按需复核；
  原始文件不随报告上传。维护者需要进一步检查时，另行确定最小必要的处理后材料。

阶段退出检查：

- [ ] O7.1 公式／标志／失败／生命周期／输出／重启及关闭路径开销通过。
- [ ] O7.2–O7.5 语义／度量／源项／AMR／边界／身份成套通过，独立 RZ 参考获科学 review。
- [ ] CPU 通过后完成受影响 CUDA；没有重复数学或全域不必要回传。
- [ ] 第二平台短 benchmark 保留绝对时间、全部重复值和负收益；同问题同终点。
- [ ] 已选 O9 子集逐条达到冻结终点及科学预算；未具备条件的项目清楚保留，未冒称整个 O9 完成。
- [ ] 测试按独有覆盖收束；长轨迹／正式性能手动运行，不扩张日常 CI，不依赖 FLASH。
- [ ] 本地后处理完成，上传清单无 raw data；成功、失败、未完成均有精简且可复核的证据。
- [ ] 最新 Reference／算例／API／功能说明和注释同步，用户不会看到未实现能力已开放。
