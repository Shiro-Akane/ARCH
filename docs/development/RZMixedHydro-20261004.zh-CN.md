# RZ mixed AMR Hydro 单阶段联动

## 最小实现
共享 evaluate_all_dimensions 把同一 GeometrySemantics 传给
RegisterCoarseFineFluxes。此前内部 RZ AMR guard 在真实 reflux/identity 接线后解除；
gravity consumer 未迁移，仍在任何输出/cache修改前明确拒绝。
未增加另一套 Hydro、重构、EOS、limiter 或 reflux 数学，
production runtime Grid/IHydroSolver/driver scheduler 未切换。

旧 guard fixture 更新为真实 IGravityPolicy stub：
RZ 明确抛错，dU sentinel 保持，source callback计数0。
这不是允许未迁移 gravity 进入新语义。

## 实际执行链路
四个真实 LoadLeafGrid 混合拓扑：
径向/轴向界面 × inner radius0/1，每组四L1+一L0，各16×16；
合计5120 active cells。两个IdealGas组分相同gamma1.4/Cv3，X=.6/.4。
常态 rho2、mom_r0、mom_z6、mom_phi0、eng21.5，即 p5、vz3、无旋流。
物理边界 ghost 是解析常态夹具，不能冒充生产边界生命周期。

完整调用：
BindActiveHandles → flux Clear → RZ ExecuteExchange（ordinary/coarse-fine/axis seam）
→ HLLC<PCM>全方向 → RZ divergence/source/registration
→ stage_update（w_old0/w_flux1/dt.001）
→ RZ ApplyReflux 到 state_next。
实际 staged conservative fields/composition 与初态独立常态参考比对。

| 界面 | inner r | max delta | max register imbalance | final max state error |
| --- | --- | --- | --- | --- |
| radial | 0 | 2.77556e-17 | 0 | 2.77556e-17 |
| radial | 1 | 4.95494e-17 | 3.14523e-15 | 3.55618e-17 |
| axial | 0 | 5.34464e-16 | 2.13163e-14 | 1.38778e-17 |
| axial | 1 | 5.29374e-16 | 2.84217e-14 | 1.38778e-17 |

沿用已有工程 close2e-12，没有新冻结科学阈值。
stage没有产生repair；registration identity明确为RZ。
CPU curvilinear_metrics/amr_operation_plans/amr_flux_surface_plan 3/3 PASS；
diff check PASS。精确源码/base/test ELF SHA与stream精度见Summary；
完整日志留 ignored studio/.local/integration/rz-mixed-hydro-20261004。

## 未完成与 next action
这证明 shared Host owners 的单阶段联动，不是完整生产 RZ 演化。
正式内部 Hydro policy binding、scheduler、物理BC、扩散/RKL AMR、
public source/chart identity、IO/checkpoint、gravity和CUDA仍未贯通。
非均匀/长期独立参考与预算、冻结O9输入仍待。
RZBlockTransfer的角动量细化finding保持开放；
无旋流常态不具备关闭角动量gate的覆盖范围。
未完整Build ARCH，未运行simulation，未 push/tag/main merge；raw输出不提交。
