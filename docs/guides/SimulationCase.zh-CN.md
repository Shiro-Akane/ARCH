# ARCH 模拟算例指南

英文原文：[SimulationCase.md](SimulationCase.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本文提供完成第一个 ARCH 算例的连续学习路径，建议按顺序阅读。完整参数目录和框架扩展接口集中在可搜索的[研究与 API 参考](../Reference.zh-CN.md)中。

## 1. 运行小型激波管

如尚未构建 ARCH，请在仓库根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

然后运行小型一维 Sod 输入：

```bash
export OMP_NUM_THREADS=4
./bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

命令行契约始终为：

```text
./bin/ARCH <registered-problem-name> <parameter-file>
```

`Sod.cpp` 中的 `REGISTER_PROBLEM_CLASS` 将运行时名称注册为 `Sod`，目录负责组织源码和输入。成功运行后，日志、plot 文件和 checkpoint 会写入 `output/first_sod/`。

教学输入使用 64 个单元、HLLC 通量、MUSCL-MC 重构、SSPRK2、理想气体，并关闭 AMR、燃烧、重力和扩散。它用于教学和 smoke test。

## 2. 将问题理解为 CFD 计算

ARCH 推进守恒状态的单元平均值

\[
U=(\rho,\rho u,\rho v,\rho w,E,\rho X_1,\ldots,\rho X_N).
\]

算例作者在初始时刻提供更便于表达的原始变量

\[
W=(\rho,u,v,w,p,X_1,\ldots,X_N),
\]

再由选定的 EOS 计算动量和总能量。

`Sod_beginner.par` 中的主要选项分别承担不同任务：

| 参数 | 作用 |
| --- | --- |
| `solver = HLLC` | 面通量/Riemann 近似 |
| `reconstruct = muscl` | 左右面状态重构 |
| `limiter = mc` | 激波附近的 MUSCL 斜率限制 |
| `time_integrator = RK2` | 流体 method-of-lines 时间积分 |
| `cfl = 0.4` | 显式波稳定时间步的比例 |
| `eos_type = ideal` | 压力、能量、声速和温度闭合 |

耦合时间精度由算子组合及其子步决定。燃烧/扩散/流体采用对称组合：

```text
B(dt/2) -> D(dt/2) -> H(dt) -> D(dt/2) -> B(dt/2)
```

因此即使选择 SSPRK3 和 PPM，耦合算子分裂最高仍为二阶。激波、限制器、AMR 界面和正性修复还可能进一步降低局部观测阶数。完整精度契约见 Reference。

### 单位

理想气体 Euler 方程可使用任意内部一致的单位制。Helmholtz EOS、核反应网络、NSE 阈值和恒星输运使用 CGS 量，例如 `g`、`cm`、`s`、`K` 和 `erg`。

## 3. 理解最小参数文件

### 网格与维度

```ini
geometry = cartesian
nblockx1 = 4
nblockx2 = 0
nblockx3 = 0
```

每个 block 在每个活动维度含 16 个内部单元。设置 `nblockx2 = nblockx3 = 0` 会创建真正的一维存储；只设置 `nblockx3 = 0` 会创建二维存储。`nblockx2 = 0` 而 `nblockx3` 为正是无效组合。

六个边界键接受 `outflow`、`reflect` 或 `periodic`。非活动轴的取值不生效。

### 数值方法

```ini
solver = HLLC
reconstruct = muscl
limiter = mc
time_integrator = RK2
cfl = 0.4
```

维护中的运行时求解器为 `SW`、`VL`、`Roe`、`HLL` 和 `HLLC`。重构接受 `pcm`、`muscl`/`plm` 和 `ppm`。MUSCL 限制器为 `minmod`、`superbee`、`vanleer` 和 `mc`。未知数值选项会警告并选择 fallback；启动时的 Strategy 行会报告实际配置。

### 时间与输出

```ini
tmax = 0.15
out_dir = output/first_sod
base_name = SodBeginner
plt_dt = 0.05
plt_variables = DENS, PRES, VELX, ENER
```

路径相对于进程工作目录解析。Plot 文件包含物理坐标、AMR level/Morton 元数据和请求的派生场。Checkpoint 包含守恒状态及用于重启的 AMR 叶节点拓扑。

### 物理与 AMR

```ini
eos_type = ideal
gamma = 1.4
gravity_type = none
use_burn = false
use_diffusion = false
lrefinemax = 0
```

建议先得到纯流体结果，再逐项添加物理模块。`lrefinemax = 0` 关闭 AMR，但参数加载仍会验证 `refine_var`。

## 4. 进行受控实验

修改前先复制输入：

```bash
mkdir -p runs/sod_experiment
cp simulation/Sod/Sod_beginner.par runs/sod_experiment/arch.par
```

推荐的首批实验：

1. 将 `nblockx1` 加倍并比较激波/接触间断宽度；
2. 保持 HLLC 和 CFL 不变，比较 `pcm`、`muscl` 和 `ppm`；
3. 在相同分辨率下比较 HLL 与 HLLC；
4. 启用 `lrefinemax = 1` 并检查 HDF5 输出中的 `Grid/level`；
5. 降低 `cfl`，判断某一特征是否属于时间步伪影。

每次只改变一个数值选项。精度评估使用 Sod 解析解、L1/L2 误差和守恒量。

## 5. 维护中的基准算例

`simulation` 目录将不同物理问题分开：

```text
simulation/
├── Sod/
│   ├── Sod.cpp
│   ├── Sod.par
│   └── Sod_beginner.par
└── Sedov/
    ├── Sedov.cpp
    └── Sedov.par
```

- `Sod` 仅表示一维 Cartesian 激波管。`Sod.par` 使用 128 个均匀单元和 `t = 0.2` 的标准左右状态。
- `Sedov` 是具有有限沉积半径的 Cartesian 爆炸波。二维下，`explosion_energy` 表示单位深度能量。连续注入压力会归一化到所需能量，但单元中心采样会产生随分辨率变化的沉积误差。

独立目录使每个基准只对应一个物理契约。

CPU 验证输入也遵循每个算例一个目录的布局：

| 目录 | 用途 |
| --- | --- |
| `SmoothAdvection/` | PCM/MUSCL/PPM 光滑流体收敛 |
| `DiffusionMode/` | RKL1/RKL2 解析余弦模组分扩散 |
| `ExternalGravity/` | 常加速度源更新 |
| `BurnOneZone/` | aprox13/Helmholtz ODE 求解器比较 |

其通过/失败结论及简洁 CSV 结果集中在[验证索引](../../validation/README.zh-CN.md)中，本指南不重复分析表。

## 6. 创建新算例

创建 `simulation/MyCase/MyCase.cpp`。`REGISTER_PROBLEM_CLASS` 将一个可默认构造的普通算例类包装起来，该类需提供：

```cpp
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

在 `simulation/` 下一层目录中使用以下两个 ARCH 头文件：

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

`Setup` 在网格分配前运行，应在其中读取和验证算例参数并注册核素。`Init` 会在 OpenMP 下对已分配单元调用，并可能在初始 AMR 构建期间再次调用；它必须确定、线程安全，且不能依赖调用顺序产生副作用。

最小完整示例：

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"

#include <cmath>
#include <stdexcept>

class GaussianDensity
{
    double rho0_ = 1.0;
    double pressure_ = 1.0;
    double amplitude_ = 0.1;
    double width_ = 0.1;
    int gas_id_ = -1;

public:
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        rho0_ = config.Get<double>("rho0", 1.0);
        pressure_ = config.Get<double>("pressure0", 1.0);
        amplitude_ = config.Get<double>("amplitude", 0.1);
        width_ = config.Get<double>("width", 0.1);
        if (rho0_ <= 0.0 || pressure_ <= 0.0 || width_ <= 0.0) {
            throw std::invalid_argument("GaussianDensity parameters must be positive.");
        }
        gas_id_ = specs.add_species(
            "Gas", 1.0, 1.0, config.physics.gamma, 1.0);
    }

    void Init(const PointCoords &point, PrimitiveData &out) const
    {
        const double radius2 = point.x * point.x + point.y * point.y;
        out.rho = rho0_ + amplitude_ * std::exp(-radius2 / (width_ * width_));
        out.p = pressure_;
        out.u = 0.0;
        out.v = 0.0;
        out.w = 0.0;
        out.SetMassFraction(gas_id_, 1.0);
    }
};

REGISTER_PROBLEM_CLASS("GaussianDensity", GaussianDensity);
```

新增 `.cpp` 后重新运行 CMake 配置，以刷新源码 glob：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
./bin/ARCH GaussianDensity path/to/arch.par
```

## 7. 算例侧类型和辅助函数

### `SimConfig`

用 `config.Get<double/int/string>(key, default)` 读取算例自定义参数。数值通过 `std::stod` 解析；`2*pi` 等表达式应写成已求值的十进制数。未知键会作为自定义参数保留，但不会进行拼写验证。

需要核心设置时，应读取对应的强类型成员，例如：

```cpp
config.grid.dim
config.grid.x1_min
config.physics.gamma
config.physics.gravity.g_y
```

### `PointCoords`

每个点同时提供 Cartesian、spherical 和 cylindrical 表示：

```text
point.x, point.y, point.z
point.r, point.theta, point.phi
point.r_cy, point.phi_cy, point.z_cy
```

逻辑轴由几何和维度决定：

| 几何 | 1D | 2D | 3D |
| --- | --- | --- | --- |
| Cartesian | x | x, y | x, y, z |
| Spherical | r | r, phi（极平面） | r, theta, phi |
| Cylindrical | r | r, phi（极平面） | r, z, phi |

转换坐标使用原点 `(0,0,0)`。算例特有的中心偏移应在 `Init` 内应用。

### `PrimitiveData`

应设置正的 `rho` 和 `p`、速度分量以及完整组分。框架通过选定 EOS 计算守恒动量和能量。

若状态由密度和温度定义，在 `Init` 中调用 `state.SetTemperature(temperature)`。此时温度是能量初始化的权威输入，`p` 仍可设置为对应的诊断压力。不调用时，ARCH 从 `rho` 和 `p` 推导能量。

名称不存在时，`GetSpeciesID` 返回 `-1`。调用 `SetMassFraction` 前必须确认 `id >= 0`。质量分数应非负且总和为一。

### `ProblemHelper`

使用内置网络的算例应在手动添加核素前调用：

```cpp
std::vector<double> default_X;
ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);
```

支持 `aprox13`、`aprox19`、`aprox21` 和 `iso7`。

对于由温度定义的初始参考状态，在 `Setup` 中计算一次压力：

```cpp
const double pressure = ProblemHelper::GetPressureFromRhoT(
    config, specs, density, temperature, default_X.data());
```

该函数执行运行时 EOS dispatch，应只在 `Setup` 中使用。

如果初始内能必须与给定温度严格一致，在 `Init` 中用同一数值调用 `SetTemperature`。

## 8. 运行前检查表

- 新增算例源码后重新运行 CMake 配置。
- 命令行问题名必须与注册字符串一致。
- 所有路径相对于进程工作目录解析。
- 使用维护中的 MUSCL limiter；`none` 会选择 MinMod fallback。
- 二维 spherical 几何中的 `x2` 表示平面 `phi`。
- Helmholtz 和反应网络模块使用 CGS 材料数据。
- 对 Helmholtz 验证运行，用 Git LFS 实体化 `EOS_toolkit/tables/helmholtz/helm_table.dat` 并核对 checksum。
- 组分索引前必须确认 `GetSpeciesID >= 0`。
- 初始化所有核素分数并保证总和为一。
- 将 EOS dispatch 辅助调用放在 `Setup` 中。
- 收敛研究中记录 floor、clamp 和 fallback 警告。
- `main` 上使用 `compute_backend = cpu`。

所有精确可接受取值和扩展契约见 [docs/Reference.zh-CN.md](../Reference.zh-CN.md)。
