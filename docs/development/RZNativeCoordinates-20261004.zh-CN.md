# RZ 原生 Grid 坐标入口

基线 3c28623a0b7d2ae2a38a17c49781a9fe44887ffe。
冻结含义：二维 cylindrical=(r,z)，PointCoords.r 仍为球半径；r_cy/z_cy 是柱坐标。
当前公共 default 仍保留 polar，不能提前发布 runtime/Preview RZ 可运行能力。

## 实现
Grid InitializeTopology / GetAxisNames / GetPhysicalCoords / PhysicalCoordsFromNative
接受同一显式 internal GeometrySemantics；默认 Existing 与旧行为一致。
RZ 仅允许二维 cylindrical，轴名 r_cy,z_cy；
native (r,z) -> Cartesian (r,0,z)，phi=0，r=hypot(r,z)，theta=atan2(r,z)。
RZ x2 为长度，可负、可跨度大于2pi；旧极平面角度 guard 仍保留。
未知 profile / 非二维 cylindrical 在坐标展开前拒绝。
不改 Field arrays 或默认 Plotfile/Preview 语义，不推断历史输入等效迁移。
超大数坐标使用 hypot 避免球半径中间平方溢出；没有物理小值阈值。

## 验证
CPU curvilinear_metrics/boundary_plan/checkpoint_compatibility 3/3 PASS，0.49秒。
新增 5 解析点（轴线、正负z、3/4/5、1e150）、全部256 native interior centers；
长度20的z范围显式RZ成功、旧polar拒绝；legacy 2D和3D柱坐标 exact witnesses；
1D/3D cylindrical 与 2D Cartesian/spherical 的 RZ profile 反例。
是坐标数学/兼容性证据，不是模型科学 IC、AMR 或演化验收。
本轮发现实际 ProblemHelper::detail::PopulateState 仍默认调用 Grid.GetPhysicalCoords，
下一步须将 authoritative initialization context 同一 profile 接入，并同步 repair CellVolume。
因此不以坐标单测声称 z-dependent 模型初始化已经贯通。
checkpoint 独立 chart 保护保持；公共 config/dispatch/旧输入迁移仍未完成。
无 simulation/CUDA/push/tag，原始数据留本机。
