# RZ-A 前置数学节点：W 测度与单一状态转换

依据 Core 86bec324018349c6d81df84a3cced3ed9f9a2792 科学清单第8节。
本提交只交付 A 的前置共享几何和转换，**不称 A 全部完成**，不关闭角动量 finding。

## 唯一职责

GridMetrics::Rz 提供 W=integral r dV、V centroid、W reconstruction centroid、
径向及轴向 torque face measure。半径比归一化避免求差相邻幂及仅计算长度时形成大平方。
FluidState 的无存储 scalar leaves 提供 J=m_phi W、ell=J/V、u_phi=m_phi/rho；
未创建第二份权威 J/ell 数组，没有改变现有 FluidState 布局。

此时 production/内部已有 transfer、hydro/viscous/reflux 等消费者尚未接入 W；
已有 RZ 动量仍是旧语义。因此不能拿新转换计算旧状态并宣称 Current 或支持完整RZ。

## 独立参考与检查

现有 arch_curvilinear_metrics target 及 curvilinear_metrics ctest PASS，
真实源树 architecture audit / diff check PASS。
实测固体转动 rho=Omega=1、r=[1/2,1]、dz=1：
V=3pi/4、W=7pi/12、rbar=7/9、rtilde=45/56、
m_phi=45/56、J=15pi/32、ell=5/8。
axis 径向 torque 精确零；轴向 torque=7pi/12，不能替换为普通 annulus area。
测试含轴单元、非轴单元及1e10半径的径向/轴向 W partition。

冻结父态反例的算术参考：
W1:W2=1:7、V1:V2=1:3、m_parent=-111/8、
E_parent=1539/16、internal=-9/128。
此处只验证独立算术，不伪装为真实 AMR veto 测试；实际 transfer 迁移后要走同一父态
admissibility→EOS/JENS→coarsen candidate，再验证事务不动 fine state/E/J。

## 后续节点顺序

A余下：真实 restrict/prolong 的 m_phi W 与其他量 V 分离，
共同 theta 保守正性、同一候选父态和旧RZ静态参考重算。
B：hydro torque/source/reflux/axis/viscous 全贯通。
C：Init/BC/EOS/sound/JENS/plot/API/checkpoint 意义及旧RZ checkpoint拒绝。
D：独立CPU科学和角动量1e-12 gate，然后CUDA和已冻结对应长跑。
有限环体第7节共享部件可按依赖独立实施，不影响角动量A→D顺序。
不改科学阈值，不进行新RZ长跑，不开展Windows适配。
