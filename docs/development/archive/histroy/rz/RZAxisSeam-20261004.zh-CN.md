# RZ 轴线 donor 与 ghost 执行

在既有 CoordinateSeamPlan 加入显式 RzAxisymmetric chart。
默认 ExistingChart 保留旧 polar/full-angular 行为，生产调度尚未切换。
RZ 只接受二维 cylindrical；物理 r=0 轴镜像半径，不移动 z，
mom_u/mom_w 奇、mom_v 和标量偶。非零内半径不是轴。
复用既有 locate_donor、donor_stencil、apply_coordinate_seam_transfer、
Host executor；不增加独立 RZ 重构器或半圈 donor。

CPU 限定 arch_amr_operation_plans 构建和 CTest 1/1 PASS。
实际均匀两根块、真实混合 L0/L1 五叶拓扑覆盖不同 z 区域；检查 donor 同一
物理轴边界块、两层均被覆盖、标量/组分和三分量 parity、错误维数拒绝，
以及非零内边界无 transfer。既有 polar/spherical 2D/3D seam、
混合层级、admissibility fallback 与 AMR transfer regression 同一测试通过。
不把这些工程检查称为全 RZ 保守演化、CUDA 或物理验收。

初轮夹具遗漏 lrefinemax=1 导致真实 LoadLeafGrid 正确拒绝；
修正测试声明后通过，未绕过检查。日志和编译输入保存在
studio/.local/integration/rz-axis-seam-20261004；精确身份见同名 Summary.json。

后续须将 chart identity 贯穿 GhostExchange/cache/device lowering，
并与完整 RZ metric/source/transport/elliptic/IO/checkpoint 成套迁移。
本步不开放 runtime capability，不运行 simulation，不 push。
