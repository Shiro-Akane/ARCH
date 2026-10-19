# Plotfile 原生网格候选增量

日期：2026-10-03。基线 eb12c90c256d0007b5d0ba279af8f17d7c534885。
授权：联合计划 35c5b7b114069621901386bfc4bc2a656e65af06 §4.3。
状态：候选 metadata 接线与工程验证；未完成第一个交付或科学 review。

## 存储与来源

共享 writer 接受可选 PlotNativeGrid；旧调用保留，既有 Data/Grid 数值与排列不变。
真实 PlotIO 在原本 b/k/j/i 活动内部单元遍历中同步收集 metadata，
支持 Cartesian 1D/2D，curved/3D 不宣称支持。
候选 NativeGrid 组 version=candidate-cartesian-1：
x1/x2/x3_lower、upper 为每存储单元一项的 1D double 数组；
cell_measure 也是相同 flattening；logical_x1/x2/x3 为每 block 一项 uint32。
cell shape 来自 Data dimensions；level 仍在 Grid/level。
文件内身份使用 level+logical coordinates，仅为 file-local，不宣称跨 run/epoch 全球唯一。
Stored index=b*cells_per_block+(k*Ny+j)*Nx+i，其中 i/j/k 是无 ghost 的局部索引。

x1 bounds 使用 Grid faces，x2 bounds 使用该 Grid 原生均匀 spacing 与 ghost offset；
Inactive x2/x3 bounds 明确为 0，不以零宽度推算测度。
cell_measure 调用 GridMetrics::CellVolume(make_geometry_view(grid),i,j,k)，
没有 Host/Studio 独立几何积分公式。
Cartesian 1D 实际测度为 dx1，2D 为 dx1*dx2，inactive measures omitted。
数值约定如实记录，measure unit=unknown；低维物理单位/归一化待 owner review。
Grid/x,y,z 保持既有转换后的 Cartesian 中心，不覆盖成 native edges。
geometry/centering/block kind/source/convention 都在候选组明示。

writer 对每轴长度、活动 bounds 有限且正宽、非活动 bounds=0、
logical 数量及测度有限正值进行发布前校验。不从文件名/当前 Project 推断科学身份。
本轮没有 field units、case/config/build/binary/EOS provenance，plot_identity_state 仍 unknown。
reader 仍旧 audit，不能依据候选组自动完成正式结果认证。

## 验证与限制

真实 Grid，ng=2，两个 manufactured blocks，分别 1D 与 2D；
逐 cell 对照独立 Cartesian bounds/measure、中心对齐与 x1-fastest raw values，
回读存储测度/bounds/logical mapping，坏 bounds 拒绝发布。
非方形 2D publication suite 与 NaN/Inf 保留验证持续 PASS。
编译 arch_plotfile_publication、arch_checkpoint_compatibility 和真实 PlotIO object。
CTest plotfile_publication/checkpoint_compatibility 2/2 PASS，git diff --check PASS。
只编译对象/scoped test，未链接/替换 production ARCH executable，未运行 simulation/CUDA。
本机 build-cpu fixture 与 studio/.local/integration/plotfile-native-grid-20261003 日志不提交。

测试是 manufactured IO payload，不冒称真实 AMR hierarchy/模型演化对照。
相同 bounds/measure 数组尚未接只读 query；真实 Sod/Cartesian AMR 输出逐值 review 待继续。
候选存储 6 个 bounds + measure 共增加约 56 bytes/cell，另有 12 bytes/block logical，
未优化 chunk/compression/index；真实峰值 RAM 与 I/O 必须后续实测，不能以数组理论大小代替。
PlotIO 已按 total_cells/num_blocks 预留容量。
首次全域扫描成本仍保留，pixel budget 不约束首次读量。
