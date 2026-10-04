# JENS/RZ：已定实施与逐项科学review清单

Authority：11a321d5604f9ee62b9f9587c81f14de4f128bc4，JeansRZPlatformHandoff第2.1/3/4节。不能继续把已有明确实施规则当作整阶段停工原因。

## 已确定、可以实施

JENS使用总正密度、当前EOS声速、活动方向最大物理间距、共享CGS G；候选父守恒态/父尺度判断；初始及接受宏步后检查，下一步前完成必要细化；层级/容量仍欠分辨明确失败。输出/AMR/关闭三种路径分开，关闭不新增遍历或EOS同步。无floor/额外G/固定gamma替代/周期扣均值密度。

RZ完整环体measure、r/z坐标、共享operator/source/transfer与制造解、球对称映射可继续。未确认环体生产边界及新长轨迹单独等待。

## JENS子例/参考/预算（待逐项review）

| 子例 | 独立参考及已得值 | 既有预算出处/语义 | 请Core确认 |
| --- | --- | --- | --- |
| 隔离公式/极低密度/指数抵消 | JeansNumericLeaf与jeans_numeric_reference：独立Decimal80/120位、exact输入double、literal pi/G | 16*double epsilon是叶函数工程界限，不是科学演化预算 | 能否作为数学子gate；未代表EOS/AMR签收 |
| 真实IdealGas/双组分/运动态 | JeansIdealGasReference：24组合，max relative1.9657272738823614e-16；独立caloric closure | 既有叶函数16epsilon；样本为cell守恒态点值闭合 | 接受该独立闭合参考及覆盖域 |
| Native Tabular域内rho/T/Ye | JeansNativeTabularReference：240组合，max relative1.0004323370200706e-14 | NativeTabularFixture既有2e-12，只是log-linear解析fixture | 此fixture能否签收静态子gate；真实table/Helm/free-energy子例和预算仍需指定 |
| threshold与候选父态 | 以真实restriction父守恒态重新走同一EOS，不平均子声速；父spacing由真实GridMetrics | jeans_cells>=4已定；阈值附近科学/舍入验收未给具体预算 | 指定父态多材料/阈值临界独立状态和允许误差，不能由观察后拟合 |
| JeansWave/GravityBox集成/接受宏步/regrid/restart | 复用正式初始化、当前真实AMR和checkpoint，不复制物理 | Jeans原mass/energy/force/residual预算沿run_self_gravity；不能当作JENS新非线性误差预算 | 第6节已冻结 uniform-lifecycle-1；实际生命周期和非均匀扩展逐项待验 |

## RZ子例/参考/预算（待逐项review）

| 子例 | 可复用证据/语义 | 需确认的科学列 |
| --- | --- | --- |
| 制造解/轴线/混合层级 | RZCompositePoisson，Phi=a*r²+b*z²，A=-L；完整环体volume；operator点值与平均RHS明确区分 | 保留原测试budget，Corereview适用源/势/面力语义与覆盖；残差非独立势力验收 |
| 球对称映射至RZ | 沿第4.3节独立两向力/axis参考，不以同生产kernel互证 | 指定有限源、域/边界、点值或体积平均、误差weight与阈值 |
| 有限环体轴线/离轴 | RZFiniteRingAxis/OffAxisReference：独立高精度积分，observer点值；并非cell/face平均 | 是否采用piecewise-constant有限源，inside/contact路线及平均语义 |
| 近场积分 | 近外缘整体64→分区64 g_r差1.9753441487098356e-15 cm/s²；直接Newton与同阶核一致仍有共享源积分误差 | 分区/阶数/接触处理与源积分误差预算；不能由当前数值倒推“够准确” |
| 远场矩/树聚合 | 完整环体monopole+traceless quadrupole候选；示例Phi误差7.558246644870839e-16 cm²/s² | 第7节已定full-ring矩、余项界和误差接口；实际实现/覆盖域待验 |
| 边界/离散/AMR/MG/backend | 复用现有GravityBoundary tree/workspace与shared FGMRES | 第4.3节分项误差预算：边界截断、源积分、离散、AMR、MG、后端舍入，逐项给出处/阈值 |
| angular momentum transfer | RZBlockTransfer：保守mom_phi体积分不等于真实r-weighted Lz；axis变化4.9850588267931159e-4，offaxis1.039613642698e-4 | 第8节已定单一表示、贯通消费方和收支；原 finding 等实际科学子组签收 |
| RZ长轨迹/公开能力 | O7.5完整数学、旧checkpoint拒绝及所有消费者同步后开放 | 指定冻结case/input/domain/EOS/boundary/endpoint/reference/预算与restart分割点；未确认不启动新RZ长跑 |

本清单的已测误差是处理后历史证据，producer身份按各报告保留；不能冒充本次HEAD新测结果。没有新增/放宽阈值，没有从CPU/GPU一致宣布科学通过。接近源/全域参考及原始数组留本机，提交精简scalar摘要。模型满足对应短科学gate且冻结包完整后可独立推进CPU/CUDA长跑；本清单不是冻结输入包。

## 4. Core 决定、已批准子 gate 与剩余材料（2026-10-04）

评审对象为 `4b5e496a9943099c203c6b001b1d95a56d9edf72`。下面的批准是
参考定义和有限验收范围的批准；协作者的实际测试值保留其报告身份，
未把未复跑的 EOS/AMR、RZ 或 CUDA 轨迹写成本机科学验收。

### 4.1 JENS 静态参考与父态

| 子 gate | Core 决定 | 可继续的工作／覆盖边界 |
| --- | --- | --- |
| 隔离 numeric leaf／Decimal 参考 | 认可独立参考路线及原 16 epsilon 工程预算 | 精确 double 输入、缩放与拒绝路径；次正规结果按表示精度分别判断，不能承诺统一相对 epsilon；非有限／不可表示结果明确失败 |
| IdealGas 24 组合 | 认可单组分和冻结组分双组分 caloric 闭合参考 | 独立核对 gamma_eff=1+sum(X Cv (gamma-1))/sum(X Cv)=27/14；原 16 epsilon 是该良态 fixture 预算，不扩展为任意高 Mach 或演化预算 |
| NativeTabular 240 组合 | 认可当前 log-linear 合成 fixture 的静态子 gate，保留原 2e-12 | 不签收真实表或 Helm；复用各 EOS 已有独立值／导数／反演参考及误差预算补齐选定适用状态 |
| 声速含义 | 当前 EOS 在冻结组成下的绝热声速，即 (dP/drho)_(s,X) | 不是等温、燃烧松弛、NSE 再平衡或辐射闭合的有效声速；这些不随 JENS 自动支持 |
| 候选父态 | rho、径向/轴向动量、E、rho X 按 V；新 RZ 角向按第8节 W，再由同一父态 EOS 求声速 | 禁止平均子声速、子 N_J 或子温度来替代父态；子组分按 rho X 迁移 |
| threshold | N_J<jeans_cells 请求细化；父态 N_J>=jeans_cells 且其他指标及事务全部允许才可粗化 | 用实际 FP64 比较，明确等号行为；不插入降低目标的 epsilon、不加新参数；临界舍入样本与明确欠／已解析样本分开 |

父态独立样本使用单／双组分、不同密度和相反速度的手工守恒子态，
独立 long-double／Decimal 汇总 rho、动量、E、rho X 的体积分，
再按该独立 caloric law 求父态内能和声速。相反速度的平均动量可为零，
粗化后保留总能量意味着未解析动能进入父态内能，不能人为减掉该能量。
不同径向位置使用真实环体体积；新 RZ 角向依第8节使用 W。几何／AMR owner 继续只有一套。
高 Mach 内能抵消、不可解析状态使用既有 EOS 拒绝和预算，不夹紧以通过测试。

可先完成上述静态父态、比较边界和容量失败子例，再补接受宏步、regrid、restart
的实际生命周期。父态算术误差须来自参考精度和已有状态/EOS 误差传播，
不是把已观察误差重新乘系数当预算。JeansWave/GravityBox 的原质量、能量、
势力、残差 gate 继续保留；新的非线性终点、AMR 历程和演化参考包仍逐项定案。

### 4.2 RZ 环体参考与角动量

- **有限源工具的用途认可：** 完整、有限的轴对称环体源及源外 observer 点值参考
  可继续作为独立工具；现有环核、直接 Newton 和固定阶／分区差异用于诊断。
  不把薄环、点质量、二维 log 或相同源积分阶数下的一致性视为有限源误差已消失。
  源内／接触、cell／face 平均、生产近远切换、阶数及分项预算仍待短设计确认。
- **冻结物理角动量诊断：** 对当前 piecewise-constant m_phi 表示，
  V=pi*(r_hi²-r_lo²)*dz，W=(2*pi/3)*(r_hi³-r_lo³)*dz，Lz=sum(m_phi*W)。
  此处 W 是体积的一阶径向矩；面上的角动量通量还须用该面的力臂与原生面积积分，
  不能把 W 直接当作面面积或通量权重。
- **当前 finding 保留：** mom_phi 的 V 积分通过、粗化后恢复 parent，均不能关闭
  refined 阶段的 Lz 变化。已报告的约 5e-4／1e-4 不是容许阈值。

一个精确反例足以说明不能同时保持任意子态的两种矩：
parent r=[0,1]，两子区间 [0,1/2]、[1/2,1]，同 dz；
V1=pi*dz/4、V2=3*pi*dz/4；W1=pi*dz/12、W2=7*pi*dz/12。
若子态 m1=1、m2=0，则 V 守恒要求父 m=1/4，Lz 守恒要求父 m=1/8。
单个父标量不能对任意态同时满足两种约束。

RZ 的物理守恒判据以角动量及其边界／源项力矩收支为准；
`sum(m_phi*V)` 不作为另一项必须同时守恒的全域物理量。
当前 m_phi 的存储／平均语义如不能满足权威角动量矩，须在设计中明确相容表示或迁移，
不能把两个冲突约束一起列为必过门槛。

因此不批准“只修 prolongation 并保留原 restriction 就能同时守恒”的结论。
第8节已定单一 RZ 代表量、面力矩、传递、reflux、EOS 与轴线消费链和短收支门槛；
合作者按 A→B→C→D 提交实现和独立科学材料，不额外保存双权威状态。
质量、总能量、rho X 与正值约束保持；finding 由实际旋流／变旋流、
多次混合事务及有／无边界力矩的收支验收关闭，RZ 发布出口在签收前保留。

### 4.3 推进顺序与签收

先集成获准的两条架构迁移及受影响 tooling/config gate；并行完成 JENS 已定
静态父态和生命周期材料、现有模型工程工作；RZ 角动量和环体源提交独立短设计。
CPU 对应科学子 gate 通过后统一做受影响 CUDA 检查；已有完整冻结输入的模型
可独立执行长轨迹，未批准的新 RZ 不启动验收长跑。
继续分别标记实现、工程验证、科学 review、性能，使用同一 ARCH 工作区，
不新增外部软件依赖或独立 CI 矩阵；原始数据及全量日志留本机。

## 5. 父态静态子 gate 实施记录（2026-10-04）

按4.1节补充6个真实restriction→EOS→JENS父态样本：
Cartesian、内部RZ轴线及离轴，单/双组分，相反速度保留总能量。
独立体积分与caloric参考PASS；最大相对误差2.7056947525674304e-16。
预算仅为声明的静态舍入传播，不扩展至演化/高Mach，详见O7ApprovedArchitectureAndJeansParent-20261004.zh-CN.md。
阈值、容量和宏步/regrid/restart生命周期继续实施；RZ 4.2的finding和待审设计保持开放。

## 6. 已冻结的 JENS 短验收包：uniform-lifecycle-1

本节点以 `08ae94684d433ddbb7398a9de88c4fb9d09070b4` 的现有算例和数学规则为基线。
合作者可据此完成生命周期实现和运行输入；这是一份实施契约，**不是已运行结果**。
机读参数与预期见 [JENS 短验收契约](../../validation/gravity/results/o7-resume-20261004/jens-short-contract.json)。
运行参数与输出尚未开放完整 JENS 链路；已推送的内部 CPU tree fixture 可继续使用，
完成生命周期及 Runtime/API 的真实接线后才生成完整有效 `.par`。
不另建初始化模型、科学默认表、测试工作流或 JENS 专用数值下限。

### 6.1 配置条件与复用算例

`jeans_cells` 仅在 `refine_var` 实际请求 JENS 约束时必填，有限且 ≥4。
只在 `plt_variables` 请求 JENS 时求诊断、不要求目标格数、不改变树或推进；
显式给出的合法但不适用值可保留并标明未消费。两处都关闭时不新增遍历、EOS 求值或同步。
三个通道均按主计划要求使用 self gravity；显式请求不可用能力应失败，不能警告后继续关闭。
注册表、条件解析、API、Studio 和 checkpoint 身份由同一负责人同时更新。

复用 `GravityBox`：Cartesian、所有活动面 periodic，IdealGas、单组分 gas，
`network_name=none`、`use_burn=false`、`use_diffusion=false`、`hydrostatic_radial=false`。
这是有解析闭合的制造材料，`gas_cv=1 erg/(g K)`、`temperature0=1 K`、
`rho0=1e7 g/cm³`、`gamma=1.6666666666666667`；三个扰动参数
`amplitude=temperature_amplitude=velocity0=0`。不把此比热或目标格数推荐为用户默认。
显式设置已有 `width=0.1 cm`、活动轴中心为各域中点；不依赖 Setup 的数值回退。
`sml_rho=1e-12`、`min_eint=1e-10`、`max_eint=1e21` 沿现有算例，状态远离兜底边界。
HLLC／MUSCL／MC／RK3、`cfl=0.2`、共享 CGS G、原 `gravity_rtol=1e-10`、
`gravity_atol=0`，其余有效标准键从当前完整算例渲染并保存身份。

| 维数 | 活动域长度 cm | 根块 (x1,x2,x3) | 每块活动轴格数 | L1 叶块／单元数 |
| --- | --- | --- | --- | --- |
| 1D | 1 | (4,0,0) | 16 | 8／128 |
| 2D | (1,0.25) | (4,1,0) | 16 | 16／4096 |
| 3D | (1,0.25,0.25) | (4,1,1) | 16 | 32／131072 |

活动轴下界均为 0；所有活动物理间距为 `h0=1/64 cm`，L1 为 `h1=1/128 cm`。
独立十进制高精度计算给出 `c_s²=10/9 cm²/s²`、
`lambda_J≈2.286919195481633 cm`、`N0≈146.3628285108245`、
`N1≈292.7256570216490`；运行参考须使用实际输入 double 的 gamma 闭合，
不能用表中舍入值验证 16 epsilon 门槛。

### 6.2 事务、终点和负例

1. **触发与维持：** `jeans_cells=160`，`lrefinemin=0`、`lrefinemax=1`、
   `max_blocks=64`。均匀态没有曲率触发，JENS 在第一次演化前独立触发全部细化，
   分别得到表中 8／16／32 个 L1 叶块。实际 staged 事务保留 4 个旧根块直到发布，
   峰值至少为 12／20／36 个槽位，不能把叶块数当容量；64 是本短包明确的容器容量。
   每个接受宏步重新确认全部叶单元满足目标；
   候选父 N0<160，后续不得粗化。`regrid_interval=2` 不得推迟安全细化。
2. **允许粗化：** 在已有 C++ AMR 事务 fixture 中建立同一 L1 状态，目标改为 64；
   父 N0>64 且所有其他条件允许时回到 L0。复用真实 restriction／父 EOS，
   不添加 `initial_refine` 算例参数，不用临时用户 BC 或修改初始函数来制造树。
3. **失败行为：** 同一目标 160、`lrefinemax=0`，或者不能容纳完整细化事务的
   `max_blocks`，必须报告欠分辨／资源原因并拒绝演化；如容量值先被现有配置检查
   拒绝，另在事务 fixture 验证内部容量失败。禁止部分发布、降低目标或继续推进。
4. **三通道与续算：** 关闭／仅输出／实际约束各自推进到 `tmax=0.02 s`、
   `max_steps=-1`；关闭和仅输出用同一 DENS 指标及原曲率阈值，树保持 L0。
   约束通道树为 L1，步数可不同，比较同一物理终点。
   已接受状态 checkpoint 的物理时间固定为 0.01 s，再续算至 0.02 s；
   与该通道的连续运行比较。启用的新控制和几何语义进入原有身份检查。
   输出间隔 `plt_dt=0.01`、`chk_dt=0.01`；记录真实 checkpoint 时间，
   不能用文件名或输出请求冒充已到达分割时刻。
5. **等号与非均匀样本：** 叶函数 fixture 单独验证实际 FP64 值与目标完全相等
   时允许保持／候选父粗化，邻近欠／已解析点按严格比较判定。
   本均匀包不代替已批准的反向速度、多材料父态及非零引力 `JeansWave` 参考。

### 6.3 科学出口与收束

总正密度仍用于 Jeans 长度。泊松周期源使用扣均值密度；本均匀态的该源为零，
零均值势规范下解析 `Phi=g=0`，温度、压力、密度和静止速度保持常量。
两种密度用途必须同时成立，否则本样本不能在无引力场时触发细化。
静态良态诊断沿已批准 16 epsilon；动态 EOS、守恒、势力各用各自门槛，
不能把 16 epsilon 作为所有轨迹字段误差。

本包冻结相对质量与总能量漂移 ≤1e-12，按长精度体积归约、初始正量归一化；
依据是短时均匀守恒态、二进制精确的等体积层级和既有守恒预算，**不依据试跑误差拟合**。
全程必须有限、正密度、可解析内能且 `state_repairs.events=0`；
各次 Poisson 求解仍须通过原始物理 residual/target 和场租约检查。
均匀的周期零源检查 Phi／g 的零解及静止态，不能只检查净力相消。
如实际浮点路径不能保留零源，提交源／归约误差分析，不能自己加绝对重力 floor。
同通道续算沿现有严格 checkpoint／状态对照；跨后端允许既有归约预算，
不得把 CPU/GPU 相同结果当作独立参考。

把本包作为现有 gravity campaign 的一个有界子组，复用 `BoxCampaign` 的
有效输入、native volume、repair 和 solve trace 读取；仅需补 JENS／树及时间检查。
保留非均匀 JeansWave 的原密度、力、守恒、时空收敛 gate，
不叠加一套长期全矩阵，也不为本包新增 CI job。
CPU 子组通过后统一做受影响 CUDA；此短包不授权非线性碎裂或长轨迹结论，
其最终输入／EOS／源码／build／binary 身份和简短指标需回交维护者 review。

## 7. RZ 有限环体边界：已定数学部件与误差接口

本节点允许合作者实现以下共享数学与有界参考检查；生产能力声明、近场误差界
和完整科学验收分别交付。优先扩展现有 `GravityBoundary`、十分量矩、树／工作区
及 `CompositePoisson` 边界系数所有者，CPU/device 共用公式。
不另造源项函数、椭圆求解器、重力常数、softening 或面向用户的积分精度参数。

### 7.1 源、观察点和矩

源为每个叶单元 `[r_lo,r_hi]×[z_lo,z_hi]` 上的常密度，绕轴完整转动；
允许椭圆数学 fixture 的非负源，真实流体仍按既有正密度契约。
所求是现有边界 `face.center` 的势点值。当前 Phi 未知量为几何 cell center 点值，
不是 cell/face 平均势；此节点不改未知量、不增加独立直接重力加速度路径。
轴上的 source/observer 坐标使用连续解析极限，不能用“小半径阈值”替换轴定义。

单叶矩的中心取三维 `(0,0,z_c)`，`z_c=(z_lo+z_hi)/2`，不是环截面的 `(r_mid,0,z_c)`：

$$
M=\rho\pi(r_{hi}^2-r_{lo}^2)\Delta z,\qquad
I_{xx}=I_{yy}=\frac{M(r_{hi}^2+r_{lo}^2)}4,\qquad
I_{zz}=\frac{M\Delta z^2}{12}.
$$

该单叶关于中心的 dipole 和奇阶矩为零；父节点须用原有平移／聚合规则，
不能假设任意密度分布的父节点仍有该对称性。
支持球半径必须包含完整圆周，例如
`a=sqrt(max(r_hi)^2+max(abs(z_edge-z_c))^2)`；
不能把原生 r-z 矩形的半对角线误当三维环体支持半径。
保留原始二阶矩存储，按现有 quadrupole 公式消费，不重复保存另一组有漂移风险的矩。

对非负源、observer 到节点中心距离 `R>a`、`q=a/R`，二阶展开势余项满足

$$
e_{far}\le \frac{GM}{R}\frac{q^3}{1-q}.
$$

仅已证明中心反演对称的单叶可用 `GM/R*q^4/(1-q²)`；一般父节点继续用三阶起的上界。
测试若使用带符号制造源，应以绝对质量积分构造界，不能用相消后的 M。
几何 opening 是遍历候选条件；误差账本不允许通过时继续下树／近场，
不能只依据一个示例的实际误差宣布全源树达到精度。

### 7.2 近场与接触

令观察点为 `(R_o,Z_o)`，积分变量为 `(r',z')`：

$$
s^2=(R_o+r')^2+(Z_o-z')^2,\quad d^2=(R_o-r')^2+(Z_o-z')^2,\quad
m=\frac{4R_or'}{s^2},\quad
\Phi=-4G\rho\int\!\int\frac{r'K(m)}s\,dr'\,dz'.
$$

这里 K 的参数是 `m=k²`；库与 [NIST DLMF 的模数 k 定义](https://dlmf.nist.gov/19.2#E8)
换算必须显式。现有独立参考采用 [AGM 关系](https://dlmf.nist.gov/19.8#E5)，
生产可复用合适的共享叶函数，但独立参考不得导入生产 kernel。
补参数 `1-m=d²/s²` 直接从距离计算，避免近接触时由 `1-4Rr/s²` 的相消误成零；
远场、补参数、轴极限的切换由数值域定义，不能加物理 softening。
`R_o=0` 采用原有有限环体轴参考对应的解析分支。

边界面可接触有质量的最外层单元；“仅源外参考通过”不覆盖这种真实输入。
批准在接触点所在的子矩形分割后用三角形 Duffy 映射：
`x=x_o+t[(1-u)a+u b]`，Jacobian 为 `t*abs(det(a,b))`，
高斯点取内部且不采奇点；退化三角形按几何消去，不插 epsilon。
对数奇性积分有限，但普通 Gauss 阶数差只是误差估计，**不是可靠上界的证明**。
近场返回值、势误差界／估计、状态及实际工作量；未提供可靠界的结果只能作为
有界实验或单独注明估计质量，不能在下述 certified 残差列中填成已满足。
工作／细分上限由算法内部拥有，不能到上限后静默宣布收敛。

### 7.3 分项账本与非循环 residual 判据

势误差单位为 cm²/s²，泊松 residual 单位为 s⁻²，不能直接比较数值。
复用实际离散边界组装 `b_hat=b_source+B*f_hat`：对逐面可靠势误差界 `e_f`，
按同一离散系数计算 `e_b=abs(B)*e_f`，包括汇总、系数与舍入的已知界；
沿当前原生体积 RMS 求 `E_b=norm_V(e_b)`。
近场积分、远场截断、FP64 求值／归约分别记录，不能把嵌套积分差和截断界混成一个“残差”。

现有求解 residual 检查继续是离散近似边界问题的检查。
**如要声明满足精确环体边界对应的原始用户 residual 请求，**必须同时满足下列
由三角不等式给出的更强条件，而不以尚未知的精确 RHS 反过来决定边界精度：

$$
T_{safe}=\max\{atol,\;rtol\max(0,\|b_{hat}\|_V-E_b)\},\qquad
\|A\Phi-b_{hat}\|_V+E_b\le T_{safe}.
$$

因为精确 RHS 的范数至少是 `max(0,norm(b_hat)-E_b)`，这个判据足以保证
对精确环体离散 RHS 的 residual 不超过原 `max(atol,rtol*norm(b_exact))`。
可在内部给边界总误差与代数 residual 各留一半预算；近／远／舍入在边界份额内分配，
求解器可用更严格的内部目标，原配置值和日志中的原始请求保持其含义。
这是事后可检查的不等式；不能把未认证的估计代入后仍称 certified。
`atol=0`、`T_safe=0` 且误差无法归零时细化参考计算或明确失败，不能暗中添加下限。
这一检查不是 Phi／力的误差界，不替代条件数、空间离散或连续物理参考。

### 7.4 合作者现在交付什么

1. 先提交 full-ring 矩、轴／离轴 kernel、支持球、平移和余项界的 CPU 数学子组，
   再接触积分；报告真实算法、最大工作量、失败状态和误差界依据。
2. 扩展现有独立轴线／离轴工具覆盖 matched piecewise-constant 源，精度与分区分别改变；
   轴线解析式保留，接触不得沿用当前明确拒绝 inside/contact 的离轴工具冒充已验收。
3. 原 RZ 二次制造势、Gauss／物理 residual 和现有空间收敛标准保持。
   生产近场、边界势、Poisson 解、面力分别记录；新的误差预算不得由观察值倒推。
4. tree density／AMR epoch 变化时按原身份失效；CPU 通过后统一 CUDA，
   显存、kernel 调度可不同，积分／矩／误差判据只有一套。

上述数学部件可以独立实现、推送 review，角动量实现不阻塞它们。
完整 RZ release 和新长跑仍须取得近场与势／力参考、角动量及全消费者短科学签收。

## 8. RZ 角动量：单一表示与贯通实施节点

Core 选择 **RZ 专用的角向代表量 `m_phi=J_cell/W`** 作为唯一角向状态，
沿用现有角向槽位和物理量纲；不另存一个可独立演化的 ell／J 数组。
`J_cell=m_phi*W`、`ell_cell=J_cell/V=r_bar*m_phi` 是派生量，
`r_bar=W/V`。质量、径向／轴向动量、总能量和 rho X 仍为原生体积平均。
这项选择是数值表示的决定；不代表现有 RZ finding 已关闭或完整科学验收通过。
生产能力继续受完整消费链、独立收敛／收支及可解析状态检查约束。

### 8.1 平均、重构与 EOS

m_phi 的量纲仍是 g/(cm² s)，但在 RZ 中不再标成 rho*u_phi 的普通体积平均。
物理代表速度仍由 `u_phi=m_phi/rho` 得到；现有 EOS／内能恢复消费一份代表状态，
不增加第二个热力学模型或双权威动量。
这个动能闭合是有限体积表示的离散近似，不能宣称恒等于任意子单元速度分布的平均动能。
光滑、可解析状态须证明二阶一致性，轴邻格和强旋流须单独检查；
无法解析的真实输入或候选父态拒绝／保留细化，不补热、不改总能量、不给旋流额外 floor。
初始化、用户物理边界与公共 primitive 接口仍给物理分量，由同一几何适配负责状态语义。

m_phi 的径向权是 `r*dV`。线性物理动量重构的对应平均位置为

$$
\widetilde r=\frac{\int r^2dV}{\int r dV}
=\frac34\frac{r_{hi}^4-r_{lo}^4}{r_{hi}^3-r_{lo}^3}.
$$

不要把它与体积平均位置 `r_bar=(2/3)*(r_hi³-r_lo³)/(r_hi²-r_lo²)`
或几何中点混为一谈；这些度量继续由 `GridMetrics` 单一所有者提供。
重构到真实面的位置后，现有 HLL／HLLC 等 Riemann 数学仍消费物理 primitive，
禁止把 J 或 ell 原样送入 Riemann solver、EOS、燃烧或扩散。
原有二阶重构／求积如不足以表达不同权矩，扩展其几何适配并证明一致性；
不能通过为某个 solver 单独加近似参数掩盖该问题。

例如 rho=1、`u_phi=Omega*r`、r=[1/2,1]、同 dz：
`r_bar=7/9`、`m_phi/Omega=45/56`；真实平均旋转动能为 `5*Omega²/16`，
代表闭合为 `2025*Omega²/6272`，差为 `65*Omega²/6272`。
该差来自亚单元分布，随细化应收敛；不能拿它当成热源或放宽 EOS 拒绝预算。
独立刚体旋转角动量参考是 `J=Omega*rho*pi*dz*(r_hi⁴-r_lo⁴)/2`。

### 8.2 一次力矩更新，沿用既有数学所有者

每个单元的权威方程为

$$
\frac{d(m_\phi W)}{dt}
=-\sum_f\int_f rF_{m_\phi}\,dA
+\int_{cell}rS_{m_\phi}\,dV.
$$

径向面力矩通量是 `2*pi*r_face²*dz*F_mphi`；轴向面是
`2*pi*integral(r²*F_mphi dr)`，不是 `2*pi*integral(r*F_mphi dr)`。
面通量可用已重构物理状态作矩一致求积；常值面通量可乘该面的径向一阶面积矩。
求积、方位应力和边界方向与原生面积共用，不能把 W 直接当面面积。

- Hydro 只移除 RZ 方位分量中已经被力矩散度吸收的 advective curvature source；
  径向压力／离心源、质量、径向／轴向动量、能量与组分通量保留其物理方程。
  `GeometricSources`／flux owner 使用一个 RZ 适配，不复制全部 solver 或三套源项。
- 外部角向作用和粘性方位应力分别纳入力矩收支。扩散使用原有应力数学，
  同一角动量通量表示后去除对应重复曲率项，不能只修 Hydro 而遗漏 viscosity。
  轴对称自引力无方位力，但径向／轴向力和原有能量功耦合仍须检查。
- Reflux 累计并修正同一有符号面力矩积分，最后除本单元 W。
  CPU/device、各时间积分阶段都消费同一权矩与转换叶函数；执行器只承担资源／kernel 差异。
- 轴面 r=0 的力矩通量严格为零。正则场 `u_phi=O(r)`，物理角向量为奇、
  派生 ell 为偶且为 O(r²)；反射 ghost 按原生几何映射取符号，
  不能把负半径 ghost 的公式直接当正体积，不能在轴上执行除以 r。

### 8.3 AMR、同一候选父态与不可解析的粗化

角向 restriction 为 `m_parent=sum(m_child*W_child)/W_parent`；
其余守恒分量继续按 V。prolongation 的角向偏差使加权 W 和为零，
其他偏差使加权 V 和为零；复用已有共同 theta 正值限制，
从可解析的 parent 基态混合，不能独立夹紧角向量而损失 J／总能量／rho X。
轴邻格的 regular profile 与代表闭合仍受第8.1节的重构及误差检查约束。

以下反例双方都可解析，证明候选父的正值验收不能省：
两个径向子格 [0,1/2]、[1/2,1]，rho=1，径向／轴向动量为零，
`m1=1,m2=-16`；各子比内能均为 1/16，因此 `E1=9/16,E2=2049/16`。
`V1:V2=1:3`、`W1:W2=1:7`，父态为
`m=-111/8,E=1539/16`，其比内能为 `-9/128`。
必须拒绝这次粗化并保留原细网格，不能改 E、J 或 floors 来构造父态。

同一候选父构造供正值检查、JENS、其他粗化指标与实际 migration 使用。
先检查可解析性，再 EOS/JENS；候选不可解析属于粗化 veto，
旧已接受状态损坏则明确失败，两者不能混成一个“可忽略错误”。
不在 JENS 中另造按 V 平均角向分量的父态副本。
已提交的旧内部 RZ 静态参考按新语义迁移、重新计算独立参考，不能复用旧 PASS 关闭该 finding；
Cartesian 及未变语义的其他几何保持其现有参考和路径。

### 8.4 消费链、短验收与发布出口

| 次序 | 合作者可立即实施／提交 | 独立检查与出口 |
| --- | --- | --- |
| A | 几何权矩、单一状态转换、restriction／prolongation 内部 CPU fixture | 分区后 W 总和、手工 J、刚体／变旋流、上述不可解析父 veto；同时保持 rho/E/rho X 收支 |
| B | Hydro 面力矩、几何源、reflux、轴线与已有 viscosity 适配 | 闭域无力矩、受边界／外源力矩两类收支；混合层级多次事务，不能只比较最终 parent |
| C | Init／BC、EOS/声速/JENS、plot／API／checkpoint 语义接线 | 代表量、速度、J/V 与 native measure 可辨；状态语义版本升级、旧 RZ checkpoint 明确拒绝 |
| D | 受影响 CPU 科学子组、再统一 CUDA | 独立径向平衡／旋转光滑解保持既有至少1.8的空间收敛目标；可解析域、能量和角动量各自验收 |

闭域与有力矩的离散守恒检查，使用
`abs(delta_J + outward_torque_impulse - applied_torque_impulse)`，
以初始 `sum(abs(m_phi)*W)` 和外部／边界绝对力矩冲量之和归一化，短组门槛为 1e-12。
该门槛来自既有守恒级别和短事务浮点收支，不按先前 5e-4／1e-4 实测漂移拟合。
归一化量为零的零旋转/零力矩 fixture 要保持零角动量；不添加 tiny 分母。
质量、总能量、rho X 的原有守恒预算及未触发修复条件分别保留。
空间收敛覆盖热压支撑的刚体旋转平衡 `P=P0+rho*Omega²*r²/2`，
独立亚单元积分与 native 面力；单靠总 J 守恒不能签收局部力或 EOS 闭合。
高 Mach／轴邻格若达舍入或表示下限，回交误差／域分析，不能擅改门槛。

所有权仍为原 `GridMetrics`、`FluidState` 的转换消费、`GeometricSources`、
AMR transfer／flux surface、EOS bridge 和 IO；完整职责才决定是否需要新小文件。
新增注释说明 Workflow、权矩公式、状态语义和子函数用途；公共用户仍只需两个 include 头文件。
API/plot 中单位相同不代表平均语义相同，writer/reader/Inspector 同批标明并验证；
旧极平面和新 RZ identity 不兼容，不建静默兼容层。

可以先推送 A 或 B 供科学 review，独立推进第7节环体和第6节 JENS。
完整消费链通过前保持生产 RZ gate；新 RZ 长轨迹与性能计时在相应短科学签收之后。

本轮 Core 复核、DPS 调用边界与已知费用见 [精简复核记录](../../validation/gravity/results/o7-resume-20261004/core-nodes-review.json)。源码／编译／实际科学结果与规划参数分开标记，当前没有新的完整轨迹通过声明。
