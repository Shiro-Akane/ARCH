# aprox13 单区燃烧

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本页结果对应[Validation 总索引](../README.zh-CN.md)注明的科学验收版本；后续目录维护
及新构建检查单列于[维护记录](../backend/results/maintenance-freeze-20260908/)。

CPU 与 CUDA 均通过原定的跨求解器误差和组分闭合检查。
[当前候选的应用记录](results/application-first-law-20260907/release-878/evidence.json)
包含全部六次执行及对应源码、程序身份。同一候选还通过了
[原生燃烧恢复验证](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json)。
整个项目的验收状态统一见[验证索引](../README.zh-CN.md)。

`BurnOneZone` 实现仍位于 `simulation/BurnOneZone/`；本记录归属的不可变参数文件位于 [`inputs/`](inputs/)，它们使用生产 burn driver，在 \(\rho=10^7\,\mathrm{g\,cm^{-3}}\)、\(T=3\times10^9\,\mathrm{K}\)、初始 `C12=0.5`、`O16=0.5` 条件下推进至 \(t=10^{-10}\,\mathrm{s}\)。严格 BE_NR 输入（`rtol=1e-10`、`atol=1e-14`）提供内部收敛参考；BD 和 ROS4 使用 `rtol=1e-6`、`atol=1e-10`。这是求解器交叉 verification，不是对 aprox13 反应率的独立物理 validation。

## Helmholtz 表身份

本记录唯一使用的表来源是从 [Timmes EOS 网站](https://cococubed.com/code_pages/eos.shtml)下载的 `helmholtz.tar.xz` 中的 `helm_table.dat`。运行时路径为 `EOS_toolkit/tables/helmholtz/helm_table.dat`，运行前必须由 Git LFS 实体化。

| 属性 | 必需值 |
| --- | --- |
| 表形状 | 541 个密度行 × 201 个温度列 |
| 文件大小 | 60,242,514 bytes |
| SHA-256 | `c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1` |

loader 要求固定 541×201 表的全部四个数据块，并拒绝截断或非数值输入。Git LFS pointer 不能作为运行输入。

## 复现

使用开启测试的构建，准备好 `ARCH`、`arch_cuda_single_level_validation`，
以及包含 NumPy 和 h5py 的 Python 环境。核对表身份后运行归档脚本，并选择新的输出目录：

```bash
git lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat"
sha256sum EOS_toolkit/tables/helmholtz/helm_table.dat
python3 validation/burn/results/application-first-law-20260907/replay.py \
  --build-dir build-cuda \
  --output-dir validation/burn/results/application-new
```

相对 BE_NR 的验收要求：核素 Linf 和总能量相对误差均不超过 \(10^{-8}\)，丰度和残差不超过 \(10^{-12}\)。在 13 个核素上，L1 为平均绝对差，L2 为均方根差，Linf 为最大绝对差。热力学量使用 \(\lvert q-q_{ref}\rvert/\lvert q_{ref}\rvert\)。

| 后端 | 求解器 | 核素 L1 | 核素 L2 | 核素 Linf | 相对能量误差 | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPU | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | 通过 |
| CUDA | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | 通过 |
| CPU | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | 通过 |
| CUDA | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | 通过 |

[指标 CSV](metrics.csv) 还包含两端的 BE_NR 结果，其 CPU/CUDA 核素 Linf
差异为 5.551e-16；全部六次运行中，丰度和残差最大为 2.221e-16。

两个测试解的丰度和均闭合到舍入误差。ROS4 使用匹配的四 stage L-stable 系数集，每个内部步共享一套 Jacobian 矩阵。Stage 方程和系数集遵循 [L-stable ROS4 公式](https://link.springer.com/article/10.1007/s10915-023-02232-3)，并与 [OpenFOAM Rosenbrock34 实现](https://api.openfoam.com/2212/Rosenbrock34_8C_source.html)交叉核对。本应用记录检查一个状态和时间区间，下文的独立时间积分与第一定律检查补充了跨求解器对照。网络数据及实现测试见 [Timmes 技术说明](../../docs/physics/TimmesNetworks.zh-CN.md)。

## 内置网络的时间积分精度检查

四个内置网络另有独立 DOP853 时间积分得到的不可变端点，并通过 Radau 和
更严格的最大时间步复核。[当前候选的参考复核记录](results/independent-time-final-20260907/release-888/evidence.json)
覆盖四个网络，端点能量还使用已有高精度 Helmholtz 单项式拟合模型独立检查。
反应率仍来自 ARCH 共用 RHS；这里独立的是**时间积分**，不是核反应数据。
十六条复核轨迹覆盖每个网络的两种独立积分器和两档最大时间步。核素 Linf
差异最大为 \(1.666\times10^{-16}\)，温度相对差最大为
\(2.121\times10^{-14}\)，独立端点 EOS 相对差最大为
\(2.221\times10^{-16}\)，所有第一定律检查均满足原定预算。
[完整 Release 回归](../backend/results/final-first-law-20260907/release-regression-895/evidence.json)
包含十二种 Host 网络/ODE 检查、相应反例和独立的 CUDA 策略检查。
同一候选的[内置 NSE 应用验证](results/nse-application-native-20260907/release-890/evidence.json)
另覆盖十六个用例、三十二个物理终点。

启用 testing 后构建 `arch_burn_mainline_reference`。普通执行检查十二种网络/ODE
组合，核素 Linf 和总能量相对误差上限为 `1e-8`，丰度闭合上限为 `1e-12`。
温度采用同样的无量纲 `1e-8` 目标；另检查加热信号，防止微小释能算例中的
空操作被总能量尺度掩盖。严格验证输入为 `rtol=1e-13`、`atol=1e-17`、
`max_substeps=3000000`，不是应用默认值。局部积分容差不保证同量级全局误差。

独立只读复核使用 NumPy/SciPy/mpmath 运行 [time_reference.py](time_reference.py)，
指定 `--binary <build>/arch_burn_mainline_reference --build-dir <build>`。
它只查询共用 RHS/EOS，不调用 ARCH 的 ODE 算法或 Jacobian，也不会刷新 fixture。
运行时仍使用共用内存守护。

旧 main 中依赖具体方法的数值快照及原检查器/反例保持不变，作为历史数据保留。
在已独立证实的 Jacobian 和时间误差控制修正后，不再把这些近似端点当作精确解。
CUDA 短步策略检查仍保留原严格后端一致性阈值，与上文的参考复核和应用检查互为补充。
