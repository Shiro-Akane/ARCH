# RZ ideal final stencil outward enclosure node

**IDEAL STENCIL ENCLOSURE PASS；完整physical RHS/Phi/force及production RZ gate仍未签收。**

基线68294c71b2c1ca7012d92d2a6fb23e84898c514e，唯一工作区/home/arch/projects/ARCH-compute-optim。科学清单§7.3的系数构造实施节点，未修改物理、原拟合/LU/fallback策略或验收阈值。

## 实际所有者、输入和范围

CompositePoisson::native_rz_stencil_enclosure(face_index)从当前不可变owner的实际最终face读取samples和FaceStencilConstruction，返回理想root-dyadic RZ最终stencil区间及真实stored coefficient偏差界。只接受Cylindrical/AxisymmetricRz/dimension2与当前有效face index，非RZ及无效index拒绝。

source/face几何依赖的当前整数dyadic root identity给出理想normalized offsets；共同origin解析消去，避免将相消后的stored center差当成精确基底。逐操作向外舍入构造seed、polynomial basis、weights、Gram与right。最终anchor仍按原常数约束重置。TwoPoint/已记录EllipticRecovery按原两点公式围住，不当成quadratic。结果为transient proof packet，不新增evolved geometry/state、物理源项或Poisson solver。

本节点认证的是理想root geometry上的最终所选stencil数学系数。它不认证选中stencil的空间截断误差、face面积、源/observer存储坐标差异、源边界积分或连续Phi/力；完整物理残差需继续组合这些已有或待证依赖。

## LU误差界的非循环证明

现有DenseLUSolver只产生有限FP64候选C（inverse witness）和lambda_hat；不假设其逆矩阵/解精确，也不另造solver。

以向外区间计算所有可能理想Gram的 q=norm_inf(I-C*G)。仅q<1时利用Neumann级数：
norm_inf(G^-1) <= norm_inf(C)/(1-q)。
因此 norm_inf(lambda_exact-lambda_hat) <= inverse_upper*norm_inf(right-G*lambda_hat)。

matrix norm/reduction、1-q下界、division/product均向外；rhs残差包含理想坐标/basis/weights/Gram构造与候选LU误差。该lambda区间再经原weighted correction、scale、boundary项和anchor reset传播成最终coefficient区间；stored coefficient到上下端点的最坏距离向外给出error upper。非有限、不可证明q<1或候选LU失败只能返回未认证，不以epsilon/floor放行，不从观测误差拟合阈值。

## 独立参考与真实层级

现有独立Fraction root-coordinate参考重建理想系数；新独立校验逐项验证区间包含及真实误差上界，并精确反解6×6 Gram的所有列，核对其真实inverse infinity norm不超过返回upper。未调用生产LU或interval helpers。

38个实际operator，7464面，38752 coefficient terms全部通过：
5416 TwoPoint，2048 PolynomialFit，0 EllipticRecovery。
包括24个原始axis/nonaxis/nonbinary origin和合法anisotropy fixture，及4套真实64×4 Multigrid hierarchy；10个实际派生coarse operator、最大spacing ratio4。不是通过公开coarse flag伪造mesh。

CompositeMultigrid新增只读level_operator(level)查询；coarse构造仍为private、hierarchy owner专有，既有physical root ratio<=2规则不变。无效level拒绝。

本组inverse defect upper最大1.540823024726025e-11<1；coefficient区间宽度最大4.283883470179717e-08 inverse-native-length。它是保守构造证明，不保证任意输入能满足原用户residual请求；不能据此放宽请求。未来组合若预算不够，需真实细化证明/计算或明确失败。

实际recovery仍为0，本组不宣称覆盖该分支。策略已记录且代码有对应解析分支，仍需自然触发fixture的独立验证。没有在科学检查中改造浮点结果或人为触发fallback。

## 构建、回归、binary身份

复用原CPU Release tree，parallel28增量构建；两次memory guard未停止、swap growth0。初次11.139s/peak2577036 KiB，hierarchy接入10.142s/peak1740684 KiB；最终测试fixture扩展只重编译测试。

生产ARCH SHA256保持：
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
与68294c71匹配receipt相同，沿用该冻结JENS9短+9真实restart PASS，不重复无相关变化的短包。输入、共享G、终点与门槛未变。

最终6项Core CTest（physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics）全PASS，10.73s。architecture audit、diff check PASS。未configure/new build tree，未启动simulation/新长跑或CUDA。

## 交付与下一步

复现：

    python3 validation/gravity/rz_stencil_enclosure_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>

标量summary在validation/gravity/results/rz-stencil-enclosure-20261005/summary.json，记录准确源码/probe/binary及复用JENS receipt身份。
raw faces/Gram参考数组、完整日志与ELF留本机studio/.local/integration/rz-stencil-enclosure-20261005，不提交H5/plt/checkpoint。

继续face area/source/observer geometry和完整physical operator/RHS ledger，以及自然recovery、独立Phi/force、axis/viscosity和RZ全消费者科学gate。保持production RZ能力门槛；对应CPU通过后才统一CUDA和获批计时/长跑。未修改root STATUS，未merge main，未开展Windows适配。
