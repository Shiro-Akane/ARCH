# Typed CPU ring 执行器与真实 CUDA 预先拒绝

基线5a423d057996eccb39916996e6e9ca42735be9a2，唯一Linux/WSL工作区。
执行器合同PASS，不是完整RZ Runtime/科学或Device ring数值签收。

## 同一数学所有者的实际接线

原GravityWork新增EvaluateRingBoundary：同步借用GravityBoundary/op/source/control，
结果拥有数组并保留source generation、Bounded/WorkLimit/PrecisionLimit及工作量。
原HostGravityExecution直接调用原ring_boundary，未复制积分、矩、误差或G公式。
每次调用先退役旧result；stale/missing descriptor不能保留旧Bounded证书，
WorkLimit本身沿原接口返回带请求身份的诊断，未静默转成功。
该工作项不是field publication，也不改变SelfGravity bind gate。

旧EvaluateBoundary现在携带显式GeometrySemantics，SelfGravity传实际op.base().semantics。
Host/CUDA在调用旧log kernel之前拒绝RZ chart，不修改Existing原循环/算术。
CUDA明确拒绝typed ring（CPU科学签收前无数值实现），先清空Host-owned旧证书，
不调用Host fallback、不访问借用owner、不启动kernel/transfer。

## 真实合同与范围

原self_gravity_lifecycle新增一个既有数学预算的16-cell轴线native源：
r/z各4格、rho1、共享CGS G，face target1e-18、65536 boxes/leaf、100000总cap。
通过实际Host executor得到Bounded result，再以原tree验证current/source/generation。
stale version、missing owner、WorkLimit limit1和legacy RZ拒绝均通过。
单个numerical-domain dependency不是生产Runtime的所有block依赖证明，
该样本无EOS/Hydro/time integration，不能冒充注册模型演化。

原CPU CTest1/1 PASS（13.19秒）。CUDA-enabled Host同一CTest PASS（13.00秒）。
原4070Ti cuda_reduction_contract通过：36原边界case不变，
新增真实executor拒绝前后kernels/bytes_h2d/bytes_d2h/synchronizations增量全0，
Bounded哨兵结果退役、legacy RZ log拒绝（Device哨兵不是已认证ring source）。只验证拒绝，不宣称Device RZ evaluation。

GravityWork类型变化后重编原actual Cartesian Runtime六源、链接当前archive，
4→8→4、7gather/time0/steps0及租约失效/recovery PASS。
原3584-cell RZ face/work/cell acceleration检查仍通过；production bind gate未解除。

## 构建与证据

受影响CPU target增量5.022秒、owned RSS峰982044KiB；
实际CUDA executor/reduction合同增量16.055秒、峰955420KiB；swap0。
NVCC对未修改的header报告defaulted-equality used-but-never-defined warnings，保留原日志；
编译链接和实机运行PASS，不把warning删掉或宣称没有warning。
原架构审计/diff检查PASS；无configure、main merge、新worktree或Windows工作。

[处理后summary](../../validation/gravity/results/rz-typed-ring-execution-20261005/summary.json)
含source/test ELF/生产ELF和actualRuntime身份。
原始build/test log及ELF留studio/.local/integration/rz-typed-ring-execution-20261005、
同prefix build logs和rz-typed-ring-runtime-20261005。未上传原始数组。

完整CPU/CUDA ARCH ELF未改变，既有匹配receipt仍有效，不重复原完整包；
新source不能用旧生产binary/managed Manifest声称fresh。

## 尚待贯通

RZ-BOUNDARY-DISPATCH-01只在typed executor/legacy拒绝范围推进；
SelfGravity::prepare还未选择RZ ring路径，production bind/regrid仍停止。
后续要接实际全块Current/Scratch/Next density、topology和source generation，
按已批准误差判据组合ring/source/native RHS/residual、solve/force-work/publication。
一般坐标误差、连续Phi/force、axis/viscosity/角动量及完整A→B→C→D门槛保留。
不把固定face target当新的用户配置或适用于一般演化的已冻结budget。
未运行RZ simulation、生成H5/checkpoint、新长跑或benchmark；公开CUDA JENS gate未变。
