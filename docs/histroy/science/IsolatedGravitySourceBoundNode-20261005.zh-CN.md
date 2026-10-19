# 隔离重力源构造界节点（2026-10-05）

## 结论

ISOLATED SOURCE CONSTRUCTION PASS；FULL CHAIN PENDING；production RZ gate保留。
补实际self gravity在isolated域使用的 -4*pi*G*rho 源构造可靠界。
函数是companion certificate：接收原生density和已计算source，检查其相对数学
公式的偏差；不是新的source producer、初始化函数、solver或正式演化路径。
既有SelfGravity/UniformGravity生产源构造未修改。

## 实际路径与区别

只读核对SelfGravity::prepare：isolated保留总rho；periodic使用density mean、
执行线性组合并project。UniformGravity同样另有mean subtraction与projection。
本节点不把正总rho冒充periodic fluctuation。op.has_constant_nullspace时
显式UnsupportedPeriodic；periodic mean/reduction/project的证书仍独立待实施。

GatherDensity/正式模型对实际流体正密度的条件未改变。rho=0只作本节点椭圆
数学exact-zero fixture，不能由本测试宣布允许真实零密度流体。
G只用共享CGS常数，不提供额外G输入；原生rho/shared G的stored FP64数值
按权威输入消费，不复制物理初始化或改变单位。数学pi由Core pi相邻double
包围，明确包含pi常数表示以及乘法的舍入。

## 可靠界

对nonnegative stored rho，分别围住4*pi*G，再乘rho得到source上下界。
与实际computed source的逐cell距离向外取上界；精确零保持零。
实际norm_upper沿既有stored native norm weights，不能当作理想几何weight
构造误差已被证明。source与RHS/residual的算术项必须分开，后续按相同单位
s^-2合并，不与边界势误差cm²/s²直接比较。

PositiveSource被提供为zero返回CollapsedToZero，而不猜测一定由underflow
造成；负/非有限rho、NaN source拒绝。没有用floor让微小正源变为零。
无法表达可靠finite bound返回Overflow，不继续伪装收敛。
函数没有自动授予完整物理residual quality或publication token。

## 独立验证

真实mixed native RZ operator的28个cells，密度重复覆盖：
0、1e-310、1e-300、1e-10、1、1e7、1e100、1e300和最大finite double。
实际source以现有production factor形式计算；独立工具不导入production
bounds/source函数，用literal高精度pi、exact-from-float的shared G/rho，
100/140位逐cell核对source区间与误差界，stored-weight norm平方也通过。

可表示的次正规source不能承诺统一relative epsilon；本节点给绝对可靠界。
这些极端数值是数学fixture，不是推荐模型、用户默认或新科学阈值。
最小次正规rho导致正source提供为zero的失败保留，未用它声明零源。
exact-zero cell/error/norm维持0；负rho、NaN、错误巨大source/error overflow、
periodic域拒绝等反例通过。

当前Core targeted CTest四项、architecture audit、boundary-acceptance、
既有native arithmetic Fraction reference及diff check通过。
不将static构造检查当作演化、EOS、势/力或完整RZ科学签收。

## 身份、构建与留存

baseline e4c1a916459fdac72a8bc406a4c0e71c1c994c3b，
构建时本节点修改存在，文件hash与实际scoped ELF身份见summary。
复用unique ARCH-compute-optim、既有CPU Release build-cpu，仅standard
incremental targets；未configure/newtree/新workspace。memory guard未停止，
swap增长0。

production ARCH ELF保持d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e；
新的companion路径由本次scoped ELF验证；原匹配JENS 9短演化+9实际restart
receipt不重复运行。

复现：

    python3 validation/gravity/isolated_gravity_source_reference.py --probe build-cpu/arch_composite_poisson --output <local-summary.json>

原生arrays/full logs留本机studio/.local/integration/isolated-gravity-source-20261005；
只交付source、独立工具、processed scalar摘要与报告，不上传H5/plt/checkpoint。

## 下一依赖

理想native geometry/stencil/weight构造、periodic projection、真实source/tree/AMR
身份、整条original RHS/residual组合账本仍需完成。general-parent far arithmetic、
RZ角动量全消费者与未决RZ-VISC-01/RZ-AXIS-01参考保持。对应CPU科学签收后才
统一CUDA及冻结长跑；不启用production RZ、Windows适配或修改tag/main。
