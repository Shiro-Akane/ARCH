# RZ Block AMR 测度接线与角动量 finding

## 实现范围
Block::InterpolateFromCoarse / AverageToCoarse 显式接收 GeometrySemantics；
验证 parent/child GeometryView，复用既有 prolong_family / restrict_family。
coarse/fine volume 都取对应完整环体 CellVolume；未增加第二套插值或改变 floors/species policy。
Host GhostExchange 的 coarse-fine source weights 使用同一 chart，
ExecuteExchange 传递 chart；默认 Existing 保留旧 Grid/polar 调用。

这是内部适配，不切换 production runtime Grid，不开放 RZ capability。

## 可复现检查
tests/host/amr/test_amr_operation_plans.cpp::test_rz_regrid_roundtrip：
r=[0,1] 与 [1,2]，z=[-0.5,0.5]；
一个16×16 parent → 四个16×16 child → 一个恢复 parent，实际 Block owners 执行。
rho=2+0.1r+0.2z，mom_r=0.1rho、mom_z=0.2rho、mom_phi=2rho*r，
eng=100rho、enuc_rate=0.3rho、X0=0.6+0.01r、X1=1-X0。
parent ghost 为解析夹具，不是演化边界验收。

独立 long-double 体积 pi*(hi²-lo²)*dz。
比较 rho、三个动量、eng、enuc_rate、rho*X0、rho*X1 的积分；
归一化分母 max(1,abs(parent integral))，不是全部积分的严格相对误差。
axis/off-axis 最大归一化误差分别 3.5563253182938023e-17 / 2.4968065488241603e-17。
1e-12 是保守传递的工程算术检查，不是新科学预算。

最后补直接 array 依赖后，受影响两个 CPU target 定向重编译，
curvilinear_metrics / amr_operation_plans 2/2 PASS；git diff --check PASS。
源码/hash、真实 test ELF 和数值见 Summary.json；未完整构建 ARCH。
日志、旧中心近似测量及最终精确测度日志留 ignored studio/.local/integration/rz-block-transfer-20261004。

## 科学 finding：细化不保留角动量
mom_phi 的体积积分通过，不能推导 Lz 守恒。
对当前 piecewise-constant cell momentum_phi 使用真实径向一阶测度：
Lz=sum[mom_phi*(2/3)*pi*(hi³-lo³)*dz]。
没有使用 r_mid*V，也没有修改状态变量或人为放宽阈值。

| r domain | parent Lz | refined Lz | restored Lz | refinement relative change |
| --- | --- | --- | --- | --- |
| [0,1] | 6.52565014546476 | 6.5289032204505819 | 6.5256501454647609 | 0.00049850588267931159 |
| [1,2] | 102.00961373742267 | 102.02021879603544 | 102.00961373742267 | 0.00010396136426979989 |

粗化恢复不能抵销 refined state 在演化期间的误差。此 finding 未关闭；
CTest PASS 只证明所列工程断言，测试有意输出 angular 差值，不把其标为通过。
当前既有 transfer 约束是体积加权的 momentum_phi，不是 r 加权角动量；
新误差要求 Core 确认 authoritative state/discrete transfer 与预算。
不能自行选择 r*mom_phi 状态替换、重分配修正或放宽验收阈值。

## 后续 gate
请 Core 明确：
- authoritative angular state 与单元平均约定（momentum_phi 或 angular density）；
- refinement/restriction 必须保留的矩与其 positivity/energy/species 兼容策略；
- 独立参考和 angular error budget。
本增量提交实现、processed finding 与身份供 review，不表示完整 O7 RZ AMR 通过。
真实 regrid/reflux/time evolution、IO/checkpoint 语义、CUDA 和整体科学出口仍未完成。
原始 H5/plt/checkpoint 不提交；本轮未运行 simulation、未 push/tag/main merge。
