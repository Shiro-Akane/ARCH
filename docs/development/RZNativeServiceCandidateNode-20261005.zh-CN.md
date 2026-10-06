# RZ 原服务候选接线与 WorkLimit

基线11c3a3baa2d85b6dec8c789d9c964c944e6f3ab6，唯一Linux/WSL工作区。
**工程回归PASS；非零真实服务WorkLimit，未进入Poisson。**

## 实施与资格

SelfGravity新增显式bind_native_rz_candidate内部CPU验证入口，默认RZ bind/
配置/API/Driver生产路径保持拒绝。候选消费真实native grid/handle/storage、
原GatherDensity每块view、同一GravityBoundary source/generation、dynamic初始预算
和typed ring；再接原RHS/solver/gradient/face/work/CellAcceleration/publication。
代码接线不等于后续分支已运行。代数目标内部预留一半，原rtol/atol未改；
完整原请求assessment必须Accepted，且physical_status仍UncertifiedInput。

GravityFieldStamp区分ExistingPhysics与NativeRzCandidate。
默认matches只接受ExistingPhysics，候选只能显式native getters读取。
普通potential/acceleration/report/timestep/Hydro patch按同一require拒绝候选；
unknown scope在publication前拒绝。默认已有路径scope/数学不变。
没有给公开RZ或CUDA JENS新增能力，不以native discrete残差通过充当连续势力签收。

## 真实非零结果

原AMRControl真实两块，各16×16，共512cells；
r=[0,1]、z=[-0.5,0.5]、rho1、代表E10，无Hydro/EOS演化。
两个完整native Host view/inputs进入原服务，shared CGS G、
原rtol1e-10/atol0、cycles200、65536 boxes/leaf和100000 total work不变。
初始提议target6.185029400051462e-20 cm²/s²。

RingBoundaryStatus::WorkLimit；leaf40960、parent13680，total54640未耗尽global100000。
242.433秒、owned RSS峰21920KiB、swap0、guard未停止。
**没有Poisson、Phi/force/work结果或candidate field成功publication。**
当次ELF/源码SHA和完整失败日志保留，不能用后续测试版本冒充failed producer。

新增RZ-NATIVE-SERVICE-BUDGET-01 OPEN：source scale只是初始提议，
后续须检查其与实际isolated RHS/B尺度及finite-leaf工作量的关系。
不能提升work cap/放宽rtol-atol，不把failed enclosure当Bounded，
不以未完成数组或估计量当certificate；新的内部份额仍须由原请求完整ledger验收。

## 回归及限制

CPU self_gravity_lifecycle 11.89s PASS，含候选scope/unknown/retirement；
gravity_stage_contract PASS；actualCartesianRuntime重编六源，4→8→4、
7gather/time0/steps0及非首块/slot/租约回归PASS。
这不是DriverRuntime RZ regrid/evolution验证。

零数学源尝试被原GatherDensity严格正密度条件拒绝，原失败日志保留。
明确负例复测确认拒绝和无publication；未修改正密度规则，
未用零源替代非零失败。scope unit PASS不证明native成功后的读取隔离，
正例后段getter/非首块负例因WorkLimit尚未执行。

architecture/diff PASS。非零CPU子组未通过，不进入CUDA数值验收/长跑。
生产ARCH ELF未重建，旧receipt保留原编译身份，不声称current source fresh。
没有simulation、新H5/plt/checkpoint、benchmark或Windows适配。

[处理后summary](../../validation/gravity/results/rz-native-service-20261005/summary.json)
含当次failed producer、当前source/test/生产ELF、失败和回归指标。
raw/ELF/完整log在studio/.local/integration/rz-native-service-20261005及同prefix logs；
actualRuntime在rz-native-service-runtime-20261005，本机持久保留。

下一步修复上述初始work allocation，完成非零候选后验证成功publication、
physical-reader拒绝、Current/Scratch/Next/非首块状态、失败恢复和RZ拓扑变化。
不缩小root extent来替代512-cell finding。
完整RZ-BOUNDARY-DISPATCH-01、continuous势力/axis/viscosity/angular A→D、
CUDA/long-run科学出口仍未关闭。
