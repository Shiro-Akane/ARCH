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

## 6. OpenMP 并行特征

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

在 64 x 16、`aprox13`、关闭 NSE bypass、热点温度 `3e9 K` 的 3 步燃烧测试中，1 线程和 16 线程在步骤 0--3 的每份输出上均有 20 个同名 HDF5 数据集，且全部逐位一致。测试中生成了初始为零的 Ne20，因此确实经过网络/ODE/LU 燃烧路径，而非纯流体或 NSE bypass。

## 7. 使用方法

在参数文件中选择网络：

```text
use_burn = 1
network_name = iso7       # 或 aprox13 / aprox19 / aprox21
ode_solver = BE_NR
linear_solver = DenseLU
eos_type = helmholtz
eos_table_path = /absolute/path/to/helm_table.dat
```

构建时不要允许 OpenMP 请求静默退化为串行版本；CMake 已使用 `find_package(OpenMP REQUIRED)`。运行时可用环境变量控制线程数，例如：

```bash
OMP_NUM_THREADS=16 ./bin/ARCH CellularDet case.par
```

程序启动信息会显示实际的 `Species Count` 和 `OpenMP: ON (max threads=...)`。应检查它们是否与参数文件一致。

## 8. 当前限制与禁止事项

1. `aprox19`/`aprox21` 中依赖 Helmholtz 电子化学势 `eta_e` 的弱反应入口当前保持显式零值；现有 ARCH 网络接口没有把 `eta_e` 传入网络。这一限制来自实现边界，不能用任意常数或未经验证的拟合悄悄替代。
2. 高温高密度下的 NSE bypass 是积分策略，不是网络正确性的证明。网络验收测试应明确关闭 bypass。
3. 通过局部 RHS/Jacobian/LHS 对照不等于已经得到分辨充分的二维胞状爆轰。冲击波、诱导区和反应区仍需 ZND/CJ 初始剖面或足够的网格/AMR 收敛性验证。
4. 不得重新接入旧 pynucastro 网络来替代这里的实现，也不得把旧网络生成的图与本验证表混合。
5. 若修改反应率、核素顺序、能量权重、筛选、EOS `cv`、温度行/列或 ODE 投影逻辑，必须重新运行四网络的原 Fortran RHS/Jacobian/LHS 验证。

## 9. 后续维护验收清单

- [ ] 四个网络报告的 species count 分别为 7、13、19、21；
- [ ] 6 状态原 Fortran RHS/Jacobian/LHS 最大相对误差均小于 `1e-12`；
- [ ] 温度导数路径中没有有限差分温度扰动；
- [ ] 使用与基准一致的真实 Helmholtz 表；
- [ ] OpenMP 1/多线程结果通过确定性比较；
- [ ] 长时间流体-燃烧耦合运行没有 NaN、密度 floor、能量 cap 或组分和漂移；
- [ ] 二维爆轰结论附带分辨率/收敛性证据，而不仅是视觉图像。

最后更新：2026-07-15。
