# RZ Host Hydro 组合与状态修复计量

## 接线
TimeIntegratorHelper 的 add_geometric_sources、evaluate_all_dimensions、
perform_stage_update 显式携带同一内部 GeometrySemantics。
全维扫描仍用原有 FluxSchemePolicy / FluxTraversal / reconstruction / EOS / limiter，
没有复制 Riemann 数学或改变 state update/floor/composition policy。
散度的完整环体面/体积与 cylindrical r/z/phi source 使用同一 view。
stage repair 的 affected-volume / mass / momentum / energy / species budget
取该 view 的原生 CellVolume，避免 RZ 状态却报告旧二维扇区测度。

默认 Existing 保留；production IHydroSolver/runtime Grid 尚未切换。
显式 RZ + gravity 或 AMRControl 在任何输出清空/cache 重置前明确拒绝，
避免已接线 operator 误消费 legacy gravity/reflux 语义。
这是暂时的内部 guard，不是最终 RZ 支持域；完整计划仍要求迁移这些消费者。

## 真实 CPU 执行
真实 HLLC<PCM>、IdealGas、SpeciesManager、padded Grid 和共享 Host traversal。
r=[0,1]/[1,2]，z=[-0.5,0.5]，各16×16；rho=2，vz=3，p=5，
轴线无旋流、非轴线无旋流、非轴线 vphi=2，共768 active cells。
能量包含三分量动能；constant ghost 由夹具提供，不能替代真实边界验收。

独立解析：
- radial pressure divergence 与 p/r source 抵消；
- 剩余 delta mom_r = dt*rho*vphi²*2/(r_lo+r_hi)，dt=.001；
- 其它 conservative/species delta 为0，z 不冒充 polar angular direction。
最大 radial error 约1.4e-17，沿用现有 close 的2e-12工程检查。
一次共享 stage_update 保留 axial momentum/composition，不产生 repair。

另用既有密度 floor .5→1 的人工反例，仅核对预算测度：
每组256事件，affected volume=pi*((inner+1)²-inner²)，signed mass=.5*volume；
无科学场景、物理 floor 或验收阈值变更。
AMRControl guard 反例验证拒绝发生前 delta sentinel 未改动。

## 检查与证据
初次编译遗漏测试 AMRControl 的必需构造参数；使用真实(4,2)夹具修正，
错误日志留本机，没有绕过该反例。
curvilinear_metrics / amr_operation_plans 2/2 PASS，git diff --check PASS。
精确 input/base/test ELF SHA、stream 输出精度说明见 Summary。
完整日志留 ignored studio/.local/integration/rz-host-hydro-20261004。
未独立 configure、未完整 ARCH build、未运行 simulation/CUDA、未 push/tag/main merge。

## 尚未完成
物理 axis BC / AMR reflux / gravity / hydro dt / scheduler / public runtime identity，
其它 flux-reconstruction-EOS 组合及完整时间演化，仍须 CPU 全路径验收。
RZBlockTransfer 的 refinement angular momentum finding 保持开放；
本次 constant-state stage witness 不会清除该 finding。
IO/checkpoint 语义与 CUDA、冻结参考/预算以及 O9 出口均未完成。
