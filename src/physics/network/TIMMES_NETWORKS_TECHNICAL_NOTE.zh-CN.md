# Timmes C++ 核反应网络：实现、验证与使用说明

> 状态：CPU/OpenMP 实现已完成原 Fortran 对照验证。本文面向后续使用者和维护者，说明网络来源、数值约定、误差、并行方式以及当前限制。

## 1. 来源与实现边界

本目录中的 `iso7`、`aprox13`、`aprox19` 和 `aprox21` 是根据项目提供的经典 Timmes 小型核反应网络 Fortran 源包逐项转写的 C++ 实现。反应率公式、正反应/逆反应关系、核素数据、筛选修正、近似平衡分支、RHS 和能量释放约定均以对应的 `public_*.f90` 为基准。

这不是运行时调用 Fortran 的包装层，也不是 pynucastro 生成代码：

- 运行时不依赖 Fortran、Python 或 pynucastro；
- 网络代码位于 `src/physics/network/`，由 C++ 直接编译；
- Helmholtz EOS 仍需运行参数指定真实的 Helmholtz 表；
- 物理验收以原 Fortran 在相同状态、相同 EOS 表下的输出为判据。

本次重构由 AI 辅助完成代码抽取、机械转写、接口适配和测试构建。AI 生成或修改的代码不能仅凭代码审阅视为物理正确；下文列出的原 Fortran 数值对照才是当前版本的验收依据。

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

自热温度方程与原 Timmes 实现保持一致：

```text
dT/dt = enuc / cv
```

其中 `cv` 由 Helmholtz EOS 解析计算。完整 Jacobian 使用原 Timmes 的支撑体系：

```text
J(i,j) = d(dX_i/dt) / dX_j
J(i,T) = d(dX_i/dt) / dT
J(T,j) = (d enuc / dX_j) / cv
J(T,T) = (d enuc / dT) / cv
```

这里有两个容易误改的要点：

- 温度列使用反应率的解析温度导数，不再通过扰动温度做有限差分；
- 按原 Timmes Jacobian 约定，不在温度行额外展开 `d(cv)/dX` 或 `d(cv)/dT` 的商法则项。

Backward Euler + Newton-Raphson 的线性系统为：

```text
LHS = I - dt * J
LHS * delta_U = U_old - U_k + dt * RHS(U_k)
```

屏蔽修正的组分 Jacobian 采用与原 Fortran 一致的 frozen-screening 约定。维护者不应单独改变筛选因子的组分求导方式，否则即使 RHS 相同，Jacobian 和 LHS 也会偏离基准。

## 5. 原 Fortran 数值验证

验证使用真实 Helmholtz 表，在以下 6 个状态进行：

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

最差项为 `aprox13` LHS 的 `2.850309327978e-13`，仍低于要求约 3.5 倍。误差主要来自 Fortran/C++ 浮点求值顺序差异，不能据此推断任意更长时间积分都保持同样的轨迹误差；长期物理结果仍需做守恒量、收敛性和分辨率研究。

## 6. 在线 NSE 求解器

高温高密度路径使用 `src/physics/nse/nse_solver.h` 中的 `NSESolver<NetType>`。其来源是 Frank Timmes 的 `public_nse.tbz`：该归档实际包含 47 核素 Fortran 在线求解程序，而不是预先生成的 NSE 二进制表。C++ 实现转写了其中的 Saha 方程、质量/电荷守恒残差和 2 x 2 Newton-Raphson Jacobian，并剔除了固定 47 核素数组。

求解器只从 `NetType` 读取编译期核数据：`NUM_SPECIES`、`AION`、`ZION`、`BINDING_E`、`SPIN` 和能量转换系数。`BINDING_E` 在这里必须是单个原子核的总结合能（MeV），不能把质量超额未经转换直接代入。接口变量沿用历史名称 `Y_old/Y_out`，实际输入输出均为质量分数 `X_i`；能量闭包内部使用摩尔丰度 `X_i/A_i`。

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
- 组分循环使用 `omp simd`，求解器没有共享可变状态，可安全放在外层单元 OpenMP 并行区内；
- `iso7` 和 `aprox13` 的全部核素均满足 `Z/A=0.5`，两条守恒方程线性相关。代码会检测该退化情形，固定 `mu_n=mu_p` 后解一维归一化方程；这两个网络只在 `Ye=0.5` 时存在受限 NSE 解；
- `aprox19`/`aprox21` 同时含有物理量子态相同的 `h1` 和 `prot` 记账条目。NSE 统计和中排除重复的 `h1`，平衡自由质子写入 `prot`，避免重复计算质子简并度。

这里得到的是当前网络核素集合上的“网络受限 NSE”。`iso7`/`aprox13` 没有自由核子和中子丰核素，因此不能替代原 47 核素完整 NSE；它们在高温低密度下尤其可能偏离完整 NSE。需要完整物理 NSE 时，应使用独立、覆盖充分的 NSE 核素集合并设计守恒映射，不能把缺失核素的丰度强行塞入现有小网络。

进入 NSE 后，燃烧驱动不再原样冻结组分。它联立求解：

```text
e_EOS(rho, T_new, X_NSE(T_new)) - e_old - enuc(X_old -> X_NSE) = 0
```

快速路径使用 EOS `cv` 和后续割线斜率，失败时退回有界二分；只有组分守恒与相对能量闭包都达到 `1e-12` 才接受状态。NSE 投影失败时状态保持不变并回退到正常 ODE；绝不会把未更新组分伪装为 NSE 成功。

数值验证结果：

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

## 8. CUDA 设备正确性与当前边界

构建系统提供两层开关：

```text
ARCH_ENABLE_CUDA=OFF          # 纯 CPU 构建，不查找或链接 CUDA
ARCH_ENABLE_CUDA=ON           # 同一二进制具备 CPU 与 CUDA runtime 能力
compute_backend=cpu|cuda|auto # .par 运行时请求
cuda_device=0
```

编译期 `OFF` 的二进制不能靠 `.par` 打开 CUDA。`ON` 只表示 CUDA runtime
和已注册的 device target 被编进二进制；若所选完整物理组合尚未注册 production
launcher，显式 `compute_backend=cuda` 会报错退出，绝不静默退回 CPU。当前里程碑
已经完成四个新 Timmes 网络的 device 数学正确性与一个 BE/Newton/LU 基线，但完整
Helmholtz/NSE/流体 2D launcher 尚未注册。

H100、关闭 FMA 的 CPU/GPU 全系统相对误差如下：

| 网络 | RHS | 完整 Jacobian | LHS | 温度列 | 温度/能量行 |
| --- | ---: | ---: | ---: | ---: | ---: |
| iso7 | 2.1793e-15 | 3.2230e-15 | 3.3222e-15 | 1.8993e-15 | 3.2230e-15 |
| aprox13 | 6.4300e-13 | 5.2188e-13 | 5.2189e-13 | 3.7522e-13 | 5.2188e-13 |
| aprox19 | 3.7957e-14 | 9.2546e-14 | 9.2640e-14 | 5.0480e-14 | 9.2546e-14 |
| aprox21 | 1.2590e-13 | 2.3197e-13 | 2.3202e-13 | 5.3652e-14 | 2.3197e-13 |

`aprox19/21` 的高温近抵消列若单列独立归一，诊断值分别为
`2.5691e-12` 和 `6.6134e-12`；对应绝对差为 `2.5670e4` 和 `4.39536e5`，
列尺度为 `9.9919e15` 和 `6.6461e16`。因此全矩阵规范满足 `<1e-12`，但不能
宣称每个近零抵消列各自归一也全部 `<1e-12`。

aprox19/21 的 composition Jacobian 已改成生成的 fixed-rate 显式项，仅对
He4/H1/neutron/proton 四个平衡闭包列追加 `Dual<1>` 链式修正。它与旧逐列 Dual 设备
结果的全矩阵相对差分别为 `2.96e-18` 和 `5.21e-17`。H100 上 8,192 cells、40 次重复的
单次中位量级从约 `1.866 ms→0.3085 ms`（aprox19，`6.05x`）和
`2.473 ms→0.3473 ms`（aprox21，`7.12x`）。该优化仍为 composition-Jacobian 局部
基准，不代表完整 ODE 或 2D 加速。

`aprox13` 的独立 Backward-Euler/Newton/DenseLU CPU/GPU 基线覆盖 7 个 stiff
成功态和 1 个预期失败态：X 相对误差 `1.44e-21`、T 误差 0、BE 残差
`3.31e-20`、核能相对差 `1.71e-12`，标志全部一致。该 one-thread-per-cell
基线使用 255 registers/thread、约 10,064 bytes stack/thread，理论 occupancy
仅 12.5%；它用于正确性验收，不是生产性能实现。

one-warp-per-cell 协作 LU/Newton 原型与 one-thread 基线数值逐位一致，并把寄存器从
255 降到 128、stack 从 10,064 B 降到 7,208 B、理论 occupancy 从 12.5% 提高到
25%。但 8,192 cells 的实测吞吐只有基线的 `0.156x`（慢约 6.41 倍）：rate/Jacobian
仍由 lane 0 独占，其他 31 lanes 空转，而且 launch bound 引入更多 spill。因此该原型
只保留作资源与正确性实验，禁止注册到 production。下一步必须把 rate/Jacobian 项并行
分发到 warp，或拆成 rate/Jacobian kernel 与 cooperative-LU kernel 后再重新基准。

当前还提供 validation-only 的 aprox13 批量燃烧 ABI：设备端 SoA 输入/输出、异步 stream、
无内部分配/传输/同步、每单元状态位以及失败事务回滚。8-cell、stride=11 的 H100 接口测试
通过，最大 `|sum(X)-1|=3.33e-16`。它使用固定 `cv` 和固定子步，不等价于 Helmholtz、NSE
或生产自适应 ODE，因此没有加入完整 2D runtime registry。

current aprox13 数学算子的正式微基准位于
`docs/benchmarks/APROX13_CUDA_MICROBENCHMARK_2026-07-16.md`。256K cells 时，
H100 相对 16 线程 CPU 的 kernel-only 加速为 `66.982x`，含该批次
H2D+kernel+D2H 为 `47.239x`。这不是 ODE、NSE、流体或 2D 端到端加速比。

### 8.1 BD 与 ROS4 的 CPU 验证状态

2026-07-16 使用真实 Helmholtz 表、当前四个 Timmes 网络和 8 × 2 Cellular 单步进行了
ODE 分发回归。BD 在 `dt=1e-14 s` 下完成 iso7、aprox13、aprox19、aprox21 四种矩阵
维度，所有输出有限，species count 分别为 7、13、19、21，最终
`max|sum(X)-1| <= 2.2204e-16`。aprox13 的 BD 与 BE_NR 对照中，能量相对差约
`3.51e-15`、压力相对差约 `7.34e-15`，C12/He4 等主组分绝对差不超过
`1.23e-15`。接近零的 trace species 应同时看绝对误差，不能只引用被极小分母放大的相对值。

BD 的最高阶提前退出条件原来可能在 `k=MAX_K-1` 时访问 `n_seq[k+1]`；现已增加
`k + 1 < MAX_K` 边界条件。ROS4 在相同 Helmholtz/aprox13 回归中虽然返回成功，但明显
欠反应：C12/Ne20/O16 的反应进度仅为 BE_NR 的一小部分。当前 runtime 因此对
`ode_solver=ROS4` 明确报错，保留源码供后续重新核对 Rosenbrock tableau 与误差估计器，
但不把它注册为生产能力。可用的 CPU ODE 选择为 `BE_NR` 和 `BD`。

## 9. 使用方法

在参数文件中选择网络：

```text
use_burn = 1
network_name = iso7       # 或 aprox13 / aprox19 / aprox21
use_nse = 1               # 默认开启在线网络受限 NSE；设为 0 可禁用
nseTempThreshold = 4.5e9
nseDensThreshold = 1.0e6
ode_solver = BE_NR
linear_solver = DenseLU
eos_type = helmholtz
eos_table_path = /absolute/path/to/helm_table.dat
```

构建时不要允许 OpenMP 请求静默退化为串行版本；CMake 已使用 `find_package(OpenMP REQUIRED)`。运行时可用环境变量控制线程数，例如：

```bash
OMP_NUM_THREADS=16 <build-dir>/bin/ARCH CellularDet case.par
```

程序启动信息会显示实际的 `Species Count` 和 `OpenMP: ON (max threads=...)`。应检查它们是否与参数文件一致。

## 10. 当前限制与禁止事项

1. `aprox19`/`aprox21` 中依赖 Helmholtz 电子化学势 `eta_e` 的弱反应入口当前保持显式零值；现有 ARCH 网络接口没有把 `eta_e` 传入网络。这一限制来自实现边界，不能用任意常数或未经验证的拟合悄悄替代。
2. 当前实现是基于所选小网络的在线 Saha NSE，并非 47 核素预制表。`iso7`/`aprox13` 仅支持 `Ye=0.5` 的退化受限解；`aprox19`/`aprox21` 的弱反应仍为零，因此 NSE 入口使用进入前的守恒 `Ye`，不会自行演化电子分数。
3. 通过局部 RHS/Jacobian/LHS 对照不等于已经得到分辨充分的二维胞状爆轰。冲击波、诱导区和反应区仍需 ZND/CJ 初始剖面或足够的网格/AMR 收敛性验证。
4. 不得重新接入旧 pynucastro 网络来替代这里的实现，也不得把旧网络生成的图与本验证表混合。
5. 若修改反应率、核素顺序、能量权重、筛选、EOS `cv`、温度行/列或 ODE 投影逻辑，必须重新运行四网络的原 Fortran RHS/Jacobian/LHS 验证。

## 11. 后续维护验收清单

- [ ] 四个网络报告的 species count 分别为 7、13、19、21；
- [ ] 6 状态原 Fortran RHS/Jacobian/LHS 最大相对误差均小于 `1e-12`；
- [ ] 温度导数路径中没有有限差分温度扰动；
- [ ] CUDA 验证使用当前四网络，并分别报告 RHS/Jacobian/LHS/温度行列；
- [ ] 显式 CUDA 请求在未注册组合上 fail-loud，没有静默 CPU fallback；
- [ ] 使用与基准一致的真实 Helmholtz 表；
- [ ] OpenMP 1/多线程结果通过确定性比较；
- [ ] NSE 的 `sum(X)`、`Ye` 和 EOS/结合能闭包均小于规定误差，且未重新引入直接返回的组分冻结旁路；
- [ ] 长时间流体-燃烧耦合运行没有 NaN、密度 floor、能量 cap 或组分和漂移；
- [ ] 二维爆轰结论附带分辨率/收敛性证据，而不仅是视觉图像。

最后更新：2026-07-16。
