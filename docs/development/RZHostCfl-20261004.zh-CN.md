# RZ Host CFL 与非法活动单元传播

## 接线与真实发现
DriverUtils::adaptive_dt 增显式 GeometrySemantics，patch 开始验证一次 GeometryView；
既有 per-cell EOS/声学表达式与 shared minimum/逻辑 key/并行归约不变。
RZ active axes 为 r/z，消费 dr/dz，不使用 polar r*dphi。
phi 没有 active derivative，不直接进入 acoustic transport sum；EOS 仍包含完整动能。
公开 runtime/scheduler 未切换，默认 Existing 保持 chart。

新单 NaN active density 反例先真实失败：其它正常 cells 仍给有限 dt。
共享 minimum 的 Ignore-NaN 是归约契约，不是物理输入有效性判定。
Host 的遍历区间全部为 active cells，因此在上游明确检查：
- rho 必须正且有限；
- EOS/CFL candidate 必须正且有限；
错误携带本地 cell index，经 HostFailure 跨 OpenMP 区域传播。
没有改 ReductionSpec 的 Ignore/Error 规则、物理声速或 CFL 系数。

## CPU 工程证据
真实 IdealGas 单组分，gamma1.4/rho2/p5，完整动能记录。
两个16×16网格 r=[0,1]/[1,2]，z=[-1,1]，dr=1/16、dz=2/16；
vr=.1+.2r、vz=-.5+.3z，phi=0/2，CFL=.4。
独立解析每cell候选：
.5*CFL/[(abs(vr)+sqrt(gamma*p/rho))/dr+(abs(vz)+sqrt(gamma*p/rho))/dz]；
取 active cells 最小值。四组最大 absolute error 4.33681e-19，
串/并行 binary64 exact一致。沿用已有2e-12工程close，无新科学预算。

旧 chart 反例单cell rho=0/-1/NaN/Inf、momentum=Inf 均在串/并行拒绝；
原 D2 36 个 reduction edge cases 通过，default 正常state dt bits 保持。
CPU curvilinear_metrics / reduction_contract 2/2 PASS；diff check PASS。
首次失败 ctest.log、最终 build/ctest 留本机 ignored .local，
基线/dirty input/test ELF SHA 及实际 stream 精度见 Summary.json。

## 未完成出口
这是 acoustic transport candidate，不能声称独立的强旋流几何源稳定性或长期 RZ 验收。
RZ boundary/regrid/reflux/gravity/scheduler/public identity/IO/checkpoint 尚需贯通；
AMR angular finding、O7.1和有限环体科学 gates 保持开放。
CUDA active-cell invalid传播须在 CPU完成后统一对齐，本次不声称跨backend一致。
未运行 simulation、未完整 ARCH build、未更改物理预算、未 push/tag/main merge。
