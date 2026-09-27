# P2：单层 CPU Poisson / multigrid 原型

状态：数值选择和预算在实现前冻结；P2 原型、解析验证与封包回归全部通过。
日期：2026-09-22。基线为已验证并推送的 P1.5 `780794b6`。
本文补充主计划 D-01、D-02、D-04 的单层部分；P3–P6 的决定不在此处提前落实。

## 范围与归属

- 单个完整 Cartesian 域，1D/2D/3D，cell-centered 势，均匀网格。
  各有效轴格数为不小于 2 的 2 次幂；各轴网格间距之比不超过 2。
  同时二分各轴，任一轴到 2 时停止，最粗层最多 64 个未知量。
  这些是原型明确拒绝超出范围的条件，不承诺强各向异性或任意形状性能。
- 全周期，或六个有效域面全部为给定势的 Dirichlet；允许非齐次面值。
  不提供混合、Neumann、孤立边界或曲线坐标解释。
- `numerics/elliptic/CartesianPoisson`：网格值类型、边界、统一算子/梯度、范数和数据检查。
  `numerics/multigrid/HostMultigrid`：辅助层所有权、循环、粗解；`MGTransfer`：带符号标量传递。
  `physics/gravity/UniformGravity`：CGS 密度源、周期均值及势到加速度的适配。
- P1 的标量借用视图用于物理输入边界；内部使用连续标量数组，不读取 fluid packing、AMR 父块或 Driver。
  此原型不发布生产字段，不改配置/schema，不启用 `gravity_type=self`。

## D-01 / D-02：在数值结果产生前冻结的选择

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

## D-04：预先固定的验收预算

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
  不把本阶段结果称为 AMR 自引力、动力学能量守恒或 GPU 加速验收。

## 后续接续

通用 stencil 与 transfer 叶子可供后续设备执行器调用；本阶段只实现 Host 所有权和循环。
P3 才接入 AMR 标量布局、粗细通量和 composite residual；P4 才接入字段发布及 hydro 能量耦合。
P6 才验证设备驻留、跨层执行及端到端加速。当前不新增空 CUDA 执行器。

参考：AMReX 的 [linear solvers](https://amrex-codes.github.io/amrex/docs_html/LinearSolvers.html)
将物理边界与几何多重网格分开；PETSc 的 [KSP/PCMG 文档](https://petsc.org/main/manual/ksp/)
说明粗层、transfer、平滑器和残差之间的职责。此处离散公式与验收由 ARCH 自身独立测试验证。

## 实施记录与维护性

代码落点与上述归属一致，共新增 7 个生产文件：算子头/实现、MG 头/实现与 transfer 叶子、
引力适配头/实现。短头文件提供明确的模块边界，与实现成对，不为了达到行数阈值合并无关职责。
新增代码未触及 Driver、GUI/schema、AMR、EOS、生产重力源或配置控制面；gravity 目录仍为 8 个直接文件。

[验证封包](../../validation/gravity/results/p2-20260922/README.md)记录 18 个三档网格解、
6 个 CGS 尺度解、常密度二次势、弱扰动及接口/失败检查。`poisson_multigrid_contract` 与
`poisson_multigrid_analytic` 接入 CTest 和 CPU CI 必需覆盖项。
验证脚本复用已有 provenance 与运行日志工具，不新增数学 oracle 的第二份生产实现。

最终 Dirichlet 离散采用本文件冻结的二次边界梯度；欧氏非对称性不被掩盖。
周期低对比度源项修正在引力组装端完成并记账，通用求解器的相容性检查未放宽。
ASan/UBSan/泄漏检测使用同一测试程序；CUDA 只进行新增代码编译兼容检查。
下一阶段是 P3 的多块/静态 AMR composite 接口与通量，不自动开放 `self`。

验收完成：CPU CTest 57/57、Release 数学契约与解析矩阵、ASan/UBSan/泄漏检测两模式、
架构/警告检查和 CUDA 编译兼容均通过；CI 结果检查工具 10/10。详见验证封包，不把编译检查计为 P6。
