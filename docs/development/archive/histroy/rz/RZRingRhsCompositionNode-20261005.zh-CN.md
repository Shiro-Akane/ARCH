# RZ ring / physical source / RHS 组合节点

CONDITIONAL STORED-OPERATOR PASS；physical status 明确为 UncertifiedInput，production RZ gate 保留。基线 c36489ec0cf3c2ecf752ded93a451c1e1bf4881c；当前位于主计划 O7.2–O7.5 的科学清单 §7.3 组合消费实施，不代表完整阶段完成。

## 实际接线和证明范围

GravityBoundary::assess_ring_rhs 要求本 owner 当前 ring generation、完整 source identity 与 native operator 相同。source_error 使用当前实际 density 和共享 CGS G 围住数学 -4*pi*G*rho 与实际 CPU provider source 的偏差；非共享 G 不获得本物理 source companion。boundary_error 经 canonical B 传播；assembly/residual 分别围住实际 computed arrays 与对应 stored operator 数学表达式。

逐 cell 向外求和 source_error + boundary_error + assembly_error，再按原 stored native weights 取 norm；residual_evaluation 独立加入 residual norm。assembly 已在逐 cell 项中计入，最终接口不重复计数。原 rtol/atol 与 T_safe 定义保持，没有 absolute floor。精确零保持零。

返回类型显式 RingRhsAssessmentScope::StoredNativeOperator，physical_status 固定保持 UncertifiedInput：本节点没有证明从原生理想几何到 stored volumes/face fit/stencil/weights 的构造误差，不能将缺项当零，也不能把 conditional Accepted 提升为 production field publication。原 RZ values() 仍拒绝，未新建 simulation 或直接 force path。

## 独立实际验证

真实 CPU execution.linear source → 当前真实 ring tree → canonical effective_rhs/apply → composition。axis/nonaxis × uniform/mixed × positive/exact-zero 共8组、176 cells；无 simulation timestep 或 H5输出。positive样本使用 phi=0，故是未解数组，原 rtol=1e-10/atol=0 下应拒绝，不作为真实 Poisson 解签收；zero样本 conditional Accepted 且全部 error/tolerance 精确零，physical status 仍 unknown。

独立 Python Fraction 以实际 stored face area/coefficient/volume 构造 B，传播 source/face interval 的所有逐 cell 极值；actual RHS 到 exact interval 两端的最坏偏差均不超过组合界。exact weighted error norm² 与 bound norm² 均不超过返回 upper²。数学物理 source 另用100/140位 pi/G/rho检查包含。工具不调用 production interval/assembly/norm 函数。

source、boundary、assembly、evaluation、combined RHS、total residual 与 safe tolerance 分列在处理后 summary。单位分别沿 source/RHS/residual 为 s^-2，face potential 为 cm²/s²，不能直接数值比较。此检查限定输入区间与 stored native B/weights，不能替代连续势/力或完整物理构造参考。

伪造有限零 residual 被实际 evaluation ledger 揭示；旧 source generation、缺失 source 被拒绝。6项受影响 CTest：physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics 全通过。architecture audit、diff check 通过。

## 构建与匹配短 gate

复用唯一工作区和原 CPU Release tree，parallel=28增量编译；memory guard未停止、swap growth0。production ARCH SHA256 改为 ea67946b8e14a32ec7829657708489e96ab013eb97ac3914133a7b300d38547b，因此没有沿用旧 binary receipt。

该新 binary 的冻结 uniform-lifecycle-1 完整9短演化＋9实际 .01 checkpoint restart 至 .02 全PASS；质量/能量漂移0，max JENS relative error=4.50750606346323e-17，restart均bit-exact。输入、阈值、终点不变；这是 Cartesian 单caloric-species CPU JENS 短包，不签收完整RZ/CUDA/O9。

## 下一依赖与交付

下一项为 authoritative native geometry/volume、face fit/coefficient 与 weights 构造误差，以及相同 source/AMR identity 下完整 physical RHS 门槛。另保留 viscosity、axis-force、旋转全消费者及独立 Phi/force 科学 gate；对应 CPU 签收后才统一 CUDA、冻结短计时/长跑。当前已完成组合范围不自动解锁它们。

复现：
    python3 validation/gravity/rz_ring_rhs_composition_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>
    build-cpu/arch_composite_poisson ring-rhs-probe

源码、独立脚本、标量summary供review；原始数组、完整日志、ELF、H5/plt/checkpoint仅留本机 studio/.local/integration/rz-ring-rhs-composition-20261005。未改根STATUS、未merge main、未做Windows适配。
