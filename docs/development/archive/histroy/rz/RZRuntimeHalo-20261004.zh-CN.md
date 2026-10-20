# RZ 实际 DriverRuntime Host halo 接线

基线 ca78d468a1db3d832c888cdc696bdd19212a6f51。Runtime 从已有BC profile
固定其内部GeometrySemantics；初始topology adoption和后续Host Current halo
refresh给真实GhostExchange传RzAxisymmetric。全部patch先验证boundary计划，
再进入OpenMP操作和ledger发布。旧默认profile不改变。

device backend绑定/安装明确拒绝尚未迁移的RZ；生产regrid同样提前拒绝。
这些是防止旧chart静默执行的过渡保护，不是完成device/regrid功能。
不能将当前Runtime宣称为完整生产RZ模式。

## 实际 CPU 检查

新增test_rz_runtime_boundary.cpp和可复现runner：
validation/amr/run_rz_runtime_boundary.py --build build-cpu --output-root <新本地目录>。
runner采用已有可信compile/link命令，重新编译fixture、DriverRuntime、
DriverBoundary、DriverRegrid四个translation units，链接现有CPU依赖archive；
不configure，不重新构建ARCH executable，不运行simulation。
此证据不是完整新production binary身份；所有重新编译源码及fixture SHA见Summary。

四个实际五叶块mixed AMR案例覆盖径向/轴向接口、轴域r_min=0/非轴域r_min=1。
真实initialize_topology后验证Current interior和ghost可读；轴r/phi反号、z保持，
非轴outflow保持；人工破坏ghost再ensure_fluid_ghosts，真实恢复。
刷新不改变interior version，保持version1。实际device binding/regrid拒绝也已验证，
未改变version；time0/step0。该fixture检查映射，不把任意常量轴动量称为物理解。

fixture4/4 PASS；scheduler/boundary/curvilinear scoped3/3 PASS。
原始编译/执行日志保存在ignored studio/.local/integration/rz-runtime-halo-20261004。
未生成H5/plt/checkpoint。下一步继续CFL/diffusion调用点、公共几何配置及身份接线；
重网格需统一GeometrySemantics且解决已上报角动量传递finding后再开放。
