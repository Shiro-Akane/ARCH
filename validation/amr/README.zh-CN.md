# AMR 守恒与细化行为

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

> CPU 状态：基本 prolongation/restriction、动态 regrid/reflux 守恒、核素输运及二维对称性通过；局部细化保持仍是已知限制。CUDA 的动态 topology、设备 coarse/fine exchange、紧凑通量登记和 reflux 已实现并通过源码/编译资格检查；真实设备验证待完成。

本记录将守恒与细化效率分开验收。当前 AMR 路径能把所测积分量保持在舍入误差范围内，但粗到细 ghost 的分片常数填充会在 block 界面制造细化指标，并使一个光滑周期算例最终扩展为全域细化。后一个现象作为限制如实记录，不以放宽阈值掩盖。

## 固定算例

已提交四份输入：

- `smooth_uniform80.par`：80 单元均匀网格对照；
- `smooth_uniform160.par`：具有 AMR 最细间距的均匀网格参考；
- `smooth_amr80_l1.par`：80 单元根网格和一级细化，光滑熵波穿越移动粗细界面；
- `sedov_amr_species.par`：带一个输运核素的正则化二维 Sedov 爆炸。

所有算例均使用 Cartesian 几何、理想气体 EOS、HLLC 和 RK3。光滑算例使用 MUSCL-MC，每两步 regrid；Sedov 使用 PPM，并覆盖多维 regrid、reflux 和核素通量。

## 审计环境

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

## 细化限制

光滑输入特意设置 `refine_threshold = 0.035`，拓扑演化为：

| 时间 | 0 级叶 block | 1 级叶 block |
| ---: | ---: | ---: |
| `0` | 4 | 2 |
| `0.00161450` | 3 | 4 |
| `0.00484349` | 2 | 6 |
| `0.00645799` | 1 | 8 |
| `0.00807248` | 0 | 10 |

最初未细化 block 中的解析 Lohner 指标只有约 `0.010--0.028`，低于阈值；而阈值 `0.05` 因解析最大值约为 `0.037`，完全不会触发初始细化。`GhostExchange::InterpolateFaceFromCoarse` 当前把一个粗网格值复制到两个细 ghost 单元。聚焦检查测得 fine ghost 误差为 `3.44e-3`，界面指标由 `0.00775/0.00954` 升至 `0.06727/0.04230`，从而解释了逐步扩展为全域细化的行为；该算例目前不存在可靠的阈值窗口。

`AverageFaceFromFine` 还在算术平均质量分数，而非体积加权 `rho X`；曲线坐标的 face exchange 也尚未使用物理体积权重。这些路径没有破坏上述积分测试，但局部细化保持和曲线坐标 ghost transfer 不能据此标为通过。

`ENUC` 是燃烧过程中生成的瞬态诊断，checkpoint v3 已持久化该字段，已实现的 Host 与 CUDA AMR 路径也会对它执行 transfer/exchange。因此，新 v3 checkpoint 不再存在此前的缺失字段限制。`refine_var = ENUC` 的精确动态 split-run 等价性仍需 CPU 端到端与 CUDA 真实设备验证；旧 v1/v2 checkpoint 会把 ENUC 初始化为零，不能建立这种等价性。

后续实现应对粗到细 ghost 使用受限线性的守恒变量重构，先插值 `rho X` 再恢复 `X`，并对细到粗的流体与核素状态使用体积权重。届时的验收条件是光滑算例在 `t = 0.01` 仍至少保留一个 0 级叶 block。

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
