
# RZ 外源最小决策包
状态：待 Core 确认。Finding RZ-EXT-TORQUE-GATE-01 未关闭。
代码/历史证据基线 ccfcef5bc60f4361a808dc1e83c020094ab50930；本包未重跑科学测试。

## 现有行为
HydroGeometryBinding 的 RZ gravity guard 要求 finite-ring current-state 来源；
真实 ExternalGravity(0,0,-0.025) 在演化前拒绝，stage=0，26880 个 Current/ghost word 不变。
这证明拒绝与事务保护，不能证明外源演化通过。见 RZAppliedTorqueGateFinding-20261005.zh-CN.md。

## 候选与职责
Studio/实施方在原 IGravityPolicy 所有者增加来源类型及坐标语义：
unknown（默认拒绝）、external-native-orthonormal、finite-ring-current-state。
不以类名、文件名或 model whitelist 推断来源；不把外源改称自引力。
Core 确认原生 RZ 代表态下 g_r/g_z/g_phi 的意义及功项，之后再贯通绑定/阶段账本。
未知来源、错误 chart、旧 epoch 必须拒绝且不部分发布。

## 独立参考与验收候选
复用原 24 组合：radial/axial × closed/outflow × g_phi=±0.025 × Euler/RK2/RK3，
dt=1e-4，10 步。J=sum(m_phi W)，源力矩积分与阶段权重独立计算；
动能功按相同阶段态 m·g 的 V 积分，不用同生产 kernel 互证。
保持原 normalized J budget 1e-12，以及质量/能量/rhoX/repair=0 账本。
保留漏源项/错误 RK 权重反例；开放边界明确扣除边界力矩与能量通量。

## 待 Core 确认
1. external 来源类型与 native orthonormal chart 是否可作为正式契约。
2. 角向代表态到源力矩/能量功的精确映射及时间阶段定义。
3. 原 24 组合输入和上述原预算的适用范围。
确认前仅整理候选工具，不解除 public RZ gate。通过本项不代表其余三项通过。
