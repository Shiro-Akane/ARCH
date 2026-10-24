# aprox13 单区燃烧

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

燃烧改变核素丰度，并在核结合能与热能之间传递能量。单区计算将反应和温度演化
与流体输运分开，便于比较时间积分器、组分约束和能量收支。核统计平衡（NSE）
检查还将所选内置网络的平衡态与独立参考进行比较。

各份详细记录注明实际受测的源码、程序和输入；模块结果统一汇总在
[验证总览](../README.zh-CN.md)中。

CPU 与 CUDA 均通过原定的跨求解器误差和组分闭合检查。
应用记录
包含全部六次执行及对应源码、程序身份。同一受测构建还通过了
原生燃烧恢复验证。
整个项目的验收状态统一见[验证索引](../README.zh-CN.md)。

`BurnOneZone` 实现仍位于 `simulation/BurnOneZone/`；本记录的参数入口位于 [`inputs/`](inputs)，它们使用生产 burn driver，在 $\rho=10^7\,\mathrm{g\,cm^{-3}}$、$T=3\times10^9\,\mathrm{K}$、初始 `C12=0.5`、`O16=0.5` 条件下推进至 $t=10^{-10}\,\mathrm{s}$。严格 BE_NR 输入（`rtol=1e-10`、`atol=1e-14`）提供内部收敛参考；BD 和 ROS4 使用 `rtol=1e-6`、`atol=1e-10`。这是求解器交叉 verification，不是对 aprox13 反应率的独立物理 validation。

## 当前输入与参考身份

BE_NR、BD 和 ROS4 参数文件显式声明当前配置契约所需的
`min_eint=1e-10`、`hll_wave_speed=roe` 和 `eos_coulomb_mult=1.0`。
[bd.par](inputs/bd.par) 同时供 controller 和配置检查夹具使用。
密度、温度、初始组分、物理终点与 ODE 容差沿用上方的参考问题。

历史结果保留其记录的源码与输入身份。当前配置加载的通过仅说明输入契约一致，
不代表新的科学轨迹或 CUDA 结果。回放应使用记录的数值／EOS 控制与表身份；
更长物理终点需要单独的独立参考及误差预算。

[当前设备记录](../backend/results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json)
已按声明的单GPU范围签收。当前原私有Cellular24步／全部25CHK通过原`5e-9`＋绝对项`5e-12`门，
ENUC在CPU／Device间逐位一致；各后端CHK12→新进程24的21项CHK／43项物理Plot也与自身连续运行一致。

真实两次burn-half输入／接受能量捕获及同输入交叉回放定位到Saha舍入放大和Host AMR RKL预乘时间的顺序差。
修复收敛在现有NSE与AMR RKL所有者；共享条件化数学，使用未缩放`L(U)`并应用真实步长一次。
当前内建／生成NSE数学、A19策略、原RKL与旋转AMR消费者通过；6859点当前函数桥接复用独立高精度参照。
方程、ODE控制、固定点零源、原守恒标准和源项定义保持：ENUC仍为最后接受的半步核能增量除以该半步时长。
原第1步／终态`4.3878863852117646e-7`／`1.1197942950938268e-7`失败保留原身份。
[发布资格索引](../../docs/releases/README.md)统一记录编译成本和仍需后续签收的CPU限定／平台／分发范围。

## CPU 科学依据

[单区 CPU 收据](../backend/results/release-readiness/REL3CpuScienceAndReliability-20261014.json)记录 BE_NR、BD、ROS4
按原单区终点及 CPU 跨求解器／组分闭合预算完成的新运行。下方历史证据保留；
该CPU记录不签收CUDA；当前设备结果见上方。

[CPU 耦合与 NSE 记录](../backend/results/release-readiness/REL3CpuScienceFollowup-20261014.json)
补充三种 ODE 与 RKL1／RKL2 组分扩散组成的六组 BurnGradient，均推进至原
`t=1e-10 s`。原核质量能量、质量及电荷 `1e-12` 预算、逐点状态检查和实际 AMR
变化通过。十六次应用运行覆盖 aprox13、aprox19、aprox21 和 iso7：每个网络
包含关闭 NSE 的 BE_NR 对照及开启 NSE 的 BE_NR、BD、ROS4，保持原能量／电荷
`1e-12` 与组分差大于 `1e-3` 的活动检查。这些只签 CPU 范围；独立时间参考与
新的 CUDA 运行分别结算。

此前的一维 Cellular 诊断显式恢复十一项旧有效控制值，包括 `use_nse=true`，
在应用墙钟 2.13 秒内完成一步至 `t=1e-16 s`，修正事件为零。它与二维
`CellularFlash2D.par` 的 FLASH 对照输入不同。实际第二燃烧半步报告 `NseSuccess`：
一次 NSE 尝试、零失败、零 BD 子步。捕获输入通过同一共享 BD／NSE 实现回放后，
packed 状态、报告及能量交接逐位相同。这是共享实现回放，不是独立 NSE 参考。
另以实际密度、输入组分和接受温度进行独立Saha与Helmholtz EOS审计，60／80位
参考一致；组分Linf为`8.54e-15`，热闭合相对残差为`2.28e-14`，均满足原
`1e-12`门槛。它核验已接受输出，不冒称观测了生产者内部Ye／旧EOS内能，
也未另求唯一偏好的热根。纯ODE积分不代表此路线。
[较早CPU收据](../backend/results/release-readiness/REL3GeometryGravitySeparation-20261014.json)
将该一维输入推进到原终点：24步、4.01秒、25个CHK、零修复；首步保存状态
与独立审计状态精确相同。六组全输运控制终点及四条活动ENUC续算也通过原标准，
其中守恒观测器的精度丢失已修复，物理公式／门槛未改。当前CPU/CUDA `enuc_rate`
及公开／二维资格由上方设备签收统一记录；该较早CPU收据保留原源码身份。
`ENUC`／`enuc_rate` 的单位为 `erg g^-1 s^-1`，表示最后一个燃烧半步的已接受
能量增量除以该半步时长，不能直接作为整宏步的能量收支账本。

## Helmholtz 表身份

本记录唯一使用的表来源是从 [Timmes EOS 网站](https://cococubed.com/code_pages/eos.shtml)下载的 `helmholtz.tar.xz` 中的 `helm_table.dat`。运行时路径为 `EOS_toolkit/tables/helmholtz/helm_table.dat`。
按[离线表指南](../../docs/guides/Build.zh-CN.md#离线表与-lfs-流量)恢复已核验、
身份匹配的本地普通文件副本，或展开已有本地 LFS 对象。经过核验的副本应保留在
构建／输出目录及 LFS 对象库之外。对象缺失时应报告，不能自动下载；
读取已实体化的表不消耗 LFS 流量。

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
git lfs checkout EOS_toolkit/tables/helmholtz/helm_table.dat
sha256sum EOS_toolkit/tables/helmholtz/helm_table.dat
python3 validation/burn/results/application-first-law-20260907/replay.py \
  --build-dir build-cuda \
  --output-dir validation/burn/results/application-new
```

相对 BE_NR 的验收要求：核素 Linf 和总能量相对误差均不超过 $10^{-8}$，丰度和残差不超过 $10^{-12}$。在 13 个核素上，L1 为平均绝对差，L2 为均方根差，Linf 为最大绝对差。热力学量使用 $\lvert q-q_{ref}\rvert/\lvert q_{ref}\rvert$。

| 后端 | 求解器 | 核素 L1 | 核素 L2 | 核素 Linf | 相对能量误差 | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPU | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | 通过 |
| CUDA | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | 通过 |
| CPU | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | 通过 |
| CUDA | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | 通过 |

指标 CSV 还包含两端的 BE_NR 结果，其 CPU/CUDA 核素 Linf
差异为 5.551e-16；全部六次运行中，丰度和残差最大为 2.221e-16。

两个测试解的丰度和均闭合到舍入误差。ROS4 使用匹配的四 stage L-stable 系数集，每个内部步共享一套 Jacobian 矩阵。Stage 方程和系数集遵循 [L-stable ROS4 公式](https://link.springer.com/article/10.1007/s10915-023-02232-3)，并与 [OpenFOAM Rosenbrock34 实现](https://api.openfoam.com/2212/Rosenbrock34_8C_source.html)交叉核对。本应用记录检查一个状态和时间区间，下文的独立时间积分与第一定律检查补充了跨求解器对照。网络数据及实现测试见 [Timmes 技术说明](../../docs/physics/TimmesNetworks.zh-CN.md)。

## 内置网络的时间积分精度检查

四个内置网络另有独立 DOP853 时间积分得到的不可变端点，并通过 Radau 和
更严格的最大时间步复核。独立参考复核记录
覆盖四个网络，端点能量还使用已有高精度 Helmholtz 单项式拟合模型独立检查。
反应率仍来自 ARCH 共用 RHS；这里独立的是**时间积分**，不是核反应数据。
十六条复核轨迹覆盖每个网络的两种独立积分器和两档最大时间步。核素 Linf
差异最大为 $1.666\times10^{-16}$，温度相对差最大为
$2.121\times10^{-14}$，独立端点 EOS 相对差最大为
$2.221\times10^{-16}$，所有第一定律检查均满足原定预算。
完整 Release 回归
包含十二种 Host 网络/ODE 检查、相应反例和独立的 CUDA 策略检查。
同一受测构建的内置 NSE 应用验证
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

新物理终点可通过 `--derive-reference --input <initial-state.json>` 给出显式 `network`、`rho`、`interval` 和 `state`；`state` 按网络注册顺序包含全部质量分数，最后一项为温度。该入口仍使用绑定表的 Helmholtz 及共用 RHS，只生成待审阅参考，不刷新旧端点或声明生产轨迹通过。新的 C/O 参考覆盖明显反应和升温，两种独立积分方法及端点第一定律满足预先分配的预算；两档步长上限在本次自适应积分中未实际生效，不作为有效步长细化证据。[处理后记录](results/release-closure-20261009/time-reference-summary.json)给出输入、独立性边界和参考误差，实际长程与续算另行验收。

依赖具体方法的数值快照及原检查器/反例作为历史数值记录保留。
精度验收使用独立参考，不把这些近似端点当作精确解。
CUDA 短步策略检查仍保留原严格后端一致性阈值，与上文的参考复核和应用检查互为补充。

## 详细复核记录

<details>
<summary>展开源码身份、机器可读数据与执行日志</summary>

下面是供复现与独立核查使用的数据文件，不是使用教程。上文已说明测试方法、结果和误差标准。

- [应用记录 (JSON)](results/application-first-law-20260907/release-878/evidence.json)
- [原生燃烧恢复验证 (JSON)](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json)
- [指标 CSV (CSV)](metrics.csv)
- [独立参考复核记录 (JSON)](results/independent-time-final-20260907/release-888/evidence.json)
- [完整 Release 回归 (JSON)](../backend/results/final-first-law-20260907/release-regression-895/evidence.json)
- [内置 NSE 应用验证 (JSON)](results/nse-application-native-20260907/release-890/evidence.json)

</details>

强反应生产终点与中段续算可复用 `inputs/long_time_reference.par`：C/O各0.5、rho=1e7、T0=3e9，物理终点1e-6，中段checkpoint位于5e-7。实际BD／ROS4终点通过独立时间参考的核素／温度／能量预算，BD中段续算的20个数值数据集逐位一致；见[处理记录](results/release-closure-20261009/production-summary.json)。运行时设定ODE局部容差不保证同值的全程误差；该几秒轨迹用于物理终点和续算，墙钟耐久及多模块长程另验收。
