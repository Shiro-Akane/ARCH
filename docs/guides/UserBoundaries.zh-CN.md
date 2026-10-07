# 自定义物理边界与引力边界

一个算例可分别定义流体／扩散的物理边界和泊松势的边界。两类接口由
`<UserInterface.h>` 与 `<GlobalDefs.h>` 提供，使用不同的上下文、结果类型和注册宏。
初态编写见[算例指南](SimulationCase.zh-CN.md)，参数表见[Reference](../Reference.zh-CN.md)。

## 文件和构建

把所需文件放在注册算例源码旁边，文件名固定：

```text
simulation/MyCase/
├── case.cpp
├── physical_boundary.cpp   # 流体、热、黏性、组分
├── gravity_boundary.cpp    # 引力势
└── MyCase.par
```

已有算例源码可以继续使用原来的文件名。CMake 自动收集同目录源码；新增文件后重新
配置并构建。边界随程序编译，运行时没有文件路径参数或动态加载。回调注册名须与算例
注册名一致。选择 `user` 却缺少对应注册、文件名错误或注册重复时，启动明确报错。
只需要物理边界时可省略 `gravity_boundary.cpp`，反之亦然。

## 物理边界

六个 `x1l/x1r/x2l/x2r/x3l/x3r_boundary_type` 按原生网格轴命名。

| 取值 | 含义 |
| --- | --- |
| `outflow`、`neumann` | 外推内侧状态，齐次零法向梯度；不是通用无反射边界 |
| `reflect`、`reflecting` | 翻转法向动量，保留其他内侧量 |
| `periodic` | 同一方向两面成对连接 |
| `user` | 使用 `physical_boundary.cpp`；`inflow`、`dirichlet` 是同一流体指定状态接口的别名 |

最小函数式接口如下；完整示例见 [UserGravity](../../simulation/UserGravity/README.md)，
类式写法见 [UserBoundary](../../simulation/UserBoundary/README.md)。

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>

arch::boundary::PhysicalBoundaryData PhysicalWall(
    const arch::boundary::PhysicalBoundaryContext& ctx)
{
    using namespace arch::boundary;
    PhysicalBoundaryData result;
    if (ctx.purpose == BoundaryPurpose::Hydro) {
        result.hydro = ctx.interior; // 示例：完整内側状态；可换成指定入流
    } else if (ctx.purpose == BoundaryPurpose::Diffusion) {
        result.temperature = {ScalarBoundaryKind::OutwardFlux, 0.0};
    }
    return result;
}
REGISTER_PHYSICAL_BOUNDARY("MyCase", PhysicalWall)
```

Hydro 请求必须返回完整 primitive：正密度、有效压力或温度、三分量原生速度及所有
核素质量分数。能量由所选 EOS 构造，不能直接填 ghost 守恒能量。热力学域外状态、
非有限量、负质量分数或不满足归一的组分被拒绝，不在此处施加下限修补。

Diffusion 请求分别填写 `temperature`、`velocity[0..2]`、`species`。
即使是一维网格，横向速度通道也存在。组分通道一旦填写，必须覆盖全部注册核素。
相应扩散模块须已启用，否则拒绝指定该通道。扩散启用且算例注册了物理回调时，
Core 在非周期物理面调用其 Diffusion 分支；这允许反射流体壁同时指定热通量。

| 标量描述 | ghost／面处理 | CGS 量纲 |
| --- | --- | --- |
| `None` | 继承原有 ghost，包括反射壁与角点处理 | — |
| `Value(W)` | `Wghost = 2 W - Winterior` | 温度 K、速度 cm/s、质量分数无量纲 |
| `NormalGradient(g)` | `Wghost = Winterior + 2 d g`，`d` 是面到 ghost 的物理法向距离 | 标量量纲/cm |
| `OutwardFlux(q)` | 保留 ghost 基态，在实际扩散面通量处施加指定值 | 热 erg/(cm² s)、动量 g/(cm s²)、核素 g/(cm² s) |

外法向通量为正表示流出域。零热通量是绝热条件；反射流体壁本身不代表指定温度。
核素法向梯度／扩散通量之和须为零。黏性通量改变动量时，其机械功仍计入流体能量。
若 Diffusion 分支返回 `hydro`，该 primitive 显式替换继承的基态。Hydro 分支不能
通过 Value／NormalGradient 扩散通道偷偷改变流体状态。

## 引力边界

`gravity_type = self` 时，`gravity_boundary` 接受下列策略。

| 取值 | 含义 |
| --- | --- |
| `periodic` | 全周期泊松问题，源项减去体积平均密度，势取零体积平均规范 |
| `isolated` | 现有有限质量／径向／二维对数核的域外闭合；曲线域要求完整方位角 |
| `dirichlet` | 非周期物理面指定零势 |
| `neumann` | 非周期物理面指定零外法向势梯度；正质量域通常不相容并报错 |
| `user` | 由 `gravity_boundary.cpp` 返回逐面条件，可与成对周期方向组合 |

```cpp
#include <UserInterface.h>
#include <GlobalDefs.h>

arch::boundary::GravityBoundaryData PotentialWall(
    const arch::boundary::GravityBoundaryContext& ctx)
{
    const double G = arch::constants::gravity::cgs::gravitational_constant;
    const double rho = 1.0; // 示例密度，g/cm³
    const double x = ctx.native_position[0];
    return arch::boundary::GravityBoundaryData::Dirichlet(
        2.0 * arch::constants::math::pi * G * rho * x * x);
}
REGISTER_GRAVITY_BOUNDARY("MyCase", PotentialWall)
```

此公式是均匀笛卡尔平板的势边界示例，不是任意几何的孤立质量模型。
结果可用 `Dirichlet(Phi)`、`Neumann(dPhi_dn)`、`Robin(a,b,c)`、`Periodic()`。
Robin 表达 `a Phi + b dPhi/dn = c`，要求有限系数、`a >= 0`、`b > 0`；
Dirichlet 使用自己的构造函数。势单位 cm²/s²，梯度单位 cm/s²；例如选择 `a` 无量纲、
`b` 为 cm 时，`c` 与势同量纲。每一面在一个阶段内须使用同一类型及同一组 `a,b`；
`c` 可随位置和时间变化。跨阶段改变类型或 `a,b` 会重建算子与粗层缓存，只有 `c`
改变时更新 RHS。这里支持线性 Robin，不是任意非线性边界求解。

纯 Neumann 问题先检查 Gauss 相容性：

$$
\oint_{\partial\Omega}\partial_n\Phi\,dA
=4\pi G\int_\Omega\rho\,dV.
$$

相容时固定零体积平均势；不相容时停止，不通过扣除质量背景制造解。周期方向必须
成对，并与流体 AMR 拓扑匹配。流体反射和引力镜像质量是不同的物理选择。

## 坐标、阶段时间与 AMR

`ctx.config`、`ctx.species` 只读；`ctx.point` 同时提供 `PointCoords` 的各套坐标，
`ctx.native_position` 保持实际计算轴序。`ctx.axis/side` 标识面，`ctx.cartesian_normal`
是笛卡尔单位外法向；速度分量按原生正交基。`ctx.time` 是正在求值的实际 RK／RKL
输入阶段时间，可能与宏步起始时间不同。`ghost_depth`、`ghost_point`、
`physical_distance` 描述各层 ghost。不要保存上下文引用或依赖回调调用顺序。
回调可并行求值，必须确定、线程安全、没有跨块或全域状态修改。
同一输入状态、阶段时间和用途应返回相同结果；运行时可复用已完成的 ghost，
因此不要用调用次数、全局可变变量或随机数生成边界数据。

| 计算几何 | 一维 | 二维 | 三维 |
| --- | --- | --- | --- |
| Cartesian | x | (x,y) | (x,y,z) |
| Cylindrical | r | (r,z) | (r,z,phi) |
| Spherical | r | 赤道极坐标 (r,phi) | (r,theta,phi) |

二维柱坐标的物理点为 `x=r`、`y=0`、`z=z`、`phi_cy=0`；径向、轴向与方位速度是三个独立物理分量。Native RZ 完整科学与 Device 验收仍在进行，运行受能力检查约束；历史柱坐标极平面边界检查保留原坐标语义。

显式势条件允许物理上有效的环域、方位角扇区和球坐标楔域。根网格与 2:1 AMR 的
限制仍按[自引力计算域](../Reference.zh-CN.md#自引力计算域)执行。
圆心、轴线和极点由已有坐标接合／正则性处理；零面积接合面不接受任意用户填充。
内部 AMR 面不调用物理边界。角点采用 x1→x2→x3 的固定优先次序，最后活动轴拥有
交角 ghost；两端执行器使用同一内侧快照和规则。跨周期接合需要完整且一致的坐标图。

## 后端、诊断与重启

普通 `.cpp` 回调在 Host 执行。CUDA 仅传输回调需要的边界内侧／ghost 快照、
控制面和收支面数据；共享 EOS 与通量规则在两后端一致。现有内置边界保留驻留快速
路径。小网格的回调同步成本可能超过 GPU 收益，应结合域规模和输出频率计时。

- `boundary_fluxes.tsv`：按实际 RK/RKL 系数累计物理面质量、原生动量、流体能量、
  核素及热通量。正号表示流出；单位使用实际 `GridMetrics` 体积／面积。
- `gravity_boundary_exchange.tsv`：相邻发布势场的 `0.5∫rho Phi dV` 及 Green 边界
  交换项，记录采样成本。含 RK 阶段发布，时间次序可回转；不是单独的宏步总能量守恒证明。
  开放质量边界、势规范及流体引力功须在物理分析中一起考虑。
- CUDA 边界传输、kernel 和同步计数与内置执行成本分开记录。

这些累计值从当前进程启动开始，重启后重新累计。曲线坐标动量列是原生分量，不能
直接当作全域笛卡尔动量守恒量。Checkpoint 格式 7 保存面类型、算例及两种回调源码
摘要和科学自定义参数身份；跨机器／后端不依赖绝对源码路径。身份改变或缺失明确拒绝
续算。输出路径和后端选择不属于边界物理身份。

## 配置 v3 与函数式注册

每个算例必须声明它读取的参数。函数式算例使用 `REGISTER_PROBLEM_WITH_CONFIGURATION(NAME, SETUP_FUNC, INIT_FUNC, DESCRIBE_FUNC)`，由最后一个回调返回完整 `CaseConfiguration`；原三参数宏不补造声明或默认值。示例见 [UserGravity/case.cpp](../../simulation/UserGravity/case.cpp)。`user_boundary_heat_flux` 显式声明为浮点输入，单位 `erg/(cm^2*s)`；完整输入保留现有物理值。自引力使用内部固定 CGS 常数，不能再通过 `gravity_G` / `G_const` 改写。

完整的参数声明用于配置/初始化边界校验，与完整场及 AMR 的能力登记分别维护。新任意算例不会仅因注册便自动获得 Preview。
