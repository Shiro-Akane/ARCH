# Actual native ring RHS and original-request assessment

**COMPOSITION / NATIVE NORM PASS；ACTUAL NONZERO SOLVE NOT ACHIEVED — RING WorkLimit。**

基线f45d288d4e5ff621bf6641b8775bd7507ee44eaf，唯一/home/arch/projects/ARCH-compute-optim。
科学清单§7.3的source/B/assembly/A/evaluation/native RMS依赖贯通，不授予production RZ能力。

## 已实现的真实路径

GravityBoundary::assess_native_ring_rhs先校验当前source/density/operator/topology/generation，
再由真实producer生成root-scoped face errors；舍入坐标、Estimate或不一致身份不能提升scope。
source=-4*pi*shared CGS G*rho的构造误差逐cell消费；ideal B construction乘实际fhat，
ideal B乘root-scoped potential error，actual RHS assembly逐cell加入同一RHS ledger。
native A construction与actual apply/subtraction evaluation独立成项。
homogeneous boundary属于A，prescribed values属于B；assembly已计入RHS，不在evaluation重复加入。

CompositePoisson既有assessment复用同一原判据，显式RootDyadicRzWeights scope使用ideal native RMS：
computed RHS/residual和cell error ledger都重取native norm，不混入stored source/arithmetic norm。
stored scope保留旧行为，invalid scope明确拒绝；合并overflow明确失败。
T_safe=max(atol,rtol*max(0,norm_lower(computed_rhs)-RHS_error))；
residual_norm_upper+evaluation_error+RHS_error<=T_safe。
原rtol=1e-10、atol=0不变，zero保持精确零，无floor。
结果conditional只描述ideal native discrete原请求；physical_status始终UncertifiedInput，不能当作continuous Phi/force科学签收。

## 通过的检查

真实12组source/RHS（264 cells）：8精确root组可进入native组合，4舍入组明确Uncertified。
独立Fraction对8组176 cells验证ideal weights、cell-error RMS、computed RHS norm lower、
残差upper、T_safe及原请求比较；非零未求解数组拒绝，零数组接受。
fake zero residual、missing source、stale/different operator沿既有producer合同拒绝。
最终6项Core scoped CTest全PASS，9.51s；architecture audit和diff check PASS。
最终CPU Release ARCH SHA为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，与既有冻结JENS9短+9实际restart receipt匹配，复用不重跑。
最终production build parallel28，guard peak RSS1894044 KiB，swap growth0，未停止。

## 真实非零源检查与未关闭finding

首先1×1 root按现有active extent合同拒绝；2×2合法extent触发Degenerate composite interface interpolation。
这两个输入诊断保留，没有为fixture改写科学实现。
采用已有4×4 ring fixture形式：axis root origin=0、z origin=-0.5、spacing=0.25，
rho=1、共享CGS G、现有curvilinear isolated boundary。
内部face absolute potential target=1e-18、maximum_boxes_per_leaf=65536、maximum_leaf_evaluations=100000。
原caller请求仍rtol=1e-10、atol=0。这一内部积分份额不等于新科学阈值。

首次检查失败，补充状态诊断后以完全相同输入/预算再次检查：
203.241s，RingBoundaryStatus::WorkLimit（enum1），leaf evaluations192，
parent evaluations60，range evaluations1503290，kernel enclosures42066696，
AGM iterations245655391。
现有ring结果不能标Bounded，故严格在Poisson之前停止；没有实际Phi解、没有force结果、
off-axis后续case未到达，更没有simulation/long-run。
新independent solved reference仅验证failed partial record明确拒绝、不生成PASS文件；
其positive科学验证尚未完成，不据此关闭finding。

只读检查发现每次subdivision都线性遍历所有boxes重归约区间并选择largest width。
这解释高box数量下的一部分费用；本节点未改算法、求和顺序、预算或误差界。
下一步按原数学/精度要求处理实际预算finding，评估有界workspace归约/选择以及可靠积分界，
保持失败传播，不能仅增加box cap或降低精度来宣布完成。

## 复现与证据

    python3 validation/gravity/rz_ring_rhs_composition_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local directory>
    python3 validation/gravity/rz_native_ring_composition_reference.py --probe-record <directory>/probe.json --output <new summary>
    build-cpu/arch_composite_poisson native-ring-solved-probe

最后一项当前明确预期出现该WorkLimit finding，不能当作green验收。
通过后才可执行rz_native_ring_solved_reference.py，对真实Phi及native RHS box检查原请求。
处理后summary在validation/gravity/results/rz-native-physical-rhs-20261005/summary.json。
raw arrays、完整日志和ELF留studio/.local/integration/rz-native-physical-rhs-20261005，不上传H5/plt/checkpoint。
完整RZ、axis/viscosity、一般source/observer构造、连续Phi/force及CUDA/长跑门槛均保留。
