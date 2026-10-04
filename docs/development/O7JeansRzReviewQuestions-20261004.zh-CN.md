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
| JeansWave/GravityBox集成/接受宏步/regrid/restart | 复用正式初始化、当前真实AMR和checkpoint，不复制物理 | Jeans原mass/energy/force/residual预算沿run_self_gravity；不能当作JENS新非线性误差预算 | 指定冻结jeans_cells、amr历程、终点和独立诊断参考；未给列保持pending |

## RZ子例/参考/预算（待逐项review）

| 子例 | 可复用证据/语义 | 需确认的科学列 |
| --- | --- | --- |
| 制造解/轴线/混合层级 | RZCompositePoisson，Phi=a*r²+b*z²，A=-L；完整环体volume；operator点值与平均RHS明确区分 | 保留原测试budget，Corereview适用源/势/面力语义与覆盖；残差非独立势力验收 |
| 球对称映射至RZ | 沿第4.3节独立两向力/axis参考，不以同生产kernel互证 | 指定有限源、域/边界、点值或体积平均、误差weight与阈值 |
| 有限环体轴线/离轴 | RZFiniteRingAxis/OffAxisReference：独立高精度积分，observer点值；并非cell/face平均 | 是否采用piecewise-constant有限源，inside/contact路线及平均语义 |
| 近场积分 | 近外缘整体64→分区64 g_r差1.9753441487098356e-15 cm/s²；直接Newton与同阶核一致仍有共享源积分误差 | 分区/阶数/接触处理与源积分误差预算；不能由当前数值倒推“够准确” |
| 远场矩/树聚合 | 完整环体monopole+traceless quadrupole候选；示例Phi误差7.558246644870839e-16 cm²/s² | 最终阶数/opening/聚合语义及截断预算，示例不是冻结值 |
| 边界/离散/AMR/MG/backend | 复用现有GravityBoundary tree/workspace与shared FGMRES | 第4.3节分项误差预算：边界截断、源积分、离散、AMR、MG、后端舍入，逐项给出处/阈值 |
| angular momentum transfer | RZBlockTransfer：保守mom_phi体积分不等于真实r-weighted Lz；axis变化4.9850588267931159e-4，offaxis1.039613642698e-4 | 原生角动量状态/传递语义、能量/组分约束和预算；恢复parent不消除演化影响 |
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
| 候选父态 | 先以真实物理体积 restriction 守恒量，再由父态 EOS 求声速 | 禁止平均子声速、子 N_J 或子温度来替代父态；子组分按 rho X 迁移 |
| threshold | N_J<jeans_cells 请求细化；父态 N_J>=jeans_cells 且其他指标及事务全部允许才可粗化 | 用实际 FP64 比较，明确等号行为；不插入降低目标的 epsilon、不加新参数；临界舍入样本与明确欠／已解析样本分开 |

父态独立样本使用单／双组分、不同密度和相反速度的手工守恒子态，
独立 long-double／Decimal 汇总 rho、动量、E、rho X 的体积分，
再按该独立 caloric law 求父态内能和声速。相反速度的平均动量可为零，
粗化后保留总能量意味着未解析动能进入父态内能，不能人为减掉该能量。
不同径向位置使用真实环体体积；几何／AMR owner 继续只有一套。
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
请提交一份共用的 RZ 角动量离散设计，明确权威状态与存储／单元平均语义，
列出 hydro 几何源、面通量、prolong/restrict、reflux、EOS 动能、轴线奇偶性、
IO/checkpoint、CPU/device 的贯通关系。可讨论角动量密度作为权威量或相容表示，
但不能直接把状态改成 r*m_phi 而略去这些消费方，也不额外保存可漂移的双权威状态。
维持质量、总能量、rho X 与正值处理的既定约束；独立恒定旋流／变旋流反例、
混合网格多次事务及有／无边界力矩的收支验收由设计一起提交。
该物理离散选择和预算确认后才进入对应源码修复，RZ 发布出口仍受此 finding 阻塞。

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
