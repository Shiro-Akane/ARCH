# ARCH 模拟算例指南

英文原文：[SimulationCase.md](SimulationCase.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本文提供完成第一个 ARCH 算例的连续学习路径，建议按顺序阅读。完整参数目录和框架扩展接口集中在可搜索的[研究与 API 参考](../Reference.zh-CN.md)中。

你不需要具备 CFD 背景也能快速上手。ARCH 会将流体离散为由网格单元组成的计算域，并在连续的时间步中不断更新它们的平均密度、动量和能量。一个“算例”定义了流体的初始状态、计算域边界以及底层的物理模型，而它配套的参数文件则负责指挥计算的具体执行方式。

我们的第一个示例是激波管：两种不同状态的气体最初在一个清晰的界面相遇，随着它们的相互作用，会逐渐演化出传播的波。运行这个示例可以向你展示整个计算工作流，而且不需要任何核反应网络或外部的 EOS 状态方程表。在随后的章节中，我们会将你的输入选项与底层的物理方程联系起来，并教你如何编写自定义的初始条件。

## 1. 运行小型激波管

首先，请完成 [README 的方案 A：使用 CPU](../../README.zh-CN.md#方案-a使用-cpu)。这一步将为你编译出不含测试套件的 `build-cpu/bin/ARCH` 可执行程序。如果你需要了解依赖项或可选的 CUDA 构建方式，请参阅[构建指南](Build.zh-CN.md)。下方示例都使用这份 CPU 程序，并从仓库根目录执行。
然后运行小型一维 Sod 输入：

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

命令行契约始终为：

```text
./build-cpu/bin/ARCH <registered-problem-name> <parameter-file>
```

`Sod.cpp` 中的 `REGISTER_PROBLEM_CLASS` 将运行时名称注册为 `Sod`，目录负责组织源码和输入。成功运行后，日志、plot 文件和 checkpoint 会写入 `output/first_sod/`。

这个教学输入使用了 64 个单元、HLLC 通量、MUSCL-MC 重构、SSPRK2 时间积分以及理想气体状态方程。它刻意关闭了 AMR、核燃烧、重力和扩散，因为它的主要目的是教学和基础的冒烟测试 (smoke testing)。

## 2. 将问题理解为 CFD 计算

程序采用有限体积方法：在每个单元中保存平均状态，根据穿过单元面的输运和局部
物理源项更新它。下面的符号中，密度表示单位体积的质量，速度描述流动，压力描述
流体的力学响应，质量分数描述组分。

ARCH 推进守恒状态的单元平均值

$$
U=(\rho,\rho u,\rho v,\rho w,E,\rho X_1,\ldots,\rho X_N).
$$

算例作者在初始时刻提供更便于表达的原始变量

$$
W=(\rho,u,v,w,p,X_1,\ldots,X_N),
$$

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

ARCH 的输入、初始化、计算、输出与 GUI 统一使用 CGS，包括 IdealGas。长度为 `cm`，时间为 `s`，密度为 `g/cm³`，压力为 `erg/cm³`，比内能为 `erg/g`，温度为 `K`，比热为 `erg/(g·K)`；角度用 `rad`，质量分数等无量纲。显式提供的数值不自动换算；IdealGas 无组分时的模型回退比热为 `7.18e6 erg/(g·K)`。

## 3. 理解最小参数文件

### 网格与维度

```ini
geometry = cartesian
nblockx1 = 4
nblockx2 = 0
nblockx3 = 0
```

每个 block 在每个活动维度含 16 个内部单元。设置 `nblockx2 = nblockx3 = 0` 会创建真正的一维存储；只设置 `nblockx3 = 0` 会创建二维存储。`nblockx2 = 0` 而 `nblockx3` 为正是无效组合。

六个边界键接受 `outflow`、`reflect`、`periodic`、`neumann` 或 `user`。非活动轴的取值不生效。自定义入流、热通量和引力势使用独立的同目录边界文件，见[用户边界指南](UserBoundaries.zh-CN.md)。

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

以下[算例目录](../../simulation/README.md)定义 CPU/CUDA 共用的验证问题。
对应的规范参数文件保存在所属模块的 `validation/<module>/inputs/` 中：

| 目录 | 用途 |
| --- | --- |
| `SmoothAdvection/` | PCM/MUSCL/PPM 光滑流体收敛 |
| `DiffusionMode/` | RKL1/RKL2 解析余弦模组分扩散 |
| `ExternalGravity/` | 常加速度源更新 |
| `BurnOneZone/` | aprox13/Helmholtz ODE 求解器比较 |

其通过/失败结论及简洁 CSV 结果集中在[验证索引](../../validation/README.zh-CN.md)中，本指南不重复分析表。
自引力可从支持周期/孤立输入的 [GravityBox](../../simulation/GravityBox/README.md) 开始；
[SNIaCoupled](../../simulation/SNIaCoupled/README.md) C/O 热点检查 Cartesian CPU/CUDA 与
受测完整方位角曲线坐标 CPU/CUDA 的四模块联动，不作为 SN Ia 解析精度验收。

## 6. 创建新算例

创建 `simulation/MyCase/MyCase.cpp`。`REGISTER_PROBLEM_CLASS` 将一个可默认构造的普通算例类包装起来，该类需提供：

```cpp
static arch::config::CaseConfiguration DescribeConfiguration(
    const arch::config::StandardInputResolution&);
void Setup(SimConfig &config, SpeciesManager &specs);
void Init(const PointCoords &point, PrimitiveData &out) const;
```

用户算例使用以下两个稳定的 ARCH 公开头文件：

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>
```

CMake 自动提供 `include/` 与 `src/` 搜索路径，因此算例不需要知道内部目录层级或本机路径；换机器后重新配置工程即可。两个公开头见 [include/](../../include/README.md)。

这两个文件构成完整的算例侧 ARCH 头文件表面。可以按需加入 C++ 标准库头文件，但算例不得直接包含具体 EOS 头文件、`eos_Utils.h` 或 `eosdispatch.h`。`UserInterface.h` 重新导出稳定的注册宏、算例类型和 `ProblemHelper` 操作；第二个显式头文件 `GlobalDefs.h` 提供有类型的运行时配置与共用的 `arch::constants` CGS 常数，同时避免算例依赖具体 EOS 策略。

`Setup` 在网格分配前运行，你应该在其中读取、验证算例参数并注册所需的核素。`Init` 函数会在 OpenMP 并行环境下被调用，用于精确地将初始数据填充到已分配的根网格单元中（且仅调用一次）。初始以及后续生成的任何细网格 block 都是通过守恒的 AMR 传递操作构造的，而不会再次调用 `Init`。正因如此，`Init` 必须是严格确定的、线程安全的，并且没有任何依赖于执行顺序的副作用。

以下示例为 IdealGas 材料；声明完整仅表示已列出本算例的输入需求，不表示文件、EOS 或设备已经检查：

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>

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
    static arch::config::CaseConfiguration DescribeConfiguration(
        const arch::config::StandardInputResolution&)
    {
        arch::config::CaseConfiguration result;
        result.complete = true;
        result.consumers.needs_network = false;
        result.consumers.needs_temperature_floor = false;
        result.consumers.needs_composition_floor = false;
        result.parameters = {
            {"rho0", "float", "g/cm^3"}, {"pressure0", "float", "erg/cm^3"},
            {"amplitude", "float", "g/cm^3"}, {"width", "float", "cm"}};
        return result;
    }

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
            "Gas", config.MaterialConstant(1.0, "Gas.A"),
            config.MaterialConstant(1.0, "Gas.Z"), config.MaterialInput("gamma"),
            config.MaterialConstant(1.0, "Gas.Cv"));
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

为示例准备参数文件时，复制已迁移的 Sod_beginner.par，移除 Sod 的 x_pos、rho_left/right、p_left/right、u_left/right 七个专属键，再显式加入 rho0=1、pressure0=1、amplitude=0.1、width=0.1。保留标准配置；这里的数值是教学输入，不是运行回填默认。Setup 只读取配置并登记材料，不得改写标准控制。

新增 `.cpp` 后重新运行 CMake 配置，以刷新源码 glob：

```bash
cmake -S . -B build-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_OPENMP=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-cpu/bin"
cmake --build build-cpu --target ARCH --parallel 1
./build-cpu/bin/ARCH GaussianDensity path/to/arch.par
```

## 7. 算例侧类型和辅助函数

### `SimConfig`

用 `config.Get<double/int/string>(key, default)` 读取算例自定义参数。 必须先由静态 `DescribeConfiguration` 声明类型、单位和实际需求；声明在 Setup 前解析，不能执行不完整 Setup 来探测缺项。Get 的第二个实参只保留读取观察信息，不会补齐缺失输入，也不构成批准的物理默认。自定义数值在 `.par` 中填写完整的十进制数或科学记数法，例如 `1e8`；不完整的数值在 Setup 前的声明输入解析阶段报错。标准网格边界和引力表达式参数可直接使用小写 `pi`、`2*pi` 和 `exp(1)`、`exp(-2)` 等形式。`exp(number)` 是自然指数函数；`1e8` 或 `1E8` 中的 `e/E` 是科学记数法的十进制指数标记，独立的 `e` 或 `E` 不是参数常数。

算例 `.cpp` 中的数学计算使用 C++ 标准库：按需加入 `<cmath>` 并调用 `std::exp`、`std::sin`、`std::cos`、`std::log`；自然常数可从 `<numbers>` 读取 `std::numbers::e`，圆周率也可用公开头文件提供的 `arch::constants::math::pi`。这些计算发生在 `Setup` 或 `Init` 中，不由 `config.Get` 求值。未知键会报错；只有算例声明或组分声明拥有的键才允许读取。退役键不能作为 custom 绕过。

当你需要访问核心设置时，应该直接读取它们严格按类型定义的成员。例如：

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
| Cylindrical | r | r, z（轴对称平面） | r, z, phi |

计算几何决定了网格的逻辑轴、度量和守恒量表示：二维 spherical 使用极平面 (r,phi)，而二维 cylindrical 使用轴对称平面 (r,z)（即 RZ）。在 RZ 中，r 和 z 均为长度，虽然 phi 不作为网格方向，但由于物理空间是三维的，三个速度分量依然存在。三维 cylindrical 则使用 (r,z,phi)。

以原点为球心的三维球对称密度分布为例，沿用上方算例中的 `rho0_`、`amplitude_` 和 `width_`。在 `Init` 中，可以先用笛卡尔坐标写：

```cpp
const double radius2 = point.x * point.x + point.y * point.y + point.z * point.z;
out.rho = rho0_ + amplitude_ * std::exp(-radius2 / (width_ * width_));
```

同一分布也可以用球坐标写：

```cpp
const double radius2 = point.r * point.r;
out.rho = rho0_ + amplitude_ * std::exp(-radius2 / (width_ * width_));
```

这两段是 `Init` 中密度赋值的可选写法；压力、速度和组分仍需按上方完整示例设置。对于同一个物理位置，`point.r` 与 `point.x/y/z` 描述的是同一个到原点的距离，两个表达式给出相同的球对称初态（浮点舍入范围内）。这些坐标字段在每次调用时同时存在，也可以在同一个算例中并用。

`geometry=cartesian` 指定的是计算网格的坐标、单元体积和面面积，不限制 `Init` 使用哪组坐标字段：笛卡尔网格可以读取 `point.r`，球坐标网格也可以读取 `point.x/y/z`。更换网格几何会改变离散方式；上面的等价性指同一物理位置处的初态定义。

使用曲线坐标描述物理场时，要先确定场的中心与坐标原点。`PointCoords` 的转换坐标以全局 `(0,0,0)` 为原点；`point.r` 不会随 `x1_min` 改变球心。若二维笛卡尔计算域只取正坐标区域，希望把圆形分布的中心放在左下边界（例如 `x1_min > 0`），可直接使用系统已有的网格边界。先在算例类中声明 `double center_x_ = 0.0, center_y_ = 0.0;`，再在 `Setup` 中读取边界并写入这些成员：

```cpp
center_x_ = config.grid.x1_min;
center_y_ = config.grid.x2_min;
```

`Setup` 完成后，同一个算例对象的 `Init` 会读取这两个成员。`Init(const PointCoords&, PrimitiveData&)` 没有 `config` 参数，不能直接写 `config.grid.x1_min`。圆心因此位于 `(x1_min,x2_min)`，无需硬编码坐标或新增自定义参数。在 `Init` 中计算到该圆心的局部半径：

```cpp
const double dx = point.x - center_x_;
const double dy = point.y - center_y_;
const double local_r2 = dx * dx + dy * dy;
```

三维时再声明 `center_z_` 成员，并在 `Setup` 中保存 `center_z_ = config.grid.x3_min;`，再加上 `(point.z - center_z_)` 的平方。这里平移的是初始物理分布；曲线网格的坐标原点、轴线和度量仍按所选 `geometry` 定义。在曲线网格中，`x1_min` 是原生径向轴的下界，应按该几何的物理坐标含义选择分布中心。

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

如果 cell-average 初值需要根层级的物理 cell 宽度，应保持 AMR 实现私有并调用：

```cpp
const double dx = ProblemHelper::GetRootCellWidth(config, 1);
```

逻辑轴从 1 开始编号。ARCH 当前每个方向每个 block 使用 16 个有效 cell，两侧各有 4 个 guard cell（x 方向存储为 `16 + 8`）。guard cell 不计入 `dx`；算例不得为了重建该数值而包含 `AmrDefines.h`。

若要在组分和比熵不变时构造邻近状态，可继续使用同一双头文件表面中的 EOS 无关辅助函数：

```cpp
const ProblemHelper::IsentropicState compressed =
    ProblemHelper::GetIsentropicStateAtPressureFactor(
        config, specs, density, temperature,
        default_X.data(), 1.001);

// compressed.rho
// compressed.temperature
// compressed.pressure
// compressed.sound_speed
```

最后一个参数是 `P_target/P_reference`，不是 `dP/P`。活动 EOS 通过统一的固定组分等熵实现提供 `c_v`、`(dP/dT)_rho`、压力和声速。该函数执行运行时 EOS dispatch 并积分局部热力学路径，因此只能在 `Setup` 中调用；若结果相对参考态移动超过 `0.25` 的 `ln(rho)`，函数会拒绝请求，而不会静默外推。Helmholtz 或 tabular 状态不得使用理想气体的 `T-P` 关系代替。

如果初始内能必须与给定温度严格一致，在 `Init` 中用同一数值调用 `SetTemperature`。

## 8. 运行前检查表

- 新增算例源码后重新运行 CMake 配置。
- 命令行问题名必须与注册字符串一致。
- 所有路径相对于进程工作目录解析。
- 使用维护中的 MUSCL limiter；`none` 会选择 MinMod fallback。
- 二维 spherical 几何中的 `x2` 表示平面 `phi`。
- 所有模型与材料输入使用 CGS，包括 IdealGas；显式比热不自动换算。
- 对 Helmholtz 验证运行，用 Git LFS 实体化 `EOS_toolkit/tables/helmholtz/helm_table.dat` 并核对 checksum。
- 组分索引前必须确认 `GetSpeciesID >= 0`。
- 初始化所有核素分数并保证总和为一。
- 将 EOS dispatch 辅助调用放在 `Setup` 中。
- 算例侧 ARCH 头文件只保留 `UserInterface.h` 和 `GlobalDefs.h`；EOS 操作通过 `ProblemHelper` 访问。
- 收敛研究中记录 floor、clamp 和 fallback 警告。
- 根据构建选择 `compute_backend = cpu`、`cuda` 或 `auto`；支持的组合见
  [后端选择说明](../CudaBackendStatus.zh-CN.md)。

所有精确可接受取值和扩展契约见 [docs/Reference.zh-CN.md](../Reference.zh-CN.md)。
