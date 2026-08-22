# ARCH 研究与 API 参考

英文原文：[Reference.md](Reference.md)。英文版是唯一规范文本；接口或行为变化必须先更新英文版。若中英文内容不一致，以英文版为准。

本文依据当前 `main` 分支的声明和 dispatch 路径整理，可全文搜索。学生工作流见 [`docs/guides/SimulationCase.zh-CN.md`](guides/SimulationCase.zh-CN.md)。

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
- **源码扩展 API**：在仓库内部添加数值或物理策略时使用的模板或虚函数契约，遵循仓库内源码兼容性。

本文中的稳定性标签含义如下：

| 标签 | 含义 |
| --- | --- |
| Stable | 面向算例作者维护；不兼容变化必须提供迁移说明。 |
| Source extension | 仓库内源码兼容。 |
| Internal | 不属于算例表面的驱动/AMR 实现细节。 |
| Experimental | 已实现，但验证或接口稳定化尚未完成。 |
| Reserved | 已解析或命名，留待未来实现。 |

ARCH 构建一个可执行文件和一个内部 object target，其扩展契约工作在源码层。

科学来源与接口稳定性分开记录。反应网络、NSE 公式和 Helmholtz EOS 可追溯到 Frank Timmes；恒星热传导数学的直接软件来源为 AMReX-Astro Microphysics。逐文件边界和保留条款见 [`THIRD_PARTY_NOTICES.zh-CN.md`](../THIRD_PARTY_NOTICES.zh-CN.md)。除非文件头或该说明明确指出，其他模块不声明外部来源。

## 功能矩阵

### 执行与网格

| 功能 | 接受值或接口 | `main` 状态 | 说明 |
| --- | --- | --- | --- |
| Host 执行 | `compute_backend = cpu` | 支持 | OpenMP 在构建时配置。 |
| CUDA 执行 | `compute_backend = cuda/auto` | 预留 | 参数会解析；`main` 执行 CPU 后端。 |
| 维度 | 正的 `nblockx1`；尾部 block 数可为零 | 支持 | `nblockx2=0,nblockx3=0` 为 1D；`nblockx3=0` 为 2D。 |
| 几何 | `cartesian`、`cylindrical`、`spherical` | 支持 | 网格逻辑中的字符串实际区分大小写。 |
| AMR | `lrefinemax >= 0` | 支持 | 每个活动维固定 16 个单元的 block 尺寸。 |
| 自重力 | `gravity_type = self` | 不可用 | gravity factory 会终止。 |
| Jeans 场 | `JENS` | 预留 | 解析器警告并关闭。 |

### 数值策略

| 类别 | 运行时取值 | 状态和精确行为 |
| --- | --- | --- |
| 通量 | `SW`、`VL`、`Roe`、`HLL`、`HLLC` | 已 dispatch |
| 重构 | `pcm`、`donor_cell`；`muscl`、`plm`；`ppm` | 所列 alias 已 dispatch；`weno5` 只出现在 ghost 数计算中 |
| MUSCL limiter | `minmod`、`superbee`、`vanleer`、`mc` | 已 dispatch；包括 `none` 在内的未知值回退到 MinMod |
| 流体时间推进 | `Euler`、`RK1`；`RK2`、`SSPRK2`；`RK3`、`SSPRK3` | Euler、SSPRK2、SSPRK3 |
| 扩散时间推进 | `RKL2`（默认）、`RKL1` | 独立扩散算子中 RKL2 为二阶；RKL1 是可选一阶方法 |
| EOS | `ideal`、`tabular`、`helmholtz` | CPU 已 dispatch |
| 重力 | `none`、`external` | 支持；未知字符串选择无重力 |
| 网络 | `aprox13`、`aprox19`、`aprox21`、`iso7`；`custom:<id>` | 内置网络及 CMake 自动发现的生成网络 |
| 燃烧 ODE | `BE_NR`、`ROS4`、`BD` | 均已 dispatch，并由单区 CPU 回归覆盖 |
| 线性求解 | `Auto`、`DenseLU`、`SparseKLU` | `Auto` 对不超过 30 核素使用 DenseLU，超过时使用 KLU；显式 DenseLU 拒绝大型网络 |

建议使用上述规范拼写；不同 dispatcher 的规范化行为并不一致。

## 运行时架构

可执行程序遵循以下生命周期：

```text
main(argc, argv)
  -> RuntimeParams::Load(.par)
  -> ProblemRegistry::Create(problem name)
  -> case.Setup(config, species)
  -> DispatchSolver
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
- 比内能限制到 `[1e-10, max_eint]`；
- 负质量分数被截为零，所有分数重新归一化；
- 当组分和接近零时，安装均匀组分。

这些工程保护在守恒通量更新以外修改状态。运行记录应将修复贡献与守恒量和 L1/L2 指标一起保存。

生产 PPM 路径重构密度、速度、压力和组分，再调用选定 EOS 重建总能量。它对密度和压力取 floor，将组分限制在 `[0,1]` 并归一化界面组分。

### 构建复现性与编译期妥协

Release 编译使用 `-O3 -march=native -ffast-math -DNDEBUG`。验证记录应包含编译器、flags、OpenMP 线程数、硬件和数值容差。

dispatch 翻译单元以 `-O1` 编译，并关闭 LTO、使用 `-fno-inline-functions-called-once`，因为积分器 × 通量 × 重构 × EOS 模板矩阵原本会让单个翻译单元消耗数 GB 内存。燃烧策略在进入完整通量矩阵前通过 `BurnerHandle` 类型擦除，减少 network/ODE 在流体实例中的乘法组合。性能和正确性都需要实测。

## 构建、注册和命令行

### 构建契约

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

CMake 在配置时获取 HighFive，并链接 HDF5 C++/HL 库。KLU 默认启用：CMake 先查找已安装的 KLU package；若不存在，则获取固定的 SuiteSparse v7.13.0，只构建 KLU、BTF、AMD、COLAMD 与 SuiteSparse_config。EOS `.dat`/`.h5` 资源可能需要 Git LFS。

相关 cache 选项为 `ARCH_ENABLE_KLU`（默认 `ON`）、`ARCH_FETCH_SUITESPARSE`（默认 `ON`）、`ARCH_CUSTOM_NETWORK_ROOT`（生成 package 根目录）和 `ARCH_CUSTOM_NETWORKS`（可选的分号分隔 custom ID 列表）。`BUILD_TESTING=ON` 注册理想气体 tabular EOS 与 161 方程 KLU 回归。

源码通过 CMake `GLOB_RECURSE` 从 `src/core`、`src/physics`、`src/numerics`、`src/io` 和 `simulation` 中发现。新增 `.cpp` 后重新执行 `cmake -S . -B build ...`。

### 算例注册

维护中的注册宏为：

```cpp
REGISTER_PROBLEM_CLASS("RuntimeName", CaseClass);
```

它在静态初始化期间注册 factory。重复名称会覆盖已有项，算例类必须可默认构造。另有自由函数兼容宏：

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
- 未知键保留在 `SimConfig::custom_params` 或 `custom_string_params`，不提供拼写验证；
- 自定义键缺失时，`SimConfig::Get<T>` 返回调用者提供的默认值；
- 轻量 `pi` 表达式解析器用于域边界和外部重力分量，支持 `pi`、`-pi`、`2*pi`、`pi*2` 和 `pi/2` 等形式；
- 路径相对于进程工作目录解释；
- EOS dispatch 会移除 `eos_table_path` 的引号，普通字符串则保留解析器文本。

当前 fallback 策略并不统一：

| 无效选择 | 当前行为 |
| --- | --- |
| flux | 警告，选择 HLLC |
| reconstruction | 警告，选择 PCM |
| MUSCL limiter | 警告，选择 MinMod |
| hydro integrator | 警告，选择 SSPRK2 |
| gravity | 未知值变为无重力 |
| EOS、network、ODE、linear solver | 抛出异常 |
| diffusion integrator | 根据路径 fatal 或异常 |

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
| `timeintegrator` | string | — | 规范键缺失时使用的 legacy fallback 键 |
| `cfl` | double | `0.8` | 显式流体 CFL；加载时不检查范围 |
| `EntropyFix` | bool | `true` | 启用 entropy-fix 平滑 |
| `EntropyFixCoefficient` | double | `0.1` | 启用 entropy fix 时使用 |
| `sml_rho` | double | `1e-12` | 密度修复阈值 |
| `max_eint` | double | `1e21` | 比内能上限 |
| `compute_backend` | string | `cpu` | 已解析；当前分支中 `cuda/auto` 为 V2 预留 |
| `cuda_device` | int | `0` | 预留 |

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
| `gravity_type` | string | `none` | `none`、`external`；`self` 会终止 |
| `gravity_g_x/y/z` | expression | `0` | 外部重力分量 |
| `gravity_G` | expression | `6.6743e-8` | 仅为尚不支持的自重力解析 |

对 `eos_type = tabular`，HDF5 文件声明 `table_rank = 3` 或 `4`，dispatch 自动选择相应策略。新表应优先保存比 Helmholtz 自由能；规范化数据集、导数关系、旧 direct 表路径以及实测的 guard-node/端点间隔规则见源码旁的 [Tabular EOS HDF5 接口](../src/physics/eos/TabularEOS.zh-CN.md)。Shen/LS/HS/CompOSE/EOSDriver 文件通过转换器进入该接口，二进制兼容性由规范化 schema 定义。

维护中的 Helmholtz 验证资源是从 [Timmes EOS 页面](https://cococubed.com/code_pages/eos.shtml)下载的 `helmholtz.tar.xz` 中的 `helm_table.dat`。它通过 Git LFS 实体化在 `EOS_toolkit/tables/helmholtz/helm_table.dat`，大小为 60,242,514 bytes，SHA-256 为 `c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`。原始表成员是验证权威。loader 使用固定 541×201 Timmes 布局并要求全部四个数据块；燃烧基线还要求上述精确 checksum。

### 燃烧、网络与 ODE

| 键 | 类型 | 加载默认值 | 契约 |
| --- | --- | --- | --- |
| `use_burn` | bool | `false` | 启用燃烧模块 |
| `network_name` | string | `aprox19` | 上述内置网络或任意已编译的 `custom:<id>` |
| `nuclearTempMin` | double | `1e9` | K；燃烧激活阈值 |
| `nuclearDensMin` | double | `1e-10` | g/cm3；燃烧激活阈值 |
| `smallt` | double | `1e5` | K；燃烧状态 floor |
| `smallx` | double | `1e-20` | 组分 floor |
| `enucDtFactor` | double | `1e30` | 能量释放时间步 limiter；巨大默认值实际关闭限制 |
| `use_nse` | bool | `true` | 启用带阈值 NSE 投影 |
| `nseTempThreshold` | double | `4.5e9` | K |
| `nseDensThreshold` | double | `1e6` | g/cm3 |
| `enforce_mass_conservation` | bool | `true` | 燃烧后归一化组分 |
| `burn_verbose_level` | int | `0` | 燃烧诊断详细级别 |
| `ode_solver` | string | `BE_NR` | `BE_NR`、`ROS4` 或 `BD` |
| `linear_solver` | string | `Auto` | `Auto`、`DenseLU`、`SparseKLU`；DenseLU 限于不超过 30 个核素 |
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
| `restart_file` | string | 空 | 实际重启路径必需 |

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

`Setup` 在分配前运行一次。`Init` 通过 OpenMP population loop 对已分配 block 单元运行，包括 ghost 存储，并可能在初始 AMR 构建期间再次运行。它必须确定、线程安全且与调用次数无关。

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

`evaluate_state` 是规范的热力学状态契约。对每个有效 `(rho,T,X)` 输入，它必须填充有限的 `P`、`E`、`cv`、`sound_speed`、`dp_drho` 和 `dp_dT`；其中压力、`cv` 和声速必须为正。自由能 tabular 策略从同一个插值 Helmholtz 势导出这些量；旧 direct 策略使用已提供的导数数据集，或采用受表边界约束的局部差分，而不是返回零。详见[规范化 HDF5 契约](../src/physics/eos/TabularEOS.zh-CN.md)。

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

在 `make_gravity` 注册构造。外部重力是在每个流体 RK stage 内计算的常逻辑向量。自重力需要独立场求解器。

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

四个 Timmes 派生内置网络仍直接注册。custom pynucastro 网络由用户维护 recipe `examples/network/CustomNetworkRecipe.py`，并交给安全边界固定的 `tools/network/GenerateNetwork.py` 生成。每个合法的小写 `NETWORK_ID` 在 `src/physics/network/custom/<id>/` 下形成隔离 package。`aprox` 或 `iso` 开头的 ID 保留。已有 ID 的替换需要 `--replace`，旧版本先保存在 `.backup/`。CMake 可发现任意多个共存 package，C++ dispatch 由生成注册表统一处理。一次运行用 `network_name = custom:<id>` 选择其中一个。

adapter 将 pynucastro 的 molar RHS/Jacobian 转为 ARCH 质量分数形式，把核能与弱中微子能量写入 ODE RHS，并隔离 SimpleCxx header namespace。energy Jacobian 当前不含弱中微子能量对组分的导数。custom 网络设置 `SUPPORTS_NSE=false`，温度 Jacobian 列采用相对步长 `1e-4` 的中心差分；生产验收覆盖这两项边界。

矩阵和线性求解器是独立模板参数。`DenseWrap` 是不超过 30 核素（`BurnLimits::MAX_SPECIES`）的固定尺寸专用后端；`SparseWrap` 保留 CSC 符号模式，并调用 KLU analyze/factor/refactor/solve。`linear_solver = Auto` 在不超过 30 核素时选择 DenseLU，超过时选择 SparseKLU。显式 DenseLU 会拒绝大型网络；构建时启用 KLU 后，任意已编译网络都可显式选择 `SparseKLU`。

生成或替换 package 后必须重新执行 CMake。写入前先用 `--check`；可用 `-DARCH_CUSTOM_NETWORKS="id1;id2"` 限制昂贵构建。pynucastro 只在生成时需要，ARCH 运行时不依赖 Python。

### 扩散 — Source extension/Experimental

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

`AMRControl` 拥有 `MemoryPool`、`AmrTree`、`GhostExchange` 和 `FluxRegister`，不支持算例直接访问。`amr::BLOCK_NX/NY/NZ` 为 16，`amr::MAX_NG` 为 4，x 存储 padding 到 32。修改这些常量会影响分配、IO 形状、重构范围和计划中的 CUDA 布局。

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

已注册核素使用其注册名称。历史可视化 renderer 搜索 `X_` 前缀，因此存在组分自动选择不匹配。

`HDF5Writer` 记录 PLT 写入失败，但继续运行模拟。

### Checkpoint 文件版本 1

属性包括 `checkpoint_version`、`time`、`step`、`chk_index`、`plt_index`、`dim`、`geometry`、`num_species` 和 `cells_per_block`。

数据集：

```text
Blocks/level
Blocks/logical_x1, logical_x2, logical_x3
Data/rho, Data/mom_u, Data/mom_v, Data/mom_w, Data/eng
Data/rhoX    [species, block, interior cell]
```

重启兼容性检查维度、几何、核素数和每 block 单元数。科学来源信息——参数文件、编译器、EOS、核素顺序、求解器、边界和 commit——保留在 HDF5 外部。Checkpoint 结构错误会抛出异常。

## 已知限制

- `main` 分支执行 CPU 后端。CUDA 一致性和 AMR 定量收敛待完成；自重力和 Jeans 指标尚未实现。
- 运行时选择基于字符串，多个策略表面是编译期或 duck-typed 契约，而不是稳定公共 ABI。
- 状态修复、界面 clamp 和 fallback 默认值可能破坏严格守恒或隐藏错误的数值选择；生产运行必须检查解析后的配置与诊断。
- 单位元数据和完整 checkpoint 来源信息仍位于 HDF5 外部；Release flags 也无法保证跨机器逐位复现。
- 算例构建假设 `simulation/<Case>/` 布局；plot 写入失败会报告但不会终止模拟。
- 历史 AMR 图像库只提供定性结果，输入/renderer 的核素命名不匹配要等重新生成 archive 时处理。
- Sedov 在单元中心沉积归一化的连续有限半径 profile，因此离散注入能量随分辨率变化。

## 源码索引

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
