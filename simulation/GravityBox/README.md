# GravityBox：通用 CGS 自引力气体示例

从仓库根目录运行已构建的程序：

```sh
./build-cpu/bin/ARCH GravityBox simulation/GravityBox/GravityBox.par
```

请将程序路径替换为实际构建位置。所有输入与 EOS 均使用 **CGS**：长度 cm、
时间 s、密度 g/cm³、温度 K、势 cm²/s²、加速度 cm/s²。
本案例仅负责初始场，直接使用正式 Hydro、AMR、EOS、燃烧、扩散和引力实现。
默认参数是周期的一维微扰，CPU 与 CUDA 使用同一个参数文件，仅切换
`compute_backend=cpu` 或 `cuda`。GPU 小网格通常更慢，不能据此判断数学错误。

## 初始场与参数

| 参数 | 含义与默认值 |
| --- | --- |
| `rho0` | 正密度背景，`1e7` |
| `temperature0` | 正温度背景，`1e7` |
| `amplitude` | 密度扰动相对背景的幅度，`1e-3`；周期模式须小于 1 |
| `temperature_amplitude` | 温度相对扰动，默认 0，绝对值须小于 1 |
| `velocity0` | x 方向正弦速度幅度，默认 0 |
| `width` | 孤立高斯标准差，默认 x 域长的 0.08 倍 |
| `center_x/y/z` | 孤立源中心；Cartesian 默认域中心，一维径向默认原点 |
| `gas_cv` | 无核网络时单组分理想气体的比热，默认 `1.2471693927e8` erg/g/K |
| `hydrostatic_radial` | 算例专用的一维均匀密度静水平衡参考开关，默认 `false`；不属于全局引力求解参数 |

周期模式为 `rho=rho0*(1+amplitude*cos(2*pi*x/L))`；坐标从域左侧计。
孤立模式将余弦替换为高斯：Cartesian 三维按三轴半径，一维球/柱按原生径向坐标。默认温度使用同一形状函数；静水参考模式改用下述独立温度剖面。
启用核网络时复用标准 `network_name`、`xhe4`、`xc12`、`xo16` 等组分入口，
不用 `gas_cv` 代替核物质 EOS；已验收的燃烧示例使用 Helmholtz。

## 三维孤立云

复制默认参数文件，覆盖以下键（不是在文件末尾重复声明相同键）：

```ini
nblockx1=2
nblockx2=2
nblockx3=2
x1_max=1
x2_max=1
x3_max=1
gravity_boundary=isolated
x1l_boundary_type=reflecting
x1r_boundary_type=reflecting
x2l_boundary_type=reflecting
x2r_boundary_type=reflecting
x3l_boundary_type=reflecting
x3r_boundary_type=reflecting
rho0=1e-8
amplitude=1e8
temperature0=1e-8
gas_cv=1
width=0.08
tmax=10
```

势边界由域内全部有效叶子质量给出，包括背景密度；不减平均密度，也不是固定零势。
树的二阶矩和开角属于内部算法，不增加用户配置。流体可以使用 reflecting 或 outflow；
outflow 的逸出物质此后不再是有限域内的引力源，必须单独考虑边界质量/能量收支。
周期 1D/2D 表示相应平移不变 Poisson 模型，不是三维孤立天体的降维替代。

Cartesian 已验收二进制 AMR、每个有效根轴单元数为二次幂、根网格间距比不超过 2。
Cartesian 二维/一维 isolated 仍被拒绝；一维原生球/柱 isolated 的 CPU 路线如下。
CPU 多维曲线坐标自引力已支持避开坐标奇点的完整方位角域；轴线、极点、原点、自适应层级子循环和网格外质量源仍不支持。

## 一维球/柱对称孤立域（已验收 CPU）

以默认参数为底本，覆盖以下键即可运行包含原点的球对称域；把
`geometry=spherical` 改为 `cylindrical` 即为沿轴无限延伸的柱对称模型：

```ini
geometry=spherical
compute_backend=cpu
nblockx1=4
nblockx2=0
nblockx3=0
x1_min=0
x1_max=1e8
x1l_boundary_type=reflecting
x1r_boundary_type=reflecting
gravity_boundary=isolated
rho0=1e7
amplitude=0.2
width=2e7
lrefinemax=1
refine_threshold=0.004
derefine_threshold=0.001
regrid_interval=2
max_steps=12
tmax=0.04
```

球对称外边界势取 `-G M/R`，其中单元径向体积积分乘 `4π` 得总质量；
柱对称固定 `Phi(R)=0`，质量按单位轴向长度统计，积分乘 `2π`。
原点按零通量正则面处理，内侧流体边界必须 reflecting；径向
`gravity_boundary=periodic` 和此一维径向路线的 CUDA 执行会明确拒绝。
严格径向椭圆单测、短时混合 AMR、原点、近真空、重网格与重启检查已通过；
静水参考仅验证短时寄生速度随分辨率降低，不宣称长期静水平衡精确保持。

静水参考算例设 `amplitude=0`、`hydrostatic_radial=true`、
`temperature0=5e8`、`tmax=0.02`。它只适用于从原点开始的单组分
IdealGas，构造 `T(r)=T(0)-2πG rho0 r²/[d(gamma-1)gas_cv]`，
球对称 `d=3`、柱对称 `d=2`。这是用于测量寄生速度的初值，
没有额外的 well-balanced 流体算法。验证入口为
[`radial_1d.py`](../../validation/gravity/radial_1d.py)，已并入现有
`run_self_gravity.py`，不另加 CI 任务。

## 扩散和燃烧

扩散参考：周期默认案例改为 `temperature0=3e7`、`amplitude=1e-5`、
`temperature_amplitude=2e-5`、`use_diffusion=true`、`use_thermal_diff=true`、
`alpha_therm=1e15`、`diff_integrator=RKL2`、`tmax=0.05`。
验证器用独立线性热扩散–Jeans 方程给出密度、速度和温度振幅参考。

核燃烧使用 `eos_type=helmholtz`、有效 `eos_table_path`、`use_burn=true`、
`network_name=aprox13`、`ode_solver=bd`、`linear_solver=DenseLU`，以及物理组分。
`validation/gravity/gravity_box.py` 中的 `burning_config` 和 `compact_config`
给出完整、可重现的纯氦升温与厘米尺度 C/O 联合输运输入；每次执行保存 `input.par`。
Helmholtz 输运系数由状态决定，不应再填理想气体的常系数覆盖键。

调度保持 `B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`。引力在每个 H 的真实 RK 输入
密度上重求解；能量使用实际面质量通量做功。燃烧阈值是物理控制：跨越
`nuclearTempMin` 的非光滑轨迹不具有一般二阶保证；平滑时间阶测试会单独检查参考分辨率。

## 检查结果

`GPOT/GACX/GACY/GACZ` 随当前自引力输出；`gravity_solves.tsv` 记录每次求解的残差、
目标、状态代次和执行计数。`state_repairs.txt` 不能忽略：平滑验收要求修复事件为零。
`run_timings.tsv` 的输出耗时包含流体 Host materialization、输出验证和 HDF5 写入；
调用输出函数之前的引力字段下载仍计入总时间，未计入该输出子项。

完整使用边界、CPU/GPU 数值与性能记录见
[P5–P7 验收文档](../../docs/development/P5P7GravityAcceptance.zh-CN.md)。
