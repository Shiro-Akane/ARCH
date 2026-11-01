# 均匀网格 Poisson 与 multigrid 契约

本文定义 `CartesianPoisson`、`HostMultigrid` 和 `UniformGravity` 的离散约定与验收预算。复合 AMR 自引力见[实现职责](ImplementationOwnership.md#composite-self-gravity)和[引力验证](../../validation/gravity/README.md)。

## 范围与归属

- 单个完整 Cartesian 域，1D/2D/3D，cell-centered 势，均匀网格。
  各有效轴格数为不小于 2 的 2 次幂；各轴网格间距之比不超过 2。
  同时二分各轴，任一轴到 2 时停止，最粗层最多 64 个未知量。
  求解器检查这些网格条件；强各向异性和任意形状应使用适用的算子与求解方法。
- 全周期，或六个有效域面全部为给定势的 Dirichlet；允许非齐次面值。
  不提供混合、Neumann、孤立边界或曲线坐标解释。
- `numerics/elliptic/CartesianPoisson`：网格值类型、边界、统一算子/梯度、范数和数据检查。
  `numerics/multigrid/HostMultigrid`：辅助层所有权、循环、粗解；`MGTransfer`：带符号标量传递。
  `physics/gravity/UniformGravity`：CGS 密度源、周期均值及势到加速度的适配。
- 标量借用视图用于物理输入边界，内部使用连续标量数组。
  该均匀网格求解器与 AMR 复合求解器的适用域分别定义。

## 离散与求解

采用 `A=-laplacian`，`b=-4*pi*G*rho_source`，`g=-grad(phi)`。
周期物理源为 `rho-mean(rho)`，报告被移除的密度均值；势规范为零均值。
密度减均值后再次投影源项的常数模，并将其计入 `removed_rhs_mean`，避免弱扰动被背景密度均值的舍入误差误判为不相容。
通用求解器拒绝明显不相容的周期 RHS，仅投影 `64*epsilon*RMS(rhs)` 内的舍入均值并报告。
每级残差/修正均投影常数零空间；不给分母、密度或 LU 主元添加固定小值。

内部面梯度为两相邻 cell 值之差除以 h。左域面用边界值 B 和前两个 cell 的二次插值：
`dphi/dx=(-8*B+9*phi[0]-phi[1])/(3*h)`，右面使用对应反射公式。
算子为同一面梯度的负散度；边界行对角为 `4/h^2`，内邻系数 `-4/(3*h^2)`，
非齐次有效 RHS 增加 `8*B/(3*h^2)`。这样边界力也保留二阶精度。
该离散在普通欧氏内积下非对称，不宣称 SPD，不使用 CG。
cell 加速度为相邻面加速度的平均；验证同时覆盖全部面及全部 cell，不能省略边界误差。

固定 V-cycle，前后各 3 次权重 `2/3` 的 Jacobi；粗层重新离散同一算子。
restriction 为 `2^dim` 个细格的体积平均；prolongation 为 cell-centered 多线性插值，
周期环绕，齐次 Dirichlet 修正在域外作奇延拓。不复用 hydro limiter。
最粗层复用现有 `DenseLUSolver`，最大 64 个未知量；周期时固定一个值后解，再移除势均值。
不建立全域矩阵，不要求 KLU/cuDSS。粗矩阵在层次建立时分解，重复求解复用。

接口显式接收 `rtol, atol, max_cycles`，它们是当前数学接口的收敛契约，
不是新增文本配置键。平滑权重、次数和粗层上限封装在算法内部。
`atol` 必须正且有限，确保零 RHS 有明确准则；`0 <= rtol < 1`；循环上限正。
停止条件为重新计算最细层原算子残差的体积 RMS：
`RMS(b_effective-A*phi) <= max(atol, rtol*RMS(b_effective))`。
非齐次 Dirichlet 的 `b_effective` 包含边界项；不将内部平滑残差作为成功凭据。
不收敛或非有限结果仅返回失败报告，不暴露可被当成成功的势数组。

## 验收预算

- 周期正弦/余弦、非齐次 Dirichlet 的多项式加正弦制造解：1D/2D/3D，
  每轴 16、32、64 三档。势、cell 力、包含域边界的面力的 RMS 观察阶均不低于 **1.8**。
  归一化问题固定 `rtol=1e-12, atol=1e-13, max_cycles=100`。
- 常数、线性和二次场：独立代入检查符号、面通量、非零边界；
  单位尺度解析势/力的绝对误差分别不超过 `1e-9` / `1e-8`。
- 周期 CGS 密度 `rho0*(1+0.25*cos(k*x))`：
  `phi=-4*pi*G*(0.25*rho0)*cos(k*x)/k^2`；
  检查势、力方向、移除均值。`rho0=1,1e-30,1e-100`，域长 `1,1e6`；
  32 格的相对势/力 RMS 误差均不超过 **1%**，随量纲缩放后的归一化结果误差不超过 `1e-10`。
  atol 按该问题 RHS 尺度的 `1e-13` 设置，不使用绝对密度 floor。
- transfer：常数、内部线性修正、正负号、周期均值与体积 restriction 守恒；
  独立构造的周期离散 Fourier 特征值检验代数误差，不以生产算子自造 RHS 作为唯一参考。
- 附加弱扰动回归：`1e-10` 的相对密度扰动叠加在 `rho0=1,1e-100` 上，
  相位为 `0.17`，不能依赖恰好成对消去的格点采样；沿用 32 格的 1% 解析误差预算。
  Dirichlet 域面单独统计力误差及不低于 1.8 的阶数；周期余弦的域面法向导数为零，不对舍入噪声要求阶数。
- 零 RHS、非零初值/重复求解、非相容 RHS、坏网格/边界/输入视图、NaN/Inf、
  一次循环不足和计算溢出必须有明确行为；失败不返回势/引力字段。
- 最终 residual 用独立测试实现再次计算；循环数、误差、阶数和构建身份归档。
  AMR、动力学能量耦合和设备执行使用对应的独立测试。


## 验证入口

`poisson_multigrid_contract` 与 `poisson_multigrid_analytic` 属于 CTest。
[解析验证记录](../../validation/gravity/results/p2-20260922/README.md)保存网格序列、CGS 尺度、弱扰动与失败检查的输入和结果。
