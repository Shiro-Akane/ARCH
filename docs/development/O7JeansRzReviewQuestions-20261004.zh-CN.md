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
