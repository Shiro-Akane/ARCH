# AMR 守恒与细化行为

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> 当前状态（2026-09-03）：守恒 limited-linear prolongation、体积加权 restriction、动态 regrid/reflux 与 restart 互操作均已实现。16 GiB、零 swap 的 clean Debug CUDA 构建和 H100 生产级 CPU/CUDA AMR 矩阵均已通过。证据见 [SM90 验收目录](results/h100-sm90-20260903/README.md)。

本记录将守恒与细化效率分开验收。下方历史 CPU 数据建立了原始基线；当前 AMR 路径已用一套共享、守恒的 limited-linear 重构替换旧有 coarse-to-fine 分片常数填充，并保留了能够暴露旧版伪细化信号的聚焦回归测试。

## 固定算例

已提交四份输入：

- `smooth_uniform80.par`：80 单元均匀网格对照；
- `smooth_uniform160.par`：具有 AMR 最细间距的均匀网格参考；
- `smooth_amr80_l1.par`：80 单元根网格和一级细化，光滑熵波穿越移动粗细界面；
- `sedov_amr_species.par`：带一个输运核素的正则化二维 Sedov 爆炸。

所有算例均使用 Cartesian 几何、理想气体 EOS、HLLC 和 RK3。光滑算例使用 MUSCL-MC，每两步 regrid；Sedov 使用 PPM，并覆盖多维 regrid、reflux 和核素通量。

## 历史 CPU 审计环境

被审计工作树以 `affde827fcbf317382ed45372912b562652a71c5` 为基线，并包含本页
记录的改动；测试使用 GCC 13.3.0、CPU backend、Release flags
`-O3 -march=native -ffast-math -DNDEBUG` 和两个 OpenMP 线程；硬件为 x86_64
WSL2 下的 Intel Core i7-10700。

## 定量结果

范数使用物理单元体积。AMR 与均匀网格比较时，先把 160 单元参考限制到 AMR 叶网格覆盖，再在该公共网格上计算误差。

| 算例 | 测量量 | 结果 | 判定 |
| --- | ---: | ---: | --- |
| 光滑 AMR | 最大质量漂移 | `7.33e-15` | 通过 |
| 光滑 AMR | 最大动量漂移 | `7.33e-15` | 通过 |
| 光滑 AMR | 最大能量漂移 | `2.22e-14` | 通过 |
| 光滑 AMR | 压力 Linf 偏差 | `1.41e-14` | 通过 |
| 光滑 AMR | 密度相对解析解 L1 / L2 / Linf | `7.04e-5 / 1.19e-4 / 4.56e-4` | 通过 |
| 均匀 160 | 密度相对解析解 L1 / L2 / Linf | `2.47e-5 / 5.13e-5 / 2.58e-4` | 参考 |
| AMR 对限制后的均匀 160 | 密度 L1 / L2 / Linf | `5.32e-5 / 1.01e-4 / 4.43e-4` | 通过 |
| Sedov AMR | 质量 / 核素质量漂移 | `1.11e-16 / 1.11e-16` | 通过 |
| Sedov AMR | x/y 动量漂移 | `1.00e-18 / 1.00e-18` | 通过 |
| Sedov AMR | 能量漂移、相对漂移 | `3.55e-15`、`3.43e-15` | 通过 |
| Sedov AMR | 能量质心 | `(0.499924, 0.499924)` | 通过 |
| Sedov AMR | 径向各向异性 | `6.14e-6` | 通过 |

Sedov 拓扑在整个运行中保持 12 个 0 级和 16 个 1 级叶 block。注入能量在单元中心采样，因此守恒量是相对数值初始化能量 `1.0361899940878605` 的漂移，而不是强制等于连续输入值 `1.0`。

一次独立 transfer 审计（不作为仓库 test target 提交）覆盖流体守恒变量及两个 `rho X` 字段。Cartesian 1D/2D/3D 的 prolongation 积分误差分别为 `2.60e-18`、`3.33e-16`、`2.61e-14`，cylindrical 2D 与 spherical 3D 分别为 `2.22e-16`、`1.11e-15`；对应 restriction 误差为 `1.71e-16`、`9.00e-19`、`5.55e-17`、`1.11e-16`、`1.11e-16`，曲线坐标检查使用物理体积。细单元组分闭合误差不超过 `3.33e-16`。一个修复前 `sum(X)` 偏差可达 `0.321` 的强梯度算例，现在同时把核素积分、非负性和闭合保持到 `1.11e-16`。

同一 probe 还包含一个独立分量 limiter 会产生 `-0.605` 内能密度的对抗 Euler 状态。共同凸限制器把所有细单元保持在 `min_eint = 1e-10` 以上，最小比内能为 `1.000036e-10`；五个守恒量的物理体积平均在 Cartesian 下变化为零，在非均匀体积 cylindrical 检查中为 `4.44e-16`。

## 细化问题修复状态

旧审计发现，piecewise-constant coarse-to-fine ghost fill 会把界面 Lohner 指标从 `0.00775/0.00954` 抬高到 `0.06727/0.04230`。该实现现已替换为共享的 limited-linear 守恒变量重构：先重构 `rho X` 再恢复组分，使用统一凸物理状态 limiter，并对 fine-to-coarse restriction 采用体积权重。

保留的 transfer 与 AMR exchange 测试现覆盖 Cartesian 1D/2D/3D、Host 曲线坐标体积加权、X/Y/Z 面、粗细界面两侧及 `Current`/`Next`/`Scratch`。CUDA 只消费 Host-lowered plan 和相同标量插值数学，不维护第二套公式。

Checkpoint v3 持久化 ENUC，两个后端均在 regrid 中迁移它。真实设备上的 `refine_var = ENUC` restart 矩阵已通过连续运行和四条 split-run backend route。旧 v1/v2 checkpoint 仍会将 ENUC 初始化为零，因此不能用于建立 ENUC split-run 等价性。

## 复现

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform80.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform160.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_amr80_l1.par
./bin/ARCH Sedov validation/amr/inputs/sedov_amr_species.par
~~~

标量结果保留在 [metrics.csv](metrics.csv)，原始 HDF5 输出不纳入版本控制。已有历史图仅保留为定性诊断，不参与本次验收。

| 历史算例 | 可见模块 | 档案 |
| --- | --- | --- |
| Sedov | 流体、激波驱动细化 | [图像](figures/legacy/sedov.png) |
| Gaussian | 扩散、移动细化模式 | [图像](figures/legacy/gaussian.png) |
| Rayleigh--Taylor | 流体、重力、扩散 | [图像](figures/legacy/rayleigh_taylor.png) |
| Cellular burn | 流体、燃烧 | [图像](figures/legacy/cellular_burn.png) |

PPM 在粗细面使用 MUSCL-MinMod，因此本记录不声明 AMR 全域三阶空间收敛。
