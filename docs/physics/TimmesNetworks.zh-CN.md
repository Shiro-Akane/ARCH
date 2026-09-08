# Timmes C++ 核反应网络：实现、验证与使用说明

英文原文：[TimmesNetworks.md](TimmesNetworks.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

核反应网络跟踪所选核素的含量，以及反应释放或吸收的能量。反应率方程给出这些量的
时间导数，常称为方程右端（RHS），常微分方程（ODE）求解器负责随时间推进它们。
启用核统计平衡（NSE）模型时，则在网络选定的核素集合内求平衡组分。

本文供开发者和需要选择燃烧模型的用户查阅实现与来源。第一次运行请从
[算例指南](../guides/SimulationCase.zh-CN.md)开始；需要核素顺序、求解器约定或
参考对照时，再阅读下方各节。

四个内置网络在 CPU 与 CUDA 上共用反应、ODE 和 NSE 数学实现。当前验证包括独立
时间积分、能量检查和耦合 NSE 主程序测试；原 Fortran 对照作为转写记录保留。

## 1. 来源与实现边界

本目录中的 `iso7`、`aprox13`、`aprox19` 和 `aprox21` 是根据项目提供的经典 Timmes 小型核反应网络 Fortran 源包逐项转写而成的 C++ 实现。我们严格以对应的 `public_*.f90` 源文件为基准，原样保留了所有的反应率公式、正/逆反应关系、核心核素数据、筛选修正、近似平衡分支、RHS 装配以及能量释放约定。

来源归属按文件实际内容记录，而不是由目录位置推断。转写公式和生成方程注明 Timmes 上游；`Dual.h`、`RatePair.h`、`TimmesNetworkSupport.h` 等 ARCH 自有适配层则明确标为项目代码。官方[反应网络页面](https://cococubed.com/code_pages/burn.shtml)提出引用要求，但没有列出标准 SPDX 许可证。重新分发范围以及 EOS/NSE 的具体来源见 [`THIRD_PARTY_NOTICES.zh-CN.md`](../../THIRD_PARTY_NOTICES.zh-CN.md)。

运行契约如下：

- 网络代码位于 `src/physics/network/`，由 C++ 直接编译；
- 运行时依赖中不含 Fortran、Python 和 pynucastro；
- Helmholtz EOS 仍需运行参数指定真实的 Helmholtz 表；
- 网络转写兼容性通过相同状态、相同 EOS 表下的 C++ 与原 Fortran 输出进行检查。

第 5 节保留最初的转写对照基准。当前时间积分与热力学验收见
[燃烧验证](../../validation/burn/README.zh-CN.md)。

## 2. 网络及核素顺序

核素顺序是状态数组、HDF5 输出和 Jacobian 行列的固定 ABI，不应随意调整。

| 网络 | 核素数 | ODE 方程数（含温度） | 核素顺序 |
| --- | ---: | ---: | --- |
| `iso7` | 7 | 8 | He4, C12, O16, Ne20, Mg24, Si28, Ni56 |
| `aprox13` | 13 | 14 | He4, C12, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Fe52, Ni56 |
| `aprox19` | 19 | 20 | H1, He3, He4, C12, N14, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Fe52, Fe54, Ni56, neutron, proton |
| `aprox21` | 21 | 22 | H1, He3, He4, C12, N14, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Cr56, Fe52, Fe54, Fe56, Ni56, neutron, proton |

## 3. 代码结构

- `timmes_common/`：共享核常数、温度因子、双数自动微分、筛选修正、反应率装配和通用 RHS/Jacobian 支撑。
- `<network>/TimmesRateLibrary.h`：对应原 Fortran 的反应率公式及温度导数。
- `<network>/TimmesRhs.inc`：对应原 Fortran 网络方程的 RHS 组合关系。
- `<network>/Net*.h`：核素表、网络接口、筛选/分支逻辑和能量约定。
- `aprox13/TimmesJacobian.inc`：由原 `public_aprox13.f90` 的显式 Jacobian 段机械抽取并适配的组分 Jacobian。

网络对燃烧求解器提供以下接口：

1. `eval_rhs`：质量分数 RHS 和核能释放率；
2. `eval_jacobian`：组分对组分的解析 Jacobian；
3. `eval_temperature_derivative`：组分 RHS 及核能释放率对温度的解析偏导。

## 4. 温度方程、Jacobian 与 LHS 约定

ODE 状态为

```text
U = [X_1, X_2, ..., X_N, T]^T
```

固定密度燃烧子步使用所选 EOS 的比内能 `e(rho,T,X)` 与 `cv`。
记组分变化率为 `f_i=dX_i/dt`、净比加热率为 `enuc`，热力学第一定律给出：

```text
f_T = dT/dt = (enuc - sum_i e_i * f_i) / cv
e_i = (partial e / partial X_i)_(rho,T)
```

组分变化在固定温度下也会改变 EOS 内能，因此必须计入温度方程。
对任意状态变量 `U_j`，完整热方程 Jacobian 为：

```text
J(i,j) = d(dX_i/dt) / dX_j
J(i,T) = d(dX_i/dt) / dT
J(T,j) = (partial_j enuc - sum_i e_i * J(i,j)
          - sum_i (partial_j e_i) * f_i - f_T * partial_j cv) / cv
```

三种 ODE 与 CPU/CUDA 共用这套装配。Ideal、Helmholtz 和表格 EOS 提供解析
热力学/组分导数；其他 duck-typed EOS 可使用同一数值求导适配器。
反应率导数保留网络声明的筛选约定。生成式弱网络另携带有符号能量源积分，
与接受步的组分变化共同决定交回流体模块的能量。

每个被接受的子步都用网络的核反应能量权重计算组分增量对应的释能，再加上有符号
的外部能量源增量，累计得到比内能变化 `delta_e`。流体模块向守恒能量加入
`rho * delta_e`，输出 `ENUC = delta_e / dt`，燃烧时间步限制器也使用同一积分。
计算直接使用求解器中的增量，在增量舍入到最终状态之前完成，因此较大的热能
背景不会淹没微小释能。被拒绝的试算不贡献能量；最终 EOS 查询检查热力学状态，
NSE 则提供被接受投影的能量。CPU 与 CUDA 共用这套核算。

Backward Euler + Newton-Raphson 的线性系统为：

```text
LHS = I - dt * J
LHS * delta_U = U_old - U_k + dt * RHS(U_k)
```

屏蔽修正的组分 Jacobian 采用与原 Fortran 一致的 frozen-screening 约定。修改筛选因子的组分求导方式时，需同步更新 Jacobian、LHS 和验证基准。

## 5. 原 Fortran 数值验证

历史转写对照使用当时的温度方程约定和真实 Helmholtz 表，在以下 6 个状态进行：

```text
T   = 1e9, 2e9, 5e9 K
rho = 1e6, 1e8 g cm^-3
```

表中误差是跨全部测试状态及全部相关分量取得的最大相对误差。验收阈值为 `1e-12`。

| 网络 | RHS 最大相对误差 | Jacobian 最大相对误差 | LHS 最大相对误差 | Helmholtz cv 最大相对误差 | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| `iso7` | 2.383748949565e-15 | 2.940236373588e-15 | 2.856278354823e-15 | 5.705278821529e-16 | PASS |
| `aprox13` | 3.596869184109e-14 | 2.850246815720e-13 | 2.850309327978e-13 | 1.331631293002e-15 | PASS |
| `aprox19` | 9.991734850962e-15 | 5.467476955907e-15 | 5.492012250384e-15 | 3.753488177566e-16 | PASS |
| `aprox21` | 3.997143540823e-14 | 1.092264838562e-14 | 1.091781737534e-14 | 3.947012873172e-16 | PASS |

最差项为 `aprox13` LHS 的 `2.850309327978e-13`，仍低于要求约 3.5 倍。误差主要来自 Fortran/C++ 浮点求值顺序差异。该对照覆盖上述六个状态；长期物理结果还需守恒量、收敛性和分辨率研究。

## 6. 在线 NSE 求解器

高温高密度路径使用 `src/physics/nse/nse_solver.h` 中的 `NSESolver<NetType>`。Frank Timmes 的 `public_nse.tbz` 提供 47 核素 Fortran 在线求解程序。C++ 实现转写其中的 Saha 方程、质量/电荷守恒残差和 2 x 2 Newton-Raphson Jacobian，并将固定核素数组改为编译期网络数据。

求解器只从 `NetType` 读取编译期核数据：`NUM_SPECIES`、`AION`、`ZION`、`BINDING_E`、`SPIN` 和能量转换系数。`BINDING_E` 表示单个原子核的总结合能（MeV）；质量超额需先转换。接口变量名为 `X_old/X_out`，输入输出均为质量分数 `X_i`；能量闭包内部使用摩尔丰度 `Y_i = X_i/A_i`。

固定温度和密度下的方程为：

```text
X_i = A_i/(N_A rho) * G_i * (2 pi A_i m_u kT / h^2)^(3/2)
      * exp(((A_i-Z_i) mu_n + Z_i mu_p + B_i) / kT)

sum_i X_i = 1
sum_i (Z_i/A_i) X_i = Ye
```

实现中的数值保护包括：

- 用无量纲化学势和 log-sum-exp 求值，避免 Saha 指数上溢/下溢；
- 用解析 2 x 2 Jacobian 和克莱姆法则求 Newton 步，并配合步长限制和回溯线搜索；
- 若质子丰/中子丰边界使 2 x 2 Jacobian 条件数过差，则固定 `eta_p-eta_n` 先解质量归一化，再对电荷化学势做有界一维求根；该路径只作 Newton 失败后的泛型保底；
- 最多 100 次 Newton 迭代，残差目标 `1e-12`，返回前再次检查质量与电荷守恒；
- 组分循环使用 `omp simd`，求解器状态局部于调用，可置于外层单元 OpenMP 并行区内；
- `iso7` 和 `aprox13` 的全部核素均满足 `Z/A=0.5`，两条守恒方程线性相关。代码会检测该退化情形，固定 `mu_n=mu_p` 后解一维归一化方程；这两个网络只在 `Ye=0.5` 时存在受限 NSE 解；
- `aprox19`/`aprox21` 同时含有物理量子态相同的 `h1` 和 `prot` 记账条目。NSE 统计和中排除重复的 `h1`，平衡自由质子写入 `prot`，避免重复计算质子简并度。

求解结果是当前网络核素集合上的“网络受限 NSE”。`iso7`/`aprox13` 缺少自由核子和中子丰核素，只支持 `Ye=0.5` 的受限解，在高温低密度下可能偏离完整 NSE。完整物理 NSE 需要覆盖充分的独立核素集合及守恒映射。
CPU 与 CUDA 对四个内置网络使用同一求解器；生成网络包不支持在线 NSE。

NSE 投影联立求解：

```text
e_EOS(rho, T_new, X_NSE(T_new)) - e_old - enuc(X_old -> X_NSE) = 0
```

快速路径使用 EOS `cv` 和后续割线斜率，失败时使用有界二分。组分守恒与相对能量闭包均达到 `1e-12` 后接受状态。投影失败时保留原状态并回到常规 ODE 路径。

迭代前，求解器会按同一精度标准检查输入是否已经满足 Saha 关系及质量、电荷约束。满足条件的状态保持不变，避免重复平衡投影因舍入误差产生数值热量。温度、密度或组分的变化一旦使平衡残差超标，仍会进入正常的非线性求解。

当前 [NSE 主程序验证](../../validation/burn/results/nse-application-native-20260907/release-890/evidence.json)
已通过覆盖四个内置网络的 16 个单区案例，共检查 32 个 CPU/CUDA 固定物理时间端点。
每个网络分别通过 BE_NR、BD 和 ROS4 执行 NSE，并设置关闭 NSE 的 BE_NR 对照。
测试核对后端一致性、状态正性、组分归一化，以及启用 NSE 后可明确分辨的组分变化。
独立结合能收支检查交回流体模块的能量；相对能量闭合误差与绝对电荷漂移均满足
`1e-12`。

以下历史对照记录保留原实现及其数值约定：

- 以 47 核素 `public_nse.f90` 严格残差副本为基准，在 `T=2.5e9--1e10 K`、`rho=1e6--1e9 g cm^-3`、`Ye=0.47--0.55` 的 8 个状态逐核素比较，最大质量分数绝对误差为 `4.897193761622e-13`；
- 四网络在 `T=4.5e9、5e9、7e9、1e10 K` 与 `rho=1e6、1e7、1e9 g cm^-3` 的 48 个组合全部收敛，`sum(X)` 与 `Ye` 均通过 `1e-12` 检查；
- `aprox19`/`aprox21` 另在 `Ye=0.40--0.60`、`T=4.5e9--1e10 K`、`rho=1e6--1e10 g cm^-3` 各扫描 84 个状态；最坏质量归一化误差 `6.49e-16`，最坏电荷守恒误差 `6.98e-13`；
- `aprox19` + 真实 Helmholtz EOS 在 `rho=4.322e7 g cm^-3`、`T_old=4.67e9 K` 的 He4/C12 初态得到 `T_new=6.7610109665e9 K`，相对能量闭包残差 `7.5678598227e-14`；
- 实际 64 x 16 Cellular Driver 高温路径的四个 species count 分别为 7、13、19、21，均完成一步；`aprox19` 的 OpenMP 1/16 线程最终 HDF5 共 26 个数据集且逐位一致。

## 7. OpenMP 并行特征

CPU 目标默认要求 OpenMP：

```text
ARCH_ENABLE_OPENMP=ON
OpenMP_CXX_FLAGS=-fopenmp
```

并行层次为：

- 最外层按网格单元执行 `parallel for schedule(dynamic, 1)`；
- 每个单元拥有独立的 ODE 状态、网络临时量和 LU 矩阵；
- 网络、ODE、Jacobian 装配和稠密 LU 的适合循环使用 `omp simd`；
- 不在单个 8--22 阶小矩阵上再创建嵌套 OpenMP 团队，避免调度开销大于计算量。

在 64 x 16、`aprox13`、关闭 NSE、热点温度 `3e9 K` 的 3 步燃烧测试中，1 线程和 16 线程在步骤 0--3 的每份输出上均有 20 个同名 HDF5 数据集，且全部逐位一致。测试中生成了初始为零的 Ne20，因此确实经过网络/ODE/LU 燃烧路径，而非纯流体或 NSE 投影。高温 NSE 路径另以 `aprox19` 完成 1/16 线程逐位一致验证。

## 8. 微观物理与输运系数接口

输运系数数学内核位于 `src/physics/diffusionCoe/diffusion_math.hpp`。输入包括 `eos_state_t` 中的 $\rho$、$T$、$P_e$ 和 $\eta$，以及 `SpeciesManager` 提供的 $A_{ion}^{-1}$ 和 $Z_{ion}$。调用顺序为：

```text
HelmEos::evaluate
  -> 提取 EOS 状态和网络核素属性
  -> ConductivityMath::compute_stellar_conductivity
```

[扩散系数对齐报告](DiffusionCoefficientAlignment.zh-CN.md)记录了 `aprox19`、10,000 个以上随机状态的十六进制浮点比较；测试范围内的最大绝对误差和最大相对误差均为 `0.0`。第 5 节的反应网络 ODE 验证覆盖独立的 RHS、Jacobian 和 LHS 路径。

## 9. 历史 CPU 求解器对照

早期 ODE 分发回归使用真实 Helmholtz 表、四个 Timmes 网络和 8 × 2 Cellular 单步。BD 在 `dt=1e-14 s` 下完成 iso7、aprox13、aprox19、aprox21 四种矩阵维度，所有输出有限，species count 分别为 7、13、19、21，最终 `max|sum(X)-1| <= 2.2204e-16`。aprox13 的 BD 与 BE_NR 对照中，能量相对差约 `3.51e-15`、压力相对差约 `7.34e-15`，C12/He4 等主组分绝对差不超过 `1.23e-15`。接近零的 trace species 同时报告绝对误差和相对误差。

BD 的最高阶提前退出条件使用 `k + 1 < MAX_K` 保护 `n_seq[k+1]`。`BE_NR`、`BD` 和 `ROS4` 均可通过 `ode_solver` 选择。ROS4 使用配套的四阶段 L-stable 系数，每个内部步只构造和分解一次 `I - gamma*dt*J`。在 Helmholtz/aprox13 单区对照中，以内部收敛的 BE_NR 运行为参照，ROS4 的 species Linf 为 `3.281e-11`，总能量相对误差为 `1.517e-11`，通过该记录的 `1e-8` 标准。完整输入、Helmholtz 表身份与指标见 [燃烧验证记录](../../validation/burn/README.zh-CN.md)。第 12 节说明当前的独立时间积分验收。新网络或生产状态需要进行步长/容差收敛，并比较组分和能量轨迹。

## 10. 使用方法

在参数文件中选择网络：

```text
use_burn = true
network_name = iso7       # 或 aprox13 / aprox19 / aprox21 / custom:<id>
use_nse = true               # 默认开启在线网络受限 NSE；设为 false 可禁用
nseTempThreshold = 4.5e9
nseDensThreshold = 1.0e6
ode_solver = BE_NR
linear_solver = Auto
eos_type = helmholtz
eos_table_path = /absolute/path/to/helm_table.dat
```

pynucastro 生成网络包的工作流见 [custom 网络契约](../../src/physics/network/custom/README.md)和 [Reference](../Reference.zh-CN.md)。线性求解器名称不区分大小写。`Auto` 对不超过 31 个 ODE 方程的系统选择 DenseLU，这一计数包含温度和辅助能量变量；更大的系统在 CPU 上使用 SuiteSparse KLU，在 CUDA 上使用 cuDSS。SparseKLU 仅适用于 CPU，cuDSS 仅适用于 CUDA，不兼容的显式组合会在后端构造前被拒绝。可选 cuDSS 0.8 通过 CMake/`CUDSS_ROOT` 发现，只有求解器与相应网络/EOS 路由实际链接时才开放；缺少依赖会明确报错。

生成网络包提供可设备调用的数学实现，并通过[包元数据检查](../../src/physics/network/custom/README.md)后，才能注册 CUDA 路由。`generator_version` 字段是供构建系统检查的包结构信息。CPU 与 CUDA 使用同一数学头文件、常数与 Jacobian 结构。已支持的内嵌弱反应率表由各后端持有不可变存储，并通过显式视图传给共同数学库。通过包检查但不具备设备端数学契约的网络包仅在 CPU 上执行。CUDA 稀疏计算使用共用的 BE_NR/ROS4/BD 状态机，由后端专用 CSR/cuDSS 执行器处理线性求解。

生成网络仍设置 `SUPPORTS_NSE=false`。已支持的弱反应网络使用相同 ODE 阶段、误差控制和回滚机制积分带符号的能量源，并将其与核反应能量一起纳入共同的接受步能量核算。[网络验证](../../validation/network/README.zh-CN.md)分别记录生成数学库、求解器兼容、独立物理轨迹和完整程序验证。模型的科学可靠性取决于其核素集合、反应数据和适用范围。

OpenMP 默认开启，可通过 `-DARCH_ENABLE_OPENMP=OFF` 关闭。使用 OpenMP 编译时，可用环境变量控制运行线程数，例如：

```bash
OMP_NUM_THREADS=16 <build-dir>/bin/ARCH CellularDet case.par
```

程序启动信息显示实际的 `Species Count` 和 `OpenMP: ON (max threads=...)`，用于核对网络和线程配置。

## 11. 物理限制与维护约束

1. **输运计算顺序**：`HelmEos` 先填充 `eos_state_t`，`SpeciesManager` 再提供 $A_{ion}^{-1}$ 和 $Z_{ion}$，随后调用 `diffusion_math`。
2. **弱反应与电子化学势**：`aprox19`/`aprox21` 中依赖 Helmholtz 电子化学势 `eta_e` 的弱反应入口当前为零；启用该路径需要经过验证的反应率实现和接口映射。
3. **网络受限 NSE**：在线 Saha NSE 使用所选小网络。`iso7`/`aprox13` 仅支持 `Ye=0.5` 的受限解；完整 NSE 需要独立核素集合和守恒映射。
4. **验证要求**：修改反应率、核素顺序或能量数据后，重新运行相应的原 Fortran 网络对照。EOS 或积分器修改需要独立热力学、时间收敛和能量闭合检查，再执行 CPU/CUDA 主程序验证。历史温度方程/LHS 对照与当前第一定律模型分开记录。

## 12. 积分精度与验证

第 4 节的共用第一定律方程使用解析反应与 EOS 模型验证，包含组分相关内能和比热。
测试核对能量变化率、完整 Jacobian，以及 CPU/CUDA 上 ROS4 的四阶收敛。
匹配的 [KPP ROS4 公式](https://kpp.readthedocs.io/en/stable/num_methods/rosenbrock-methods.html)
使用这套 RHS 的 Jacobian。内置网络与查表 Urca 网络另以独立 DOP853/Radau
轨迹核对时间积分。

当前[内置网络独立复核](../../validation/burn/results/independent-time-final-20260907/release-888/evidence.json)
已通过四个网络的 DOP853 与 Radau 对照，两种积分器各采用两档最大时间步，共得到
16 条独立积分轨迹。复核只查询共同的反应/EOS RHS，不使用 ARCH 的 ODE 算法或
Jacobian。另以 60 位和 80 位精度的 Helmholtz 计算核对端点内能，并通过独立积分
检查第一定律收支。这些检查验证时间积分和热力学一致性；反应数据的验证仍以所引
核数据和原网络对照为依据。

历史上随积分方法而变化的端点快照保持不变，作为回归记录保留。当前积分精度使用
独立积分参考验收，不把旧求解器的输出当作精确解。

BE_NR 将局部时间误差估计与 Newton 收敛分开：线性/Newton 方程解到机器精度，
并不代表时间积分准确。共用控制器在收紧容差后改善积分误差，解析容差细化对照
对此进行检查。

这些 ODE 修改没有更换反应率、EOS 表、NSE、接受态的组分能量定义或物理闭合预算。
生成式网络的能量求和先扣除共同的守恒重子质量基准，数据仅来自上游输出质量，
且检查每个反应的重子数守恒；它改变能量零点，不引入另一套核质量约定。
[燃烧](../../validation/burn/README.zh-CN.md)和
[网络](../../validation/network/README.zh-CN.md)验证记录分别列出这些科学检查、
主程序验证、持续运行和 sanitizer 验收。

项目自有 EOS 解析项、输运和 NSE 常数使用 CPU/CUDA 共用的一套 SI/CODATA 2022
数值。Timmes 反应数据与 pynucastro 生成资产保留各自声明的数据约定。参考计算
应采用对应的常数和数据；旧十六进制快照保留其历史背景。
详见[唯一常数定义](../../src/physics/constant/README.md)。

最后更新：2026-09-07。
