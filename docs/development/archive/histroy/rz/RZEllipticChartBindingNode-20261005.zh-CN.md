# RZ tree→elliptic chart identity 节点（2026-10-05）

## 范围与 finding

起始HEAD 897aabad73699c77cf998b2e77244bcca2ac60fd，唯一Linux/WSL工作区。
INTERFACE PASS，完整RZ Runtime/science仍未通过。
RZ-ELLIPTIC-SEMANTICS-01：原EllipticMesh已有semantics字段，但实际AMR adapter
没有从tree传入该身份；fragment/native volume比较也都使用Existing view，
因而不能证明内部RZ已经连接到真实native elliptic消费者。

## 最小修正与所有权

原EllipticMeshAdapter从tree.GetGeometrySemantics()复制权威chart；
fragment/native cell两侧消费相同GridMetrics显式view，不改公式、阈值或几何默认。
Existing Cartesian/cylindrical/spherical路径保持。原CompositePoisson因此看到真实RZ标记，
使用z两端Dirichlet，不能把轴向位置当作azimuthal periodic turn。
没有新增RZ名字推断、第二套source或AMR/elliptic状态。

真实SelfGravity尚使用旧EvaluateBoundary、physical-width/position与force/work workspace。
现在在bind、旧publication失效后明确拒绝AxisymmetricRz，早于legacy workspace构造。
这保留生产科学门槛；不是通过增加一个gate来宣布剩余consumer已完成。
下一步仍须接finite-ring边界/原生度量/force/work/实际stage来源及residual账本，
通过完整科学出口后才移除此gate。JENS公开门槛未动；被拒绝的候选patch没有应用。

## 真实tree与原CTest结果

原self_gravity_lifecycle扩展axis r=[0,1]与off-axis r=[1,2]，z=[-.5,.5]。
每例2个root，再由原PrepareRegrid→migration→publish→retire细化一个root到5个mixed叶，
epoch17→18。不是合成替代mesh；使用原树、Grid、storage offset、transfer。
uniform/mixed共3584原生单元，composite/native full-ring volume逐单元FP64精确相等；
z两端Dirichlet、原生offset、RZ拒绝、旧Cartesian场失效及受支持rebind恢复通过。
这证明两消费路径一致，不代替独立几何构造误差或连续Phi/force科学参考。

原2D/3D Cartesian vector force回归及生命周期保持PASS。
审计相关旧工程fixture发现.refine=.01/default derefine=.2及coarsen=(1,1)违反Core关系；
现在(.01,.005)、(1,.5)逐次复用CurvatureThresholds检查，coarsen保持真实DENS指标。
原科学force/residual/能量预算不变，没有修改生产数学，也不重写历史receipt。
此前mixed测试PASS仍保留日志，最终合法输入下重新测试1/1 PASS。

原run_gravity_runtime_contract.py新增重编当前adapter，防止验证旧ARCH object；
实际Runtime→GravityStage→SelfGravity的4→8→4、7次gather、
JENS lease拒绝/恢复通过，time0/steps0。
该Runtime witness仍是Cartesian；不能用它标记RZ runtime source publication已通过。

CUDA-enabled build下同一Host CTest 1/1 PASS。
没有实际device RZ求解；原device reduction数学未变，不重复未受影响CUDA叶基线。
CPU生产ARCH仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
冻结9+9匹配receipt不重复；旧Manifest不能表示当前变化源码的新鲜度。
没有运行Simulation、Plotfile、checkpoint或正式benchmark/长跑。

## 失败、检查与证据

首轮新代码编译失败：误用Grid private validator及漏amr::BLOCK命名空间。
保留完整错误，修正为既有public GridMetrics view验证和正确namespace；
不公开private接口、不加数学规则或降低门槛。
最终arch_self_gravity CPU增量5.019s、CUDA-enabled Host增量5.030s，
28并行、swap0、memory/pressure guard未触发。architecture/diff检查通过。

复现：
- cmake --build build-cpu --target arch_self_gravity --parallel 28
- ctest --test-dir build-cpu -R '^self_gravity_lifecycle$' -V
- python3 validation/gravity/run_gravity_runtime_contract.py --build build-cpu --output-root <new-local-folder>
- cmake --build build-cuda --target arch_self_gravity --parallel 28
- ctest --test-dir build-cuda -R '^self_gravity_lifecycle$' -V （Host execution）

处理后摘要validation/gravity/results/rz-elliptic-chart-binding-20261005/summary.json。
raw日志和ELF留studio/.local/integration；只提交处理后身份、指标与源码。
本节点关闭仅adapter接口finding，不关闭RZ轴向力/viscosity/连续环体科学finding、
完整A→B→C→D或JENS CUDA短包；不开展Windows适配。
