# 历史 AMR 可视化档案

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本档案包含 AMR renderer 和历史场图像，状态为不带定量容差的可视化诊断。规范 AMR 状态与定量工作列表位于 [`validation/amr/README.zh-CN.md`](../../validation/amr/README.zh-CN.md)。

渲染不会改变求解器状态或 HDF5 数据。Cartesian 场使用物理单元四边形，spherical 场使用极坐标单元四边形；网格 overlay 只包含叶 patch 边界。

## 重新生成

renderer 需要 Python 3，以及 `numpy`、`h5py` 和 `matplotlib`。从 ARCH 仓库根目录执行：

    python -m pip install numpy h5py matplotlib
    python Validation_file/AMR_Visual_Archive/render_amr_archive.py

新 clone 不含下列被忽略的 HDF5 运行输出。调用 renderer 前应重新运行相应算例或提供 plot 文件。

## 输入与图像

| 算例 | 图像 | Plot 输入 |
| --- | --- | --- |
| sedov | [sedov_amr.png](sedov_amr.png) | output/diag_sedov_dynamic_ppm_cfl03/SedovDynamicPPMCFL03_HLL_plt_0004.h5 |
| gaussian | [gaussian_amr.png](gaussian_amr.png) | output/diag_gaussian_amr_rkl_sts/GaussianAMRRKLSTSShort_HLL_plt_0002.h5 |
| rt | [rt_amr.png](rt_amr.png) | output/diag_rt_amr_gravity_diffusion/RTAMRGravityDiffusion_HLL_plt_0001.h5 |
| cellular | [cellular_amr.png](cellular_amr.png) | output/diag_cellular_amr_burn/CellularAMRBurn_HLL_plt_0005.h5 |
