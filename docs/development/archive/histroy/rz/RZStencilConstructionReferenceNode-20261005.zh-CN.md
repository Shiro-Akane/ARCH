# RZ final stencil identity and exact reference node

**EXACT REFERENCE RECONSTRUCTION PASS；production outward coefficient certificate 尚未完成。**

基线 ad6fe1f1b9af53f2a284c9d32f22cb79b90e4abe，唯一工作区 /home/arch/projects/ARCH-compute-optim。科学清单 §7.3 系数构造依赖的一项交付，不调整物理定义、验收阈值或 production RZ gate。

## 实际构造路径与实现

CompositeFace 新增 FaceStencilConstruction：TwoPoint / PolynomialFit / EllipticRecovery，由真实 build_faces、fit_interface 和对角线 recovery 分支记录。没有从系数形状猜测拟合方式；没有修改原拟合、LU pivot、fallback 条件、face coefficients、mesh topology 或求解策略。

真实 boundary/coarse-fine 拟合先扩展2层邻接集合，再对二次多项式作 minimum weighted correction：
G=P W P^T，lambda=G^-1(d-P*c0)，c=c0+W P^T lambda。boundary还含原 boundary value 的常数项；最后按原 anchor reset 约束常数。最终系数如因对角线检查恢复 two-point，必须消费 recovery 分支，不能将其宣称 quadratic reproduction。

独立工具使用实际 operator 的 root/leaf identity 与最终面邻接，精确重建理想 dyadic cell/fragment center、scale、初始 two-point 和 weighted Gram；Fraction 逐项有理计算，用独立 exact elimination 求解，未调用生产 DenseLUSolver 或 interval helper。理想拟合的全部6项多项式约束逐项精确成立；生产系数、boundary coefficient、center 与 constant constraint 的差异单独记录，未将观察值乘系数作为新 gate。

## 实际覆盖与 finding

24组：axis/nonaxis/nonbinary origin、uniform/mixed、binary/nonbinary spacing，以及合法2:1 spacing的axis-seam层级。共1416面、8268 coefficient terms；876 TwoPoint、540 PolynomialFit、0 EllipticRecovery。

因此本组没有实际触发 recovery；不能冒称该分支已经独立验收。参考实现可读取 recovery 路径并以原 two-point 数学比较，但其真实触发 fixture 与 hierarchy-derived anisotropy 覆盖仍待补。

最大实际 coefficient discrepancy 5.458860474087345e-14 inverse-native-length，boundary coefficient discrepancy 6.044106028526686e-14 inverse-native-length；最大 root-coordinate discrepancy 1.1102230246251565e-16 native length；constant constraint defect 3.344546861683284e-15 inverse-native-length。它们是精确参考下的测量结果，不是任意 mesh 的可靠误差界或新的验收阈值。

完整物理 ledger 不能只沿用stored coefficients。下一步须围住 root-coordinate basis、Gram、LU/系数构造（包括记录的真实fallback），再与已有 volume/ideal weights、boundary/source/evaluation 组合。该节点不解决 source/observer 几何差异，也不关闭连续势/力、轴力或viscosity finding。

## CPU binary 与回归

复用原CPU Release tree增量编译，parallel28，memory guard未停止、swap growth0、peak owned RSS2566364 KiB。最后probe扩展仅增量重编译测试，不改生产binary。

构造分支metadata改变CompositeFace布局，ARCH fingerprint因此改变；不能沿用旧ELF receipt。新CPU ARCH SHA256：
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。

该新binary冻结uniform-lifecycle-1的9短演化与9真实 .01 checkpoint → .02 restart 全PASS；质量和总能量漂移0，max JENS relative error4.50750606346323e-17，restart bit-exact。输入、终点、shared G、阈值未改；仅Cartesian single-caloric-species CPU短包，不签收完整RZ/CUDA。执行guard未停止、52.138s、peak817944 KiB、swap growth0。

最终6项CTest：physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics PASS（11.47s），architecture audit、diff check PASS。未重新configure，未新建build tree，未启动未获批长跑。

## Review 和本地证据

复现：

    python3 validation/gravity/rz_stencil_construction_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>

处理后摘要 validation/gravity/results/rz-stencil-construction-20261005/summary.json 包含准确binary/probe/source/input fingerprints及标量结果。raw面/coefficients/arrays、H5/plt/checkpoint、完整日志与ELF在本机 studio/.local/integration/rz-stencil-construction-20261005，不提交原始数据。

未修改root STATUS、未merge main、未开展Windows适配，未解除RZ能力门槛。CPU对应科学gate通过后再统一CUDA、冻结计时与长跑。
