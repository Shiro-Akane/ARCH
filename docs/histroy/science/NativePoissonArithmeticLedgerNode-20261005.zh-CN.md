# 原生 Poisson RHS/residual 算术账本节点（2026-10-05）

## 结论与权限范围

CANONICAL ARITHMETIC PASS；PHYSICAL CONSTRUCTION PENDING；production RZ gate保留。
原CompositePoisson owner现在可给出实际RHS数组相对数学source+B*f、实际residual
数组相对数学A*phi-b的逐cell绝对误差界与stored-native-weight norm上界。

新结果类型显式带PoissonArithmeticScope::StoredNativeCoefficients。这个scope不是
完整科学签收：输入source、geometry/stencil构造、理想V/weights到存储double的
误差仍需独立证明/账本。没有给这些未知项填零，也没有自动将本结果转换为
完整物理CertifiedAbsolute或开放production RZ。

## 实际owner与算法

bound_rhs_assembly_roundoff复用实际faces、boundary_coefficient、area、volume，
按effective_rhs相同left/right符号构造数学表达式的outward基本运算区间。
bound_residual_evaluation_roundoff复用实际anchor/samples/coefficients与boundary
anchor项，围住canonical A*phi-rhs；没有假定CompensatedSum或provider计算值
本身精确，也没有再写另一套solver或导入physics ring函数。

输入computed数组可以来自现有scalar/provider执行结果。围住数学表达式后，
逐cell计算computed值到两个端点的最大距离并向外取界；若computed数组本身
错误，误差不会因“重复运行得相同值”而自动成为零。
最后用已有norm_interval按实际stored normalized native weights取得norm上界。

inputs、extent和finite条件不满足返回InvalidInput，无法表示的中间/结果返回
Overflow。精确零及已知精确消去保持零，无hidden floor。
依赖IEEE binary64基本运算、nextafter与既有不使用fast-math的构建约定。
这只围住定义为stored系数的数学operator；不能冒充几何fit已无误差。

## 独立精确验证

复用真实原生operator：Cartesian、RZ轴线、RZ离轴 × uniform/mixed topology，
每个ordinary、large-offset cancellation、exact-zero三lane，共18组。
local probe输出实际inputs、faces、volumes、weights、computed数组与bounds；
原始数组只留本机。

独立Python Fraction将每个FP64输入精确转换为有理数；边界B、anchor差分、
A*phi-rhs以及norm平方全部用精确有理算术。工具不调用production区间函数、
stencil builder、norm或physics代码。逐cell实际偏差都在返回界内；精确
sum(stored_weight*actual_error²)<=norm_upper²全部通过。
不靠双精度近似参考、不依赖观察误差乘经验系数。

最大cell界利用率0.3155102891709164；大数抵消lane实际最大RHS误差约
0.00928684、residual误差约0.01588789。这些是针对1e12级输入的算术测试值，
不是物理容差、连续误差或真实模型残差；数据含义及单位沿operator输入，
没有从其大小生成新科学阈值。

反例覆盖missing source、NaN residual、有限但错误的computed residual、实际
boundary/residual中间overflow。18组中零lane的cell与norm账本精确为零。
已有boundary-acceptance与boundary-ledger、ring-native-face回归通过；
4项CTest与architecture audit、diff check通过。

## 身份与构建

baseline d3323b5eed12c338015cebda0f9df7d490af9d36，
构建时本节点source修改存在，source/scoped ELF hashes见summary。
复用unique ARCH-compute-optim的CPU Release build-cpu，标准incremental
ARCH、arch_composite_poisson、arch_self_gravity、arch_gravity_stage_contract，
没有configure/newtree/新workspace。受影响build的memory guard未停止、
swap增长0；最后测试fixture补溢出反例后仅重新构建test目标并核对Fraction。

production ARCH ELF仍为d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e；
内部companion ledger由本次scoped ELF验证。匹配身份的JENS 9短演化+9实际
restart receipt沿用，无相关变化不重复运行。

复现：

    python3 validation/gravity/native_poisson_arithmetic_reference.py --probe build-cpu/arch_composite_poisson --output <local-summary.json>

原始native数组/face stencils/full logs位于本机：
studio/.local/integration/native-poisson-arithmetic-20261005。
仅交付处理后scalar、reference script、source与报告，不上传原始H5/plt/checkpoint。

## 仍需贯通的完整链

本节点补已知stored-operator RHS/evaluation算术项，不代表以下项完成：
1. source=4*pi*G*rho与其他原始源表达式的构造账本。
2. 从authoritative native geometry及face fit到存储coefficients/volumes/weights
   的定义和可靠误差；未知项不可用当前scope代替。
3. 真实ring/source tree/AMR epoch绑定，将face、source、construction、
   assembly与evaluation独立项合并到原T_safe门槛，不改变原rtol/atol。
4. general-parent far/translation、全RZ消费者及独立势/力/角动量科学签收；
   对应CPU签收后统一CUDA，再按冻结包启动长跑。

没有启用production RZ或Windows适配，没有修改tag或合并main。
