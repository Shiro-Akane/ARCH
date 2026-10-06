# RZ 实际 Host Euler/RK 调度接线

基线 9bc86efe66bd95bbc7745208e07a541979ae020b。现由 Hydro policy 提供固定
内部 chart，BCHandler 报告其 chart。三个真实积分器在 Clear/阶段计算/发布以前
执行共享 preflight：拒绝缺失 Hydro、未知 chart、Hydro/BC 不匹配、RZ 无边界
身份或 preflight、错误 patch geometry，以及尚未获 authoritative contract 的
RZ gravity。Existing 默认行为和已有 stage weights/rotation/order 保持不变。

每个阶段的实际 ghost exchange 使用同一 RzAxisymmetric seam；最终实际 reflux
使用同一 GeometrySemantics。没有增加另一套 scheduler，也未改变数学公式。

## 实际验证

用五叶块 mixed AMR、两组分、轴域/非轴域、径向/轴向粗细接口，共12组
SolverEuler/SolverRK2/SolverRK3 真实入口；使用真实 StateResidencyLedger、
MonotonicSchedulerClock 和 ScopedStageBinding。检查原生 active state 常态平衡、
完整 chart 链路、最终 Current HostValid、ghost Invalid、版本3/4/5。
最大常态偏差 3.55618e-17，沿用原2e-12算术回归门槛，不是新科学预算。

错误 BC chart 在每个入口均拒绝；token/version、Current interior version
和预先累积的flux sentinel123保持不变，随后正确调用恢复成功。
初次4项检查中只有旧源码接线断言失败：它仅识别 ApplyReflux(dt)。
已更新为检查带同一 profile 的唯一 reflux callback 和 exchange binding；
原 scheduler authority、descriptor、rotation 检查全部保留。

受影响 scoped 4/4 PASS；增加flux拒绝检查后仅重编译/复验curvilinear_metrics，
1/1 PASS。完整日志保存在 ignored studio/.local/integration/rz-scheduled-hydro-20261004；
处理后的12组结果见同名Summary.json。不生成H5/plt/checkpoint，未运行simulation。

## 未覆盖与下一步

此处是真实Host积分调度接线，不等于公共RZ模式已完成。DriverRuntime 的初始
halo、CFL/重网格/diffusion、生产配置/API/IO/checkpoint语义仍需统一迁移。
角动量AMR传递finding、有限环体gravity、CUDA和冻结演化验收未因此解决。
