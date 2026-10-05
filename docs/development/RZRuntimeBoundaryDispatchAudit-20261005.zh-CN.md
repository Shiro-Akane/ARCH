# RZ Runtime 边界分派审计

基线2c166b9aaeeb2f5d544643dd6182ed5c4d78361f，唯一工作区，clean。
只读审计，没有运行新的RZ Preview/演化，没有更改科学实现或门槛。

## RZ-BOUNDARY-DISPATCH-01：未贯通的实际消费者

src/physics/gravity/GravityExecution.h的EvaluateBoundary只接dimension、geometry，
没有GeometrySemantics，也没有有限环体源/预算/误差账本身份。
其operator()在dimension==2时无条件调用isolated_log_potential。
src/physics/gravity/self/SelfGravity.cpp的prepare原调用直接传op.base().dimension；
RZ实际dimension=2，故移除bind gate后会落入旧极平面二维对数势，
即便GravityBoundary本身已识别finite_ring且Workspace观察点已转换(r,0,z)，
也不能得到批准的三维完整环体Newton边界。

这不是当前生产运行已产生错误场：SelfGravity::bind在构造workspace前明确拒绝RZ，
原self_gravity_lifecycle还验证拒绝/旧publication退役/Cartesian恢复。
该finding是解除gate前必须完成的消费链要求，不是修改维数为3的许可；
RZ仍为二维原生r-z网格，不能伪装三维拓扑或调用旧point-mass近场。

## 所有者与下一步

- GravityBoundary拥有真实ring/source/root-coordinate/generation与有界积分；
  重用已有ring_boundary及canonical RHS/native residual账本，不复制另一套数学。
- GravityExecution/SelfGravity拥有typed边界消费分派，显式区分Existing与AxisymmetricRz。
  RZ不得进入旧isolated_log_potential；未知chart/不可靠近场预算明确失败。
- GravityWorkspace保持原生area/V、coarse-fine面归并与独立force/work系数。
  首先将原face-row组装作为同一owner的可测producer，
  在现有真实axis/off-axis root/mixed binding检查原制造势的梯度/力/功映射。
  不通过绕过bind gate创建可发布的RZ场。
- 然后将真实current全块density/stage/topology身份接入finite-ring→RHS→solve→
  gradient→force/work→publication，并逐项验证旧epoch/version失败及恢复。
  原configured residual/连续参考/角动量/axis/viscosity科学出口仍单独保留。

现有3584-cell测试只覆盖tree chart/volume/width/observer与拒绝路径，
没有进入face-row、CellAcceleration、物理mass-flux work或真实finite-ring Runtime。
不能把CPU/CUDA原Cartesian/一维径向科学子组当RZ消费链证据。

## 验收边界

原清单§7/§8批准的源定义、native measure、共享G和误差判据不变。
完整continuous Phi/force和原至少1.8空间要求、角动量收支、所有消费者签收前
保持生产能力gate，不新增数值floor或科学阈值。当前只关闭审计定位工作，
RZ-BOUNDARY-DISPATCH-01本身保持OPEN，不宣称任何RZ科学运行PASS。
