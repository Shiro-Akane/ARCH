# RZ authoritative 初态坐标与 repair 测度接线

基线 acdf35574f96c1b5d9e840cdd028543b839d9095。
承接 RZNativeCoordinates 的 PopulateState 缺口。

## 实现
ProblemInitializationContext 增加 explicit geometry_semantics，默认 Existing。
真实 ProblemHelper::detail::PopulateState 在 callback/块写入前验证整个 chart 与各 native grid/config identity。
其 GetPhysicalCoords 与 repair CellVolume 同用 context profile；RZ 不再落回 polar z_cy=0 或单位方位角测度。
仍复用同一 InitialConservedState / EOS dispatch / state bounds；不复制物理公式到生产路径。
公共 Driver/Preview 仍默认 Existing，尚未开放公共 RZ 能力。

## 真实共享路径证据
手动 scoped runner 重编当前 fixture、ProblemHelper.cpp、eosdispatch.cpp，并链接可信既有 CPU 依赖。
4例：legacy polar / internal RZ，各正常与 density-floor repair。
rho=2+.25*z_cy，p=5+.5*z_cy，u=w=0,v=3,X=1；
独立 ideal reference E=p/(gamma-1)+rho*v²/2。RZ rho/p 随 z 变化，旧polar仍 z=0。
floor=4 的 repair 原有数学不改；独立 full-ring volume 按 pi*(hi-lo)*(hi+lo)*dz，
RZ总测度4pi，polar总测度2。受影响体积、质量/能量差账本符合独立积分。
max energy arithmetic error 7.10543e-15，采用现有2e-12算术工程门槛，不是新科学预算。
未知 context profile 真实路径在 callback前拒绝；rho/eng/repair ledger snapshots 不变。
CPU preview_initial_conversion / preview_mesh_geometry / initialization_probe 3/3 PASS。
diff check PASS。只记录标量处理后摘要；原始日志/ELF留
studio/.local/integration/rz-initial-population-20261004。没有H5或simulation输出。

## 边界
这是共享 Init helper 接线和解析 fixture，未调用正式注册模型 Setup，不等于 model scientific IC review；
也不等于 full public RZ AMR/evolution/restart/CUDA 完成。
context布局增加字段要求相关对象重编；没有声称旧 binary ABI 可混用。
手动 runner和报告保留 source/header/ELF fingerprints；此证据不是旧完整ARCH executable的新鲜度证明。
生产 root topology/config validation仍需完整RZ迁移，历史polar输入不自动转换。
后续还需权威模型z-dependent输入、AMR角动量约定、finite-ring gravity预算与冻结端点/长轨迹验收。
无 push/tag/main merge。
