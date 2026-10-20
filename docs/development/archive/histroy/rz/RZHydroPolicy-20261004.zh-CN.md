# RZ Host Hydro policy 接线

基线 a35f70acaa7aca9cac69e9eb775222b942000551。

真实 HydroSolverImpl 原先始终调用 Existing chart。现增加构造时固定的内部
GeometrySemantics，evaluate_patch 和 update_patch 同时传给共享 helper；
IHydroSolver 提供 chart identity 查询，未迁移的实现默认报告 Existing。
未知枚举拒绝，既有默认构造保持旧行为。此处没有新增 public config 开关。

四个混合 AMR fixture（radial/axial 接口、r_min=0/1）改经 IHydroSolver
虚接口执行真实 HLLC/PCM evaluate/update，保持五叶块、两组分、常态平衡、
边界/exchange/reflux 的原有检查。默认 profile identity 和未知 profile 拒绝也已检查。
仍沿用已有算术回归门槛，不新增科学误差预算。

CPU scoped 编译成功；curvilinear_metrics / boundary_plan / amr_operation_plans
3/3 PASS。完整日志本地保留于 studio/.local/integration/rz-hydro-policy-20261004；
处理后的数值见同名 Summary.json。未运行 simulation，未生成科学输出。

这一步补齐真实类型擦除 policy，不代表 Euler/RK 时间调度已迁移。
下一步需要 preflight 核对 Hydro/BC chart，并给 scheduler callback 的
exchange/reflux 传同一 profile，验证 ledger/slot rotation。公共 runtime、
checkpoint 语义、角动量 AMR transfer finding、有限环体 gravity 和 CUDA 仍未完成。
