# Cartesian Plotfile 相邻 y-face 修复与复验

## 结论

f6f5f98b 全域扫描发现的 Cellular stored native bounds 缺口/重叠，已由最小 writer arithmetic 修复关闭。在本次真实 Sod 192 单元、Cellular 5120 单元中，stored 与逻辑覆盖均恰好一次，gap/overlap/outside 精确测度为0。仅证明这些指定的 Cartesian t=0 输入，不外推任意 block 布局、曲线坐标、3D、科学 oracle 或演化。

实现提交：b26fb8a2f5967db15aec1b4f2bcd3ddafb9689f1。
CPU binary SHA：1bdd71ed344f01dd9722898479d37780e81a834f4d0bdea501b23a933804e1a8。

## 修复与反例

原 writer y upper = rounded y_lower + dx2；相邻 lower = x2_min + integer*dx2，两条求值路径可能舍入不同。改为 upper = x2_min + (j-ng+1)*dx2，使同一 block 内相邻行共享同一个面索引求值。

不修改 Grid/GridMetrics、科学场、EOS、cell_measure、checkpoint、阈值；不使用 epsilon/snap，不从中心反推边界，也不在 Inspector 中选择第一 match 掩盖多命中。Core 测度继续直接来自 GridMetrics::CellVolume，不能把 bounds extent 的舍入差当成新的物理测度。

先在现有 C++ publication test 增加非二进制精确 spacing 的相邻行反例，旧实现真实失败（ctest exit8）。修复后 publication/checkpoint compatibility 2/2 PASS，含既有 write/flush/close 失败传播反例。

## 清洁来源与 CPU t=0

从 clean b26fb8a2 用现有 build-cpu 增量构建 ARCH，仅 PlotIO object 与 link；未 configure，未替换桌面 build-studio-cpu。归档 executable 和完整日志均留本机 ignored。

使用原已授权 Sod-ps9xam5i、CellularDet-961ptsw8 的明确 tmax=0/max_steps=-1/CPU 输入，仅更改 out_dir 为新独立目录；plt_variables=DENS 等其他参数不变。两次退出0，checkpoint time=0/step=0，OMP_NUM_THREADS=1；不是演化验收。

- Sod native bounds 完全不变。
- Cellular 仅 1280 个 x2_upper FP64 值变化。
- 两模型其余所有 Data/Grid/NativeGrid datasets 的 dtype、shape、字节一致；包括 DENS、center、measure、level/logical/morton。
- 各模型 checkpoint 全部20个数值 datasets 与旧对照逐字节一致；四组记录 EOS 属性一致。
- 新旧 output/config/binary 身份各自记录，不声称 file bytes 或全部 checkpoint metadata 身份相同。
- Cellular stored endpoints 相对理想逻辑域仍可有浮点差；本修复关闭 coverage gap/overlap，并未宣布所有几何数值精确等于有理网格。

完整精确测度、input/output SHA、文件尺寸与本地目录见 Summary；所有原 H5/plt/checkpoint 保留，未覆盖旧反例文件。

## Inspector 原失败点复验

独立 h5py 按半开 stored bounds 得到期望，production isolated reader + client validatePlotfilePoint 验证：
- [0.1,1.2]：唯一命中 index1104，原始 DENS 44018601.46790276。
- [0.1,2.6]：唯一命中 index1232，原始 DENS 43609267.79755719。
- 均扫描5120单元，file SHA保持，未插值、未改变原始值。

新增 validation/io/verify_plotfile_face_repair.mjs。原 verify_plotfile_coverage_points.mjs 保留旧文件的零/双命中失败复现，不把旧历史报告改写成通过。

复现：用 verify_plotfile_coverage.py 读取新文件（domain/root 与旧输入相同）；node validation/io/verify_plotfile_face_repair.mjs <local-repaired-queries.json>。

没有改 Studio 实现，因此不重复已通过的320项 npm/production baseline；仅运行新 reader/client 脚本。没有 native GUI 新文件 UAT、CUDA、非t=0 simulation、push/tag。完整联合目标、科学 review、JENS/RZ与平台预算任务仍未完成。
