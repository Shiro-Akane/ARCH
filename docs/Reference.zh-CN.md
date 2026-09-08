# ARCH 研究与 API 参考

英文原文：[Reference.md](Reference.md)。英文版是唯一规范文本；接口或行为变化必须先更新英文版。若中英文内容不一致，以英文版为准。

你可以将本文档当作在成功运行首次模拟之后的速查手册。在 ARCH 的术语体系中，一个“算例”（case）定义了流体的初始状态；一个“策略”（policy）代表某种可替换的数值方法或物理模型；而“后端”（backend）则决定了这些计算最终是在 CPU 还是 GPU 上执行。此外，API 规定了你可以调用的具体函数与数据类型，而它对应的*契约*（contract）则明确约定了你可以安全依赖的输入、输出以及各类前置假设条件。

我们的核心数值术语与计算的物理流程紧密对应：
- **网格**（Mesh）：将连续的物理空间划分为离散的计算单元。
- **重构**（Reconstruction）：用于精确估计各个单元面上的状态。
- **通量**（Flux）：描述跨越每个单元面的物理输运量。
- **时间积分器**（Time integrator）：负责将整体单元状态沿时间轴向前推进。
- **状态方程（EOS）**：通过联系压力、密度和内能等热力学量来使方程组闭合。
- **自适应网格细化（AMR）**：动态地仅在需要更高空间分辨率的位置布置更小的计算单元。
- **核燃烧**（Nuclear burning）：通过求解耦合的常微分方程（ODE）来演化流体的组分和温度。
- **核统计平衡（NSE）**：在当前激活的反应网络内，提供一个瞬时的平衡组分状态。

## 目录

1. [权威性与稳定性](#权威性与稳定性)
2. [功能矩阵](#功能矩阵)
3. [运行时架构](#运行时架构)
4. [精度契约与工程妥协](#精度契约与工程妥协)
5. [构建注册和命令行](#构建注册和命令行)
6. [参数解析](#参数解析)
7. [参数参考](#参数参考)
8. [AMR 与 plot 变量词汇](#amr-与-plot-变量词汇)
9. [稳定算例 API](#稳定算例-api)
10. [源码扩展接口](#源码扩展接口)
11. [HDF5 与重启格式](#hdf5-与重启格式)
12. [已知限制](#已知限制)
13. [源码索引](#源码索引)

## 权威性与稳定性

ARCH 暴露两个接口层级：

- **稳定算例 API**：`simulation/<Case>/` 下文件使用的源码表面，包括 `UserInterface.h`、`GlobalDefs.h`、`Setup`/`Init` 契约、`SpeciesManager` 和公共 `ProblemHelper` 函数。算例只包含这两个 ARCH 头文件；具体 EOS、dispatch、AMR 和 driver 头文件不属于该表面。
- **源码扩展 API**：在仓库内部添加数值或物理策略时使用的模板或虚函数契约。扩展实现依赖所属模块的头文件，与调用者一起编译和测试。

本文中的稳定性标签含义如下：

| 标签 | 含义 |
| --- | --- |
| Stable | 编写模拟算例时使用的受支持接口。 |
| Source extension | 与仓库一起编译和测试的扩展接口。 |
| Internal | 不属于算例表面的驱动/AMR 实现细节。 |
| Experimental | 已实现，但验证或接口稳定化尚未完成。 |
| Reserved | 已解析或命名，留待未来实现。 |

ARCH 构建一个可执行文件，内部 object target 按功能拆分；其扩展契约工作在源码层。

科学来源与接口稳定性分开记录。反应网络、NSE 公式和 Helmholtz EOS 可追溯到 Frank Timmes；恒星热传导数学的直接软件来源为 AMReX-Astro Microphysics。逐文件边界和保留条款见 [`THIRD_PARTY_NOTICES.zh-CN.md`](../THIRD_PARTY_NOTICES.zh-CN.md)。除非文件头或该说明明确指出，其他模块不声明外部来源。

## 功能矩阵

### 执行与网格

| 功能 | 接受值或接口 | 当前状态 | 说明 |
| --- | --- | --- | --- |
| Host 执行 | `compute_backend = cpu` | 支持 | OpenMP 在构建时配置。 |
| CUDA 执行 | `compute_backend = cuda/auto` | 支持 | 使用 `ARCH_ENABLE_CUDA=ON` 构建；显式 CUDA fail-closed，`auto` 只能在构造前回退。 |
| 维度 | 正的 `nblockx1`；尾部 block 数可为零 | 支持 | `nblockx2=0,nblockx3=0` 为 1D；`nblockx3=0` 为 2D。 |
| 几何 | `cartesian`、`cylindrical`、`spherical` | CPU 与 CUDA 均支持 | 名称不区分大小写并规范保存。两后端共用物理单元体积、面面积、CFL 长度、扩散间距和几何源项。 |
| AMR | `lrefinemax >= 0` | CPU 与 CUDA 均支持 | 每个活动维固定 16 个单元的 block 尺寸。topology/Morton 决策留在 Host；指标、守恒 migration、ghost 与 reflux 在 device 调用共用数值叶子。 |
| 自重力 | `gravity_type = self` | 不可用 | capability gate 会在策略构造前拒绝。 |
| Jeans 场 | `JENS` | 预留 | 解析器警告并关闭。 |

CUDA 已实现笛卡尔、柱坐标和球坐标下的一维、二维与三维流体计算，支持已注册的通量、重构和时间推进组合，以及 Ideal/Helmholtz/Tabular3D/Tabular4D EOS 和 RKL1/RKL2 扩散；这些模块使用共用几何定义。二维球坐标采用 ARCH 的极坐标 `(r,phi)` 约定。被动输运与 AMR 的临时存储按运行时组分数量分配；DenseLU 则有独立的 31 个总 ODE 方程限制。动态 AMR 由主机制定拓扑计划，设备计算指标、事务性迁移状态，并执行多块交换与流体/扩散通量修正。重启采用共用检查点格式；输出所需状态显式同步到主机后，由共用写入器处理。

四个内置燃烧网络支持 DenseLU、可选的 cuDSS 求解及 NSE。生成网络包通过 CMake 的
[设备数学包契约检查](../src/physics/network/custom/README.md)，且清单声明
`device_callable_math=true` 后，在 CPU 与 CUDA 上使用同一套数学。
已识别的内嵌弱反应率表由各后端分别保存为只读数据。通过检查的仅主机网络包在 CPU 上执行。
cuDSS 要求实际链接其可选求解库，KLU 仅用于
CPU；不兼容的显式后端／求解器组合会被拒绝。外部重力在两端调用同一个逐阶段
源项算子。[后端指南](CudaBackendStatus.zh-CN.md)说明执行职责；
[验证索引](../validation/README.zh-CN.md)记录数值检查、应用结果与发布验收状态。

### 数值策略

| 类别 | 运行时取值 | 状态和精确行为 |
| --- | --- | --- |
| 通量 | `SW`、`VL`、`Roe`、`HLL`、`HLLC` | 已 dispatch |
| 重构 | `pcm`、`donor_cell`；`muscl`、`plm`；`ppm` | 所列 alias 已 dispatch；`weno5` 只出现在 ghost 数计算中 |
| MUSCL limiter | `minmod`、`superbee`、`vanleer`、`mc` | 已 dispatch；包括 `none` 在内的未知值回退到 MinMod |
| 流体时间推进 | `Euler`、`RK1`；`RK2`、`SSPRK2`；`RK3`、`SSPRK3` | Euler、SSPRK2、SSPRK3 |
| 扩散时间推进 | `RKL2`（默认）、`RKL1` | 独立扩散算子中 RKL2 为二阶；RKL1 是可选一阶方法 |
| EOS | `ideal`、`tabular`、`helmholtz` | CPU 与 CUDA 均已 dispatch |
| 重力 | `none`、`external` | CPU/CUDA 共用逐阶段源项；未知字符串与 `self` 会在构造前被拒绝 |
| 网络 | `aprox13`、`aprox19`、`aprox21`、`iso7`；`custom:<id>` | 内置网络及 CMake 自动发现的生成网络 |
| 燃烧 ODE | `BE_NR`、`ROS4`、`BD` | 均已 dispatch，并由单区 CPU 回归覆盖 |
| 线性求解 | `Auto`、`DenseLU`、`SparseKLU`、`cuDSS` | 不区分大小写；接受 `dense_lu`、`sparse_klu`、`cu_dss` 别名。`Auto` 对不超过 31 个总 ODE 方程选择 DenseLU，计数包含温度及可选辅助能量状态。更大系统在 CPU 上使用 SparseKLU，在 CUDA 上使用 cuDSS。SparseKLU 仅适用于 CPU，cuDSS 仅适用于 CUDA；不兼容的显式组合会在后端构造前报错，不替换求解器。缺少求解库或已注册的 CUDA 网络执行代码时也会明确报错。 |

策略名称按 ASCII 大小写不敏感；但不同 dispatcher 接受的 alias 与 fallback 行为仍不一致。

## 运行时架构

可执行程序遵循以下生命周期：

```text
main(argc, argv)
  -> RuntimeParams::Load(.par)
  -> ProblemRegistry::Create(problem name)
  -> case.Setup(config, species)
  -> DispatchSolver
       -> 解析已注册 execution plan 与运行要求
       -> probe build/device 并查询 CPU/CUDA capability gate
       -> 解析 backend（`auto` 唯一允许回退的位置）
       -> allocate AMRControl/MemoryPool
       -> initialize or restart leaf state
       -> dispatch EOS, burn handle, gravity, time integrator, flux, reconstruction
       -> run_simulation
            -> regrid and IO scheduling
            -> choose hydro/diffusion/burn time restrictions
            -> B(dt/2) D(dt/2) H(dt) D(dt/2) B(dt/2)
            -> advance time
```

多维流体 RHS 在一次 RK stage 更新前累加所有活动方向的面散度。几何源项和外部重力源项共享流体 stage 计算。

AMR 拥有拓扑、block 内存、ghost exchange 和 flux register。流体与多 block 扩散都会登记粗细通量，并在各自组合更新后执行 reflux。

## 精度契约与工程妥协

阶数说明将分量方法和耦合计算分开。

### 时间组合

当前 driver 使用对称组合：

```text
B(dt/2) -> D(dt/2) -> H(dt) -> D(dt/2) -> B(dt/2)
```

其中 `B`、`D` 和 `H` 分别表示燃烧、扩散和包含重力/几何源项的流体。该组合意味着：

- 对称 Strang 组合使耦合问题最高为二阶，即使流体子步使用 SSPRK3；
- 默认 RKL2 子步对独立扩散算子是二阶，但完整分裂方法仍最高二阶；可选 RKL1 为一阶；
- 在声称耦合二阶收敛前，必须单独验证自适应刚性燃烧求解器和所有 NSE 投影；
- regrid、依赖解的 limiter、激波和保护机制触发都可能使渐近阶数无法显现。

### 空间重构与 AMR

- PCM 名义一阶。
- MUSCL/PLM 在 limiter 触发前的光滑区域名义二阶。
- PPM 提供名义三阶重构；耦合时间阶数遵循 Strang 组合。
- 均匀网格光滑平流基线中，PPM 最后一对分辨率的 L1 阶数为 3.993，通过 2.7 验收阈值。该光滑接触间断结果不构成通用四阶声明。
- 2:1 粗细界面处，ARCH 使用 `AMRInterfaceReconstruction.h` 中的二阶 MUSCL-MinMod 面重构。
- 激波收敛使用相应范数和激波问题阶数；间断会降低局部阶数。

### 会改变守恒性的保护机制

`perform_stage_update` 在流体 stage 后执行稳健性修复：

- 低于 `sml_rho` 的密度会被重置，动量清零并重建能量；
- 速度模由硬编码的 `1e10` 上限截断；
- 比内能限制到 `[min_eint, max_eint]`；
- 负质量分数被截为零，所有分数重新归一化；
- 当组分和接近零时，安装均匀组分。

这些工程保护在守恒通量更新以外修改状态。运行记录应将修复贡献与守恒量和 L1/L2 指标一起保存。

生产 PPM 路径重构密度、速度、压力和组分，再调用选定 EOS 重建总能量。它对密度和压力取 floor，将组分限制在 `[0,1]` 并归一化界面组分。

### 构建复现性与编译期妥协

Release 保留优化和 `-march=native`，但共用构建契约在支持的 GNU/Clang host 编译器上显式关闭 fast-math 与浮点收缩（`-fno-fast-math -ffp-contract=off`），NVIDIA CUDA 则使用 `--fmad=false --ftz=false --prec-div=true --prec-sqrt=true`。Host 的链接选项也被精心配置，以防止 `fast-math` 的行为泄漏到启动状态中。这些设计选择刻意保留了精确的补偿求和与严格的浮点求值顺序。然而，它们并不能保证跨机器的逐位复现（bitwise reproducibility）。正因如此，我们的[验证](../validation/README.zh-CN.md)记录必须详尽登记每次验证运行所使用的编译器版本、编译参数、OpenMP 线程数、硬件配置以及所接受的数值容差。

CPU dispatch 翻译单元使用 `-O1` 和 `-fno-inline-functions-called-once`，控制积分器、通量、重构与 EOS 组合带来的编译内存开销。工具链支持时，Release 仍启用 LTO。燃烧策略在进入完整通量矩阵前通过 `BurnerHandle` 类型擦除，减少各流体路径重复实例化网络和 ODE 的开销。CUDA 按同样的功能职责拆分，并单独控制后端编译任务的并发数。

## 构建、注册和命令行

### 构建契约

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 1
```

CMake 在配置时获取 HighFive，并链接 HDF5 C++/HL 库。KLU 默认启用：CMake 先查找已安装的 KLU package；若不存在，则获取固定的 SuiteSparse v7.13.0，只构建 KLU、BTF、AMD、COLAMD 与 SuiteSparse_config。EOS `.dat`/`.h5` 资源可能需要 Git LFS。

CUDA 稀疏燃烧额外通过默认开启的 `ARCH_ENABLE_CUDSS=ON` 可选发现 cuDSS，可用 `CUDSS_ROOT` 指向安装前缀，用户目录安装也受支持。ARCH 适配层要求已审查的 0.8 API 与匹配的运行库版本；某个网络/EOS 组合在其已注册执行代码与库实际链接后才可用。缺少 cuDSS 不妨碍只用稠密求解的 CUDA 构建，但会拒绝稀疏请求。[README 构建示例](../README.zh-CN.md#构建)展示了隔离可执行文件输出与 `tools/run_memory_guarded.py`。不确定可用内存时先使用 `--parallel 1`，再按测量结果调整重型任务与总任务的并发上限。保护工具允许有限的 swap 增长，在内存或 I/O 持续停顿时停止编译。模拟容量按具体工作负载另行测量。

`ARCH_CUDSS_IR_STEPS` 是非负整数 CMake 选项（默认 `2`），控制 cuDSS 在设备端执行的线性解精化轮数；设为 `0` 可供诊断关闭。它只影响线性求解适配层，不修改 ODE 容差或 Auto 阈值：不超过 31 个总方程使用 DenseLU；30 个核素加温度和弱能量积分已经是 32 阶系统。适配层保持 cuDSS `IR_TOL=0`，求解后仍逐分量检查原矩阵残差；修正量必须通过 ARCH 的精度检查才会被接受，库返回成功本身不足以满足要求。该选项仅影响主机端适配层的编译，不重新实例化 CUDA 网络/EOS 计算核。参见 [cuDSS 精化契约](https://docs.nvidia.com/cuda/cudss/types.html#cudssconfigparam-t)。

相关 cache 选项为 `ARCH_ENABLE_KLU`（默认 `ON`）、`ARCH_FETCH_SUITESPARSE`（默认 `ON`）、`ARCH_CUSTOM_NETWORK_ROOT`（生成 package 根目录）和 `ARCH_CUSTOM_NETWORKS`（可选的分号分隔 custom ID 列表）。`BUILD_TESTING=ON` 注册维护中的理想气体 tabular EOS 与 161 方程 KLU 回归；restart、AMR、真实来源表和生成式网络的审计证据统一保留在 `validation/`，不为每次审计新增 test target。

源码通过 CMake `GLOB_RECURSE CONFIGURE_DEPENDS` 从 `src/core`、`src/physics`、`src/numerics`、`src/io` 和 `simulation` 中发现；由专用注册表处理的生成式 custom network 子树会被排除。新增 `.cpp` 后构建系统会自动重新生成，也可显式重新执行 CMake configure。

### 算例注册

维护中的注册宏为：

```cpp
REGISTER_PROBLEM_CLASS("RuntimeName", CaseClass);
```

它在静态初始化期间注册 factory。重复名称会覆盖已有项，算例类必须可默认构造。若使用一对自由函数定义算例，可用：

```cpp
REGISTER_PROBLEM("RuntimeName", setup_function, init_function);
```

### 命令行

```text
./bin/ARCH <ProblemType> <ParFile>
```

可执行程序接受这两个位置参数。目前没有 list、dry-run、参数覆盖、schema dump 或 validation 子命令。

## 参数解析

`ConfigParser` 对每个非空行读取第一个 `=`，删除 `#` 后文本，去除空白，并在键重复时保留最后一个值。键区分大小写。

重要行为：

- 整数和浮点核心值使用 `std::stoi`/`std::stod`；
- Boolean 只接受不区分大小写的 `true` 或 `false`；数字 `0`/`1` 与 `on`/`off` 会被拒绝；
- geometry、boundary、gravity 与 compute-backend token 在参数加载时统一规范为 ASCII 小写；
- 未知键保留在 `SimConfig::custom_params` 或 `custom_string_params`，不提供拼写验证；
- 自定义键缺失时，`SimConfig::Get<T>` 返回调用者提供的默认值；
- 轻量 `pi` 表达式解析器用于域边界和外部重力分量，支持 `pi`、`-pi`、`2*pi`、`pi*2` 和 `pi/2` 等形式；
- 路径相对于进程工作目录解释；
- EOS dispatch 会移除 `eos_table_path` 的引号，普通字符串则保留解析器文本。

无效选项按下表处理：

| 无效选择 | 当前行为 |
| --- | --- |
| flux | 警告，选择 HLLC |
| reconstruction | 警告，选择 PCM |
| MUSCL limiter | 警告，选择 MinMod |
| hydro integrator | 警告，选择 SSPRK2 |
| gravity | 在统一解析配置时抛出异常 |
| EOS、network、ODE、linear solver | 抛出异常 |
| diffusion integrator | 在统一解析配置时抛出异常 |

科研工作流必须检查启动时的 Strategy 行，并将 fallback 警告视为配置失败。

## 参数参考

下列默认值来自 `RuntimeParams::Load`，其优先级高于 `GlobalDefs.h` 中的默认成员初始化值。

### 网格与几何

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `geometry` | string | `cartesian` | `cartesian`、`cylindrical`、`spherical` |
| `nblockx1` | int | `1` | 正的 root block 数 |
| `nblockx2` | int | `1` | `<=0` 移除轴 2 |
| `nblockx3` | int | `1` | `<=0` 移除轴 3；轴 3 要求轴 2 活动 |
| `max_blocks` | int | `2000` | 严格的 AMR 内存池容量 |
| `x1_min/max` | expression | `0/1` | 活动轴必须 max > min |
| `x2_min/max` | expression | `0/1` | 角度限制取决于几何/维度 |
| `x3_min/max` | expression | `0/1` | 角度限制取决于几何/维度 |
| `x1l_boundary_type` | string | `outflow` | `outflow`、`reflect`、`periodic` |
| `x1r_boundary_type` | string | `outflow` | 同上 |
| `x2l_boundary_type` | string | `outflow` | 同上 |
| `x2r_boundary_type` | string | `outflow` | 同上 |
| `x3l_boundary_type` | string | `outflow` | 同上 |
| `x3r_boundary_type` | string | `outflow` | 同上 |

逻辑坐标含义：

| 几何 | 1D | 2D | 3D |
| --- | --- | --- | --- |
| Cartesian | x | x, y | x, y, z |
| Spherical | r | r, phi | r, theta, phi |
| Cylindrical | r | r, phi | r, z, phi |

转换后的 `PointCoords` 固定以 `(0,0,0)` 为原点。

所有 `bool` 参数均不区分大小写地接受 `true` 或 `false`，例如 `TRUE`、`False` 和 `tRuE`。数值 `0/1`、`on/off`、`yes/no`、部分匹配及其他拼写都会被拒绝，错误信息会指出参数名。

### 流体数值方法与执行

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `solver` | string | `SW` | `SW`、`VL`、`Roe`、`HLL`、`HLLC` |
| `reconstruct` | string | `pcm` | `pcm`、`donor_cell`、`muscl`、`plm`、`ppm` |
| `limiter` | string | `minmod` | 仅 MUSCL：`minmod`、`superbee`、`vanleer`、`mc` |
| `time_integrator` | string | `RK2` | `Euler/RK1`、`RK2/SSPRK2`、`RK3/SSPRK3` |
| `timeintegrator` | string | — | 仅在 `time_integrator` 缺失时采用的别名 |
| `cfl` | double | `0.8` | 显式流体 CFL；加载时不检查范围 |
| `EntropyFix` | bool | `true` | 启用 entropy-fix 平滑 |
| `EntropyFixCoefficient` | double | `0.1` | 启用 entropy fix 时使用 |
| `sml_rho` | double | `1e-12` | 密度修复阈值 |
| `min_eint` | double | `1e-10` | 正比内能下限 |
| `max_eint` | double | `1e21` | 比内能上限 |
| `compute_backend` | string | `cpu` | `cpu`、`cuda` 或 `auto`；显式 CUDA fail-closed，`auto` 只能在构造前回退 |
| `cuda_device` | int | `0` | CUDA probe、构造与生命周期操作使用的 runtime device ordinal |

### AMR

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `lrefinemin` | int | `0` | 已存储；依赖非零最小层级前应验证当前层次行为 |
| `lrefinemax` | int | `0` | 最大细化层级；零关闭细化 |
| `regrid_interval` | int | `2` | 必须为正 |
| `refine_var` | string list | `DENS` | 逗号或 `+`；规范场名或已注册核素 |
| `refine_threshold` | double | `0.8` | Lohner 指标，范围 `[0,1]` |
| `derefine_threshold` | double | `0.2` | 必须 `>=0` 且小于 refine threshold |

即使 `lrefinemax = 0`，`refine_var` 仍会验证。

### EOS 与重力

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `eos_type` | string | `ideal` | `ideal`、`tabular`、`helmholtz` |
| `eos_table_path` | string | 空 | tabular/Helmholtz 必需 |
| `gamma` | double | `1.4` | 理想气体 fallback/参考 gamma |
| `gravity_type` | string | `none` | `none`、`external`；`self` 会在构造前由 capability gate 拒绝 |
| `gravity_g_x/y/z` | expression | `0` | 外部重力分量 |
| `gravity_G` | expression | `6.6743e-8` | 仅为尚不支持的自重力解析 |

对 `eos_type = tabular`，HDF5 文件声明 `table_rank = 3` 或 `4`，dispatch 自动选择相应策略。表格宜优先保存比 Helmholtz 自由能；规范化数据集、导数关系、直接热力学字段模型以及 guard-node/端点间隔规则见源码旁的 [Tabular EOS HDF5 接口](../src/physics/eos/TabularEOS.zh-CN.md)。Shen/LS/HS/CompOSE/EOSDriver 文件需要表族专用转换器，目前仓库不附带这类工具；二进制兼容性由规范化 schema 而不是上游文件名或 HDF5 容器定义。[EOS 验证记录](../validation/eos/README.zh-CN.md)汇总了包括 Shen EOS4 与 EOSDriver HShen 在内的来源表评估。

维护中的 Helmholtz 验证资源是从 [Timmes EOS 页面](https://cococubed.com/code_pages/eos.shtml)下载的 `helmholtz.tar.xz` 中的 `helm_table.dat`。它通过 Git LFS 实体化在 `EOS_toolkit/tables/helmholtz/helm_table.dat`，大小为 60,242,514 bytes，SHA-256 为 `c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`。原始表成员是验证权威。loader 使用固定 541×201 Timmes 布局并要求全部四个数据块；燃烧基线还要求上述精确 checksum。

### 燃烧、网络与 ODE

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `use_burn` | bool | `false` | 启用燃烧模块 |
| `network_name` | string | `aprox19` | 上述内置网络或任意已编译的 `custom:<id>`；生成网络要求 `use_nse = false` |
| `nuclearTempMin` | double | `1e9` | K；燃烧激活阈值 |
| `nuclearDensMin` | double | `1e-10` | g/cm3；燃烧激活阈值 |
| `smallt` | double | `1e5` | K；燃烧状态 floor |
| `smallx` | double | `1e-20` | 组分 floor |
| `enucDtFactor` | double | `1e30` | 能量释放时间步 limiter；巨大默认值实际关闭限制 |
| `use_nse` | bool | `true` | 仅为四个内置网络启用带阈值 NSE 投影 |
| `nseTempThreshold` | double | `4.5e9` | K |
| `nseDensThreshold` | double | `1e6` | g/cm3 |
| `enforce_mass_conservation` | bool | `true` | 已解析并保存；当前 burn 路径尚未消费该开关 |
| `burn_verbose_level` | int | `0` | 已解析并保存；当前 burn 路径尚未消费该级别 |
| `ode_solver` | string | `BE_NR` | `BE_NR`、`ROS4` 或 `BD` |
| `linear_solver` | string | `Auto` | 不区分大小写的 `Auto`、`DenseLU`、`SparseKLU` 或 `cuDSS`（接受 `dense_lu`、`sparse_klu`、`cu_dss` alias）；具体化规则见下文 |
| `ode_rtol` | double | `1e-4` | ODE 相对容差 |
| `ode_atol` | double | `1e-8` | ODE 绝对容差 |
| `ode_max_newton_iter` | int | `50` | 使用 Newton 时的迭代上限 |
| `ode_max_substeps` | int | `10000` | 自适应子步上限 |
| `ode_dt_safe_fac` | double | `0.9` | 自适应 controller 安全系数 |
| `ode_dt_fac_max` | double | `2.0` | 增长系数 |
| `ode_dt_fac_min` | double | `0.1` | 缩小系数 |
| `ode_initial_dt_frac` | double | `1e-3` | 初始内部子步比例 |
| `ode_use_numerical_jac` | bool | `false` | 已存储；依赖前验证具体 solver 是否使用 |
| `ode_freeze_jacobian` | bool | `false` | 已存储；依赖前验证具体 solver 是否使用 |
| `dt_init` | custom double | `1e-16` | 启用燃烧时的首个宏时间步 |
| `dt_min` | custom double | `1e-20` | 宏时间步终止阈值 |
| `tstep_change_factor` | custom double | `1.2` | 第一步后的最大宏步增长 |

`ROS4` 使用匹配的四 stage、四阶、L-stable tableau。每个内部步计算一次 Jacobian，分解一次 `I - gamma*dt*J` 并由全部 stage 复用。在 aprox13/Helmholtz 单区测试中，它通过当前 BE_NR 跨求解器容差。生产研究仍需给出子步/容差收敛序列，并比较核素和能量历史，尤其是在扩展网络或 EOS 耦合时。

BE_NR 将非线性收敛与时间精度分开：Newton 修正量先满足 ODE 误差尺度的十分之一，再以 backward-Euler 与梯形端点更新之差估计二阶局部误差；接受的解仍是一阶 backward Euler。`ode_rtol`/`ode_atol` 控制该局部估计，不构成全局相对误差上界。三种 ODE 共用固定密度第一定律的 RHS 与 Jacobian，包含 EOS 内能的组分依赖和比热导数。具体方程与能量交接见[网络技术说明](physics/TimmesNetworks.zh-CN.md#4-温度方程jacobian-与-lhs-约定)，独立时间/能量检查见[燃烧验证](../validation/burn/README.zh-CN.md)。

最后三个参数是 custom-map 控制项。`xc12` 等网络专用初始分数由所选网络的 setup 实现消费。

### 扩散

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `use_diffusion` | bool | `false` | 启用扩散模块 |
| `diff_integrator` | string | `RKL2` | `RKL1` 或 `RKL2` |
| `diff_cfl` | double | `0.8` | RKL stage/step 选择所用比例 |
| `diff_max_stages` | int | `256` | 限制 STS 多项式和宏步 |
| `use_thermal_diff` | bool | `false` | 热传导 |
| `use_viscous_diff` | bool | `false` | 动量扩散 |
| `use_species_diff` | bool | `false` | 组分扩散 |
| `nu_visc` | double | `0` | 非 Helm 下的常运动黏度 |
| `alpha_therm` | double | `0` | 非 Helm 下的常热扩散率 |
| `D_spec` | double | `0` | 非 Helm 下的常组分扩散率 |

使用 Helmholtz 扩散时，省略三个常数 override 键以选择 `diffusionCoe` 输运。只要 override 键存在就会拒绝，包括零值。

### 时间、输出与重启

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `tmax` | double | `0.1` | 目标物理时间 |
| `max_steps` | int | `-1` | 正值启用步数停止 |
| `out_dir` | string | `data` | 在日志建立前创建 |
| `base_name` | string | `arch` | 输出文件名前缀 |
| `plt_dt` | double | `-1` | 正的物理时间间隔 |
| `plt_dstep` | int | `-1` | 正的步数间隔 |
| `chk_dt` | double | `-1` | 正的物理时间间隔 |
| `chk_dstep` | int | `-1` | 正的步数间隔 |
| `plt_variables` | string list | `ALL` | 逗号或 `+`，规范场/核素 |
| `restart` | bool | `false` | 启用 checkpoint 重启 |
| `restart_file` | string | 空 | `restart = true` 时必须为非空路径 |

在第零步，ARCH 写入初始 PLT 和 CHK。达到目标时间会强制最终输出；`max_steps` 停止遵循已配置输出调度。

## AMR 与 plot 变量词汇

规范名称在 tokenization 后不区分大小写。`rho`、`p`、`u`、`v`、`w` 和 `eng` 等短 alias 会被拒绝。

| 名称 | 含义 | AMR | PLT | 可用性 |
| --- | --- | --- | --- | --- |
| `DENS` | 质量密度 | 是 | 是 | 所有模型 |
| `PRES` | 活动 EOS 的压力 | 是 | 是 | 所有 EOS |
| `TEMP` | 活动 EOS 的温度 | 是 | 是 | 所有 EOS |
| `VELX/Y/Z` | 活动逻辑方向速度 | 是 | 是 | 仅活动维度 |
| `ENER` | 总能量密度 | 是 | 是 | 所有模型 |
| `VORT` | 考虑度量的旋度模 | 是 | 是 | 已实现几何 |
| `DIVV` | 考虑度量的速度散度 | 是 | 是 | 已实现几何 |
| `ENTR` | 局部 `p/rho^Gamma1` 代理量 | 是 | 是 | 有限正 EOS 状态 |
| `ENUC` | 有符号核比能源率 | 是 | 是 | 启用燃烧 |
| `JENS` | Jeans 判据 | 预留 | 预留 | 已关闭 |
| `SPECIES` | 所有已注册核素 | 是 | 是 | 已注册组分 |
| 注册名称 | 单一核素/tracer | 是 | 是 | 不区分大小写查找 |

`CONSERVED` 选择 `DENS`、活动速度和 `ENER`。`ALL` 选择所有可用 PLT 场和已注册核素。

`ENTR` 是局部代理量 `p/rho^Gamma1`，其中活动 EOS 给出 `Gamma1 = rho*c_s^2/p`。对于常 gamma 理想气体，它是通常的不变量；对于一般 EOS 策略，它是细化代理量。它不是 EOS 返回的绝对熵，不能用来把一般状态沿等熵线移动。固定组分等熵状态必须使用 EOS 策略接口一节记录的微分热力学恒等式构造。

## 稳定算例 API

### include 表面与类契约

对 `simulation/<Case>/<Case>.cpp`：

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

这是算例可以包含的全部 ARCH 头文件；C++ 标准库头文件不受限制。算例需要的 EOS 操作通过 `ProblemHelper` 提供，因此切换运行时 EOS 不会改变算例 include，也不会把具体 EOS 策略类型暴露给用户。

相对路径假设维护中的两层算例布局。算例类由 `TypedProblemGenerator<T>` 包装并必须可默认构造：

```cpp
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

`Setup` 在分配前运行一次。`Init` 通过 OpenMP population loop 填充已分配的根 block，包括 ghost 存储。初始及后续 fine block 由守恒 AMR transfer 创建，不会再次调用 `Init` 覆盖；该函数仍必须确定且线程安全。

### `SimConfig`

```cpp
template <typename T>
T SimConfig::Get(const std::string &key, T default_value) const;

double SimConfig::GetCustomParam(
    const std::string &key, double default_value) const;
```

实际支持的 `T` 是可由已存储 `double` 转换的数值类型，或 `std::string`。已复制到 `grid`、`numerics`、`execution`、`physics`、`amr` 和 `io` 的核心值应从这些强类型结构中读取。

### `SpeciesManager`

维护中的算例侧操作为：

```cpp
int add_species(std::string name, double A, double Z, double gamma, double Cv);
int GetSpeciesID(const std::string &name) const;
int count() const;
std::string get_name(int id) const;
double get_A(int id) const;
double get_Z(int id) const;
double get_gamma_ref(int id) const;
double get_Cv_ref(int id) const;
```

ID 是注册顺序索引。`GetSpeciesID` 不区分大小写，缺失时返回 `-1`；属性 getter 不检查 ID。

### `PointCoords`

```cpp
struct PointCoords {
    double x, y, z;
    double r, theta, phi;
    double r_cy, phi_cy, z_cy;
};
```

`Grid::GetPhysicalCoords` 填充所有表示。二维 spherical 和 cylindrical 几何中，第二个逻辑坐标是平面方位角 `phi`。

### `PrimitiveData`

```cpp
struct PrimitiveData {
    double rho, u, v, w, p;
    double temperature;
    bool has_temperature;
    std::vector<double> mass_fractions;
    void SetMassFraction(int id, double value);
    void SetTemperature(double value);
};
```

框架在每次 `Init` 前重置数值，并按注册核素数预先调整组分 vector。`SetMassFraction` 可为更大的 ID 扩容，但要求 `id >= 0`。`Init` 后通过选定 EOS 推导守恒动量和能量。`SetTemperature` 选择基于温度的内能初始化，对能量构造具有高于压力的优先级；否则由 `rho` 和 `p` 定义能量。每次 `Init` 前框架都会重置 `has_temperature`。

### `ProblemHelper`

公共函数为：

```cpp
void ProblemHelper::SetupNetworkAndFractions(
    SimConfig &config,
    SpeciesManager &specs,
    std::vector<double> &default_X);

double ProblemHelper::GetPressureFromRhoT(
    const SimConfig &config,
    const SpeciesManager &specs,
    double rho,
    double temperature,
    const double *mass_fractions);

double ProblemHelper::GetRootCellWidth(
    const SimConfig &config,
    int logical_axis);

struct ProblemHelper::IsentropicState {
    double rho;
    double temperature;
    double pressure;
    double sound_speed;
};

ProblemHelper::IsentropicState
ProblemHelper::GetIsentropicStateAtPressureFactor(
    const SimConfig &config,
    const SpeciesManager &specs,
    double reference_rho,
    double reference_temperature,
    const double *mass_fractions,
    double pressure_factor);
```

手动注册其他核素前先调用网络 setup。当 manager 为空时，它调用网络的 `RegisterSpecies`。压力转换执行运行时 EOS dispatch，应放在 `Setup` 中。

`GetRootCellWidth` 返回根层级上一个有效 cell 的物理宽度；`logical_axis` 采用从 1 开始的 `1`、`2`、`3`。它使用配置中的根 block 数和编译期有效 block 尺寸。当前布局下，每个方向每个 block 有 `16` 个有效 cell；两侧各有 4 个 guard cell，因此 x 方向存储尺寸为 `16 + 8`，但这 8 个 guard cell 不属于物理域宽度。算例若要构造 cell-average 初值，应调用此函数，而不是包含内部 `AmrDefines.h`。

等熵辅助函数固定 `mass_fractions`，将 `pressure_factor` 解释为 `P_target/P_reference`，并返回匹配后的密度、温度、压力和声速。它使用活动 EOS 和下述公共 EOS-policy 路径，不假设理想气体。该操作有意限制在局部邻域；若结果相对参考态移动超过 `0.25` 的 `ln(rho)`，函数会拒绝请求。两个 EOS 辅助函数都只应在 setup 阶段调用，而不能放进逐单元 `Init` 或时间步循环。

`ProblemHelper::detail::PopulateState` 是内部初始化桥接函数。

## 源码扩展接口

以下接口均要求修改源码、修改注册、完整重建并提供新的验证证据。

### 通量策略 — Source extension

每个运行时通量以重构策略为模板并提供：

```cpp
static std::string name();
static constexpr int NG;

template <typename EosType>
static void compute_fluxes(
    const FluidState &state,
    const EosType &eos,
    const Grid &grid,
    std::vector<FluidVector> &flux_out,
    std::vector<double> &species_flux_out,
    int direction,
    double entropy_fix_coefficient = 0.0);
```

在 `DispatchImpl::select_flux` 注册新模板。通量数组使用与 `TimeIntegration::accumulate_divergence` 和 AMR 通量登记兼容的面索引；核素通量按 `species * total_size + face_index` 展平。

### 重构策略 — Source extension

具体策略提供：

```cpp
static std::string name();
static constexpr int NG;
static std::pair<FluidVector, FluidVector>
run(const FluidState &state, int left_cell_index, int stride = 1);
static void run_species(
    const FluidState &state, int left_cell_index, int species_count,
    double *X_left, double *X_right, int stride = 1);
```

在 `DispatchImpl::select_reconstruction` 注册 alias，更新 `determine_required_ng`，保持 `NG <= amr::MAX_NG`，并定义物理有效的粗细界面策略。当前宽模板策略在粗细面局部替换为 MUSCL-MinMod。

### 流体积分器与求解器边界 — Source extension/Internal

类型擦除 block operator 为 `Numerics::IHydroSolver`：

```cpp
virtual void evaluate_patch(
    amr::AMRControl*, int block_id,
    const FluidState&, const Grid&, double dt,
    std::vector<FluidVector>&, std::vector<double>&,
    const Physical::Gravity::IGravityPolicy*,
    const NumericsConfig&, double flux_weight,
    void *execution_stream) const = 0;

virtual void update_patch(
    const FluidState &old_state, const FluidState &current_state,
    FluidState &new_state,
    const std::vector<FluidVector>&, const std::vector<double>&,
    const Grid&, double old_weight, double flux_weight,
    const NumericsConfig&, void *execution_stream) const = 0;
```

积分器暴露静态模板 `solve(AMRControl&, dt, BCPolicy&, gravity, hydro, NumericsConfig)`，注册时新增独立 dispatch 翻译单元并在 `SolverDispatch.cpp` 中加入选择。Reflux stage 权重必须与 RK 求积和通量登记一致。

### EOS 策略 — Source extension

项目自有物理常数由
[`PhysicalConstants.h`](../src/physics/constant/PhysicalConstants.h)
提供唯一的 CPU/CUDA 定义，按学科组织并明确单位。当前统一采用 SI 定义值和
CODATA 2022，各 EOS 共用这些定义。π 引用 C++20 `<numbers>`，辐射和
Gaussian 电荷的派生量复用基础常数。新增常数前请查看
[常数与数据边界](../src/physics/constant/README.md)：修改该头文件不会重生成外部
EOS 表或独立维护的核反应网络数据，也不隐式转换任意 code units。
数值对比需要注明采用的常数；使用不同取值的逐位快照不能作为这组常数的参考结果。

EOS dispatch 使用静态 duck typing。`src/physics/eos/eos.h` 列出期望表面。流体、初始化、燃烧、扩散和诊断会使用：

```cpp
double get_gamma(const double *X) const;
double get_eta(double rho, double T, const double *X) const;
double get_pressure(const FluidVector &U, const double *X) const;
double get_temperature(double rho, double e, const double *X) const;
double get_pressure_from_rho_e(double rho, double e, const double *X) const;
double get_eint_from_T(double rho, double T, const double *X) const;
double get_sound_speed(const FluidVector &U, double p, const double *X) const;
double get_total_energy_primitive(
    double rho, double u, double v, double w, double p, const double *X) const;
double get_pressure_from_rho_T(double rho, double T, const double *X) const;
double get_cv(double rho, double T, const double *X) const;
double get_dp_drho_e(double rho, double e, const double *X) const;
double get_dp_de_rho(double rho, double e, const double *X) const;
void evaluate_state(eos_state_t &state) const;
const SpeciesManager *get_species_manager() const;
```

`evaluate_state` 是规范的热力学状态契约。对每个有效 `(rho,T,X)` 输入，它必须填充有限的 `P`、`E`、`cv`、`sound_speed`、`dp_drho` 和 `dp_dT`；其中压力、比内能、`cv` 和声速必须为正。`dp_drho` 表示 `(dP/drho)_e`，`dp_dT` 表示 `(dP/dT)_rho`。自由能 tabular 策略从同一个插值 Helmholtz 势导出这些量；旧 direct 策略使用已提供的导数数据集，或采用受表边界约束的局部差分，而不是返回零。详见[规范化 HDF5 契约](../src/physics/eos/TabularEOS.zh-CN.md)。

所有策略都从 `eos_Utils.h` 中的 `eos_utils::get_isentropic_state_at_pressure_factor` 获得同一套固定组分等熵算法。它用 RK4 积分

```text
d ln(T) / d ln(rho) |_s,X = (dP/dT)_rho,X / (rho cv)
```

并以 `Gamma1=rho*c_s^2/P` 在 `ln(rho)` 中求解目标压力。新增 EOS 只实现 `evaluate_state`，不得复制或特判等熵求解器。该公共工具属于源码扩展接口，simulation 算例不得直接包含；算例只能通过 `ProblemHelper` 和两个稳定公共头文件访问。

应对照每个 EOS 实现检查精确 overload 集。`eos.h` 记录 duck-typed 表面，新类型在 `EOSDispatcher::dispatch_eos` 中注册。

### 重力策略 — Source extension

从 `Physical::Gravity::IGravityPolicy` 派生：

```cpp
virtual void update_field(
    const FluidState&, const Grid&, void *execution_stream = nullptr) const = 0;

virtual void add_sources_on_patch(
    std::vector<FluidVector> &dU,
    const FluidState&, const Grid&, double dt,
    void *execution_stream = nullptr) const = 0;
```

在 `make_gravity(config, GravityId)` 注册构造，重力 ID 来自已解析的执行计划。
参数名与别名在构造前统一解析，不在工厂内重新解释。外部重力是在每个流体 RK stage
内计算的常逻辑向量。自重力需要独立场求解器。

### 网络、ODE 和线性求解器 — Source extension

网络类型定义编译期尺寸和核素元数据，例如 `NUM_SPECIES`、`ODE_NEQ`、`SPECIES_NAMES`、`AION`、`ZION`，以及燃烧求解器和 `ProblemHelper` 使用的静态 `eval_rhs`、`eval_jacobian`、温度导数、注册和初始分数函数。

ODE wrapper 模板形式为：

```cpp
template <typename NetType, typename MatrixType, typename LinearSolver>
struct Solver_NEW {
    template <typename EOSType>
    static bool integrate(
        double *X_ODE, double rho, double dt_target,
        const EOSType &eos, const BurnConfig &config, double &dt_recommended);
};
```

四个 Timmes 派生内置网络仍直接注册。用户以
`examples/network/CustomNetworkRecipe.py` 为模板维护自定义 pynucastro 网络配方，
再交给 `tools/network/GenerateNetwork.py` 生成。每个合法的小写 `NETWORK_ID`
在 `src/physics/network/custom/<id>/` 下形成独立网络包。`aprox` 或 `iso` 开头的
ID 保留给内置网络。替换已有 ID 需要 `--replace`，旧网络包先保存在 `.backup/`。
CMake 通过生成注册表发现多个共存网络包，一次运行用 `network_name = custom:<id>`
选择其中一个。

燃烧工厂使用共用 `ResolvedExecutionPlan` 中的网络、ODE 和线性求解器 ID。
新增方法应扩展策略注册与能力检查，不另建解释同一组配置字符串的工厂。
具体网络和 ODE 数学实现独立于这一步选择过程。

本文的生成流程和验证配方使用 pynucastro 2.12.0。配套 Python 环境与
构建命令见[网络准备步骤](../validation/network/README.zh-CN.md#复现这些记录)。

适配层将 pynucastro 的摩尔丰度 RHS/Jacobian 转为 ARCH 质量分数形式，把核能与
弱中微子能量写入 ODE RHS，并隔离 SimpleCxx 头文件的命名空间。已识别的弱表
包含 rho*Ye 的组分链式导数及有符号能量源梯度。自定义网络设置
`SUPPORTS_NSE=false`；完整 RHS 的温度 Jacobian 列使用共用四阶差分策略、
精度导出的步长及边界模板。生成器只删除编译期字面零 Jacobian 调用；
运行值为零的结构项仍保留，以保证 KLU 安全地复用结构并重新分解。
默认 `NUCLEI` 路径会把没有连通反应的指定核素保留为不参与反应的组分，并拒绝
重复项。CMake 会校验自动生成的契约元数据（包括 `generator_version`），并根据
[网络包契约](../src/physics/network/custom/README.md)检查清单声明的稀疏结构与核素顺序。
这些字段由生成器写入，不是需要手动选择的运行时设置。
需要积分有符号弱反应损失的网络增加一个源项状态，使用同一套 BE_NR/BD/ROS4
阶段、误差控制与回滚；接受步能量包含核能与该积分。受控 Urca 轨迹已在固定热容
及 Helmholtz 闭合下通过独立参考检查；代表性的生成网络 Helmholtz 应用也已在
两端通过。测试范围、原误差预算与复现命令见[网络验证](../validation/network/README.zh-CN.md)，
AMR 与重启则有各自的[应用验收记录](../validation/amr/README.zh-CN.md)。

具备设备接口的生成包使用一个可移植数学头文件，供普通 C++ 适配层与 CUDA 实例化共用，
保留生成的反应表达式及核数据约定。能量权重来自生成的核质量与转换因子，通过守恒
重子质量偏移改善点积条件。已识别的小型不可变元数据可在设备端调用，不解引用
主机全局数组。两个后端通过显式视图访问内嵌弱表：CPU 借用主机数据，CUDA
存储管理器上传设备数据，并在网格存储变化时保留它们。插值、反应和 ODE 数学
均只维护一套。各网络包使用独立的头文件保护和局部作用域的屏蔽宏，允许多个网络包共存。
符号 Jacobian 结构来自声明的写入（先删除字面零），而不是采样数值非零；
无法识别的写入下标会被拒绝。只有网络包通过设备数学契约检查，且清单声明
`device_callable_math=true` 时，CMake 才启用 CUDA 执行。通过包检查但不具备设备数学
契约的网络包仅支持 CPU；无法识别的弱表布局会在写入最终网络包
之前被拒绝。可移植生成不改变前述科学验收要求或自定义网络的 NSE 限制，
数学／求解器短测也不能替代完整轨迹验收。

矩阵和线性求解器是独立模板参数。`DenseWrap` 是不超过 31 个总 ODE 方程
（`BurnLimits::MAX_ODE_NEQ`）的固定尺寸专用后端；`SparseWrap` 保留 CSC 符号
模式，并调用 KLU analyze/factor/refactor/solve。解析阶段保留
`linear_solver = Auto`，各后端在检查支持能力时确定具体求解器：不超过 31 个
总方程时均选择 DenseLU；超过 31 时 CPU 选择 SparseKLU，CUDA 选择 cuDSS。
计数必须包含核素、温度和辅助源项状态：无源积分时最多 30 核素，含一个源积分
时最多 29 核素，不能仅按核素数判断。显式 SparseKLU 仅适用于 CPU，显式 cuDSS
仅适用于 CUDA。显式 CPU+cuDSS 或 CUDA+SparseKLU 会在能力检查阶段、后端构造
前报错；因此当 `compute_backend = auto` 时，显式求解器只能选择与其兼容的后端。
显式 DenseLU 会拒绝超过方程数限制的系统。

SparseKLU 要求构建时启用 KLU。CUDA cuDSS 要求实际链接可选的 0.8 版求解库，
且网络已注册设备端执行代码。CUDA 执行器复用共用的 BE_NR/ROS4/BD ODE 续算
接口，只有内存、批量执行和稀疏库操作按后端区分。CUDA 使用声明的 CSR 结构
和有界的逐单元临时工作空间，在内存预算内保留分解结果，不使用稠密的 `N*N`
矩阵位置查询表。CPU `SparseWrap` 使用 CSC 数值及 `N*N` 整数查询表。
内存需求取决于稀疏分解填充和实际工作负载。对于超大网络，模型的科学可靠性
取决于核素集合、反应数据和适用范围；当前覆盖见
[网络验证](../validation/network/README.zh-CN.md)。

生成或替换网络包后必须重新执行 CMake。写入前先用 `--check`；可用
`-DARCH_CUSTOM_NETWORKS="id1;id2"` 限定本次构建的网络。通用源码扫描排除整个
custom 子树，因此只编译所选网络的适配代码。pynucastro 只在生成时需要，ARCH
运行时不依赖 Python。不同规模生成网络的检查记录统一见
[validation/network](../validation/network/README.zh-CN.md)。

### 扩散 — Source extension/Experimental

`dispatch_diffusion(config, DiffusionIntegratorId, next_step)` 使用共用策略选择
流程解析出的积分器 ID，据此构造积分策略，不再次解析参数字符串。

单 block 积分器提供：

```cpp
static void integrate(
    FluidState&, const auto &eos, const Grid&, const SimConfig&,
    double dt, double dt_forward_euler, const auto &boundary_handler);
```

组合 AMR 入口为：

```cpp
Numerics::Diffusion::advance_amr_rkl1(...);
Numerics::Diffusion::advance_amr_rkl2(...);
```

RKL stage 数学通过 `DiffFunction::RKLOrder`、`compute_stages`、`usable_max_stages`、`stable_step` 和 `get_rkl_coeffs` 暴露。`DiffFlux` 拥有热、黏性和核素通量。多 block stage 要求边界应用、ghost exchange、粗细通量登记、reflux 和 reflux 后同步。

### AMR — Internal/source extension

`AMRControl` 拥有 `MemoryPool`、`AmrTree`、`GhostExchange` 和 `FluxRegister`，不支持算例直接访问。`amr::BLOCK_NX/NY/NZ` 为 16，`amr::MAX_NG` 为 4，x 存储 padding 到 32。修改这些常量会影响分配、IO 形状、重构范围和当前 CUDA device 布局。

## HDF5 与重启格式

### Plot 文件

文件属性：

```text
time        double
dim         int
geometry    string
```

数据集：

```text
Grid/x, Grid/y, Grid/z       物理 Cartesian 单元中心坐标
Grid/level                   每个 block 的 AMR 叶节点层级
Grid/morton                  每个 block 的 Morton code
Data/<requested field>       [block, z?, y?, x] 内部单元数组
```

核素数据集使用注册名称。在绘图工具中选择组分场时应使用该名称，不要假定字段带有 `X_` 前缀。

`HDF5Writer` 记录 PLT 写入失败，但继续运行模拟。

### ARCH 检查点

ARCH 检查点保存继续模拟所需的完整状态，两个后端共用读取器和写入器。
读取器检查文件内部的格式标识及下述必需字段；不完整或不支持的输入会在恢复模拟
之前被拒绝。

属性包括 `checkpoint_version`、`time`、`step`、`chk_index`、`plt_index`、`dim`、`geometry`、`num_species`、`cells_per_block`、`dt_old`、`dt_burn`、`resume_after_regrid`、`eos_type`、`ideal_gamma`、`burn_enabled`、`active_network`、`nse_enabled`、`eos_table_path` 和 `eos_table_sha256`。checkpoint 中的 `eos_type` 记录已解析的规范策略（`ideal`、`helmholtz`、`tabular3d` 或 `tabular4d`），因此自动识别出的表 rank 属于 restart 身份，而不是沿用配置中的原始 `tabular` 拼写。燃烧关闭时 `active_network` 必须为 `none`。时间步字段分别恢复增长控制、下一宏步携带的燃烧限制及循环阶段，避免重复执行已完成的 regrid 或按步输出。表路径仅用于审计；兼容性按 SHA-256 内容身份判断，因此同一份表可以在不同安装位置之间移动。表加载器会在加载前后计算摘要，并将缓存 owner 绑定到该摘要；传给每次 checkpoint 的不可变身份描述的是 EOS owner 实际驻留的字节，而不是稍后重新读取路径的结果。

数据集：

```text
Blocks/level
Blocks/logical_x1, logical_x2, logical_x3
Data/rho, Data/mom_u, Data/mom_v, Data/mom_w, Data/eng, Data/enuc_rate
Data/rhoX, Data/X    [species, block, interior cell]
Species/name, Species/A, Species/Z, Species/gamma, Species/Cv
```

`Data/X` 保存两端实际演化的原始质量分数。读取器检查它与保存的 `rhoX` 是否一致，并直接恢复质量分数，避免先乘密度再除密度造成的舍入损失。写入器同时保存两种表示。

重启兼容性检查维度、几何、每 block 单元数、EOS 策略、适用时的理想气体 gamma、反应网络身份、EOS 表内容、燃烧与 NSE 开关，以及每个按顺序排列的核素名称和热力学属性。`ENUC` 在驱动动态细化时属于重启相关状态，因此会被持久化。结构错误或物理配置身份不匹配会在发布层次结构前抛出异常。step-zero 与已经到达终点的 restart 不会重复写初始/最终文件。CPU/CUDA 读写完全相同的 Host schema；后端名称刻意不参与兼容性判断。

物理配置身份、`Data/enuc_rate`、时间步控制元数据以及活动核素的原始 `Data/X`
均为必需内容。字段缺失、形状或数值无效、`Data/X` 与 `Data/rhoX` 不一致都会报错；
读取器不会补造未经核实的身份、将缺失燃烧能量置零，或重建缺失的质量分数。
全新模拟自行初始化 `RunState`，不使用这些重启规则。

## 已知限制

- 验证结果对应[验证索引](../validation/README.zh-CN.md)注明的受测工作负载与配置；整体验收状态也由该索引统一记录。
- CUDA 生成网络必须满足[设备数学包契约](../src/physics/network/custom/README.md)，包括声明 `device_callable_math=true`；通过检查的仅主机网络包在 CPU 上执行。两后端均未实现自重力、Jeans 指标或自定义网络 NSE。
- 运行时选择基于字符串，多个策略表面是编译期或 duck-typed 契约，而不是稳定公共 ABI。
- 状态修复、界面 clamp 和 fallback 默认值可能破坏严格守恒或隐藏错误的数值选择；生产运行必须检查解析后的配置与诊断。
- 单位元数据以及完整的构建/运行来源（参数文件、编译器、求解器设置、边界与 commit）位于 HDF5 外部。检查点内嵌重启关键的 EOS/表/网络/核素身份，但 Release flags 无法保证跨机器逐位复现。
- 算例构建假设 `simulation/<Case>/` 布局；plot 写入失败会报告但不会终止模拟。
- Sedov 在单元中心沉积归一化的连续有限半径 profile，因此离散注入能量随分辨率变化。

## 源码索引

按模块浏览请从[源码导览](../src/README.md)开始；CUDA 内部的功能分组见
[runtime 索引](../src/cuda/runtime/README.md)。下表列出共用接口，不重复各目录的文件清单。

| 区域 | 主要文件 |
| --- | --- |
| 程序入口 | `src/main.cpp` |
| 参数加载 | `src/io/ConfigParser.h`、`src/core/RuntimeParams.h` |
| 算例注册/公共门面 | `src/core/UserInterface.h`、`ProblemRegistry.h`、`ProblemHelper.h/.cpp` |
| 算例适配 | `src/interface/GenericProblem.h`、`ProblemGenerator.h` |
| 算例侧数据 | `src/data/UserTypes.h`、`GlobalDefs.h`、`physics/species/Species.h` |
| 守恒存储 | `src/data/FluidState.h` |
| 坐标/度量 | `src/grid/Grid.h`、`GridMetrics.h` |
| AMR | `src/amr/` |
| 运行时组合 | `src/driver/SolverDispatch.cpp`、`driver/dispatch/`、`Driver.h` |
| 流体时间推进 | `src/numerics/integrator/` |
| 通量/重构 | `src/numerics/flux/`、`src/numerics/reconstruction/` |
| 扩散 | `src/numerics/diffusion/` |
| 燃烧/ODE/线性代数 | `src/numerics/burnsolver/`、`src/numerics/linalg/` |
| EOS/重力/网络/NSE | `src/physics/` |
| 第三方来源和保留条款 | `THIRD_PARTY_NOTICES.md`、`LICENSES/` |
| plot/checkpoint | `src/io/plot/`、`src/io/chk/`、`src/io/hdf5/` |
| 基准算例 | `simulation/Sod/`、`simulation/Sedov/` |
| verification/validation 记录 | `validation/` |
