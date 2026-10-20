# RZ-EXT-TORQUE-GATE-01：外部力矩实际消费者的 preflight 门槛

## 当前计划位置与结论

处于联合计划 O7.2–O7.5 / RZ-B 外源力矩消费，依据科学清单第8.2/8.4节。
状态 NOT_CLEARED；首个真实 Euler 场景在第一 stage 之前拒绝，
不是外力矩预算超标，也不是24场景通过。生产RZ/self-gravity/CUDA/long-run门槛保持。

基线 3d5b98c874cc98ee412bc2efce7398d18e1acadb，唯一原工作区。
仅修改 tests/host/grid/test_curvilinear_metrics.cpp，未改科学Core、API/schema或生产gate。
生产CPU32f7b139... / CUDA1cbbd695... SHA保持，不重复匹配的JENS9+9或全科学baseline。

## 实際失败证据

复用既有五叶 mixed-AMR、BC/ghost/StateResidencyLedger、actual HLLC/PCM和RK owner。
候选组为 radial/axial粗细界面×闭/开放边界×signed native phi acceleration×Euler/RK2/RK3，
共24组；第一组 direction=0、r=[1,3]、z=[-1,1]、closed、phi acceleration=-0.025。
离轴避免把常值方位加速度当轴正则源。dt1e-4、计划10步沿用既有短fixture；
这是内部消费者检查输入，不是新长跑或benchmark冻结包。

实际接口 ExternalGravity(0,0,-0.025)，真实 SolverEuler::solve 接收非空policy后拒绝：
RZ gravity requires authoritative finite-ring contract
新增独立诊断入口 rz-applied-torque-audit 返回2，打印 NOT_CLEARED。
observer stage_calls=0；preflight前后Current（含ghost）流体/两组分共26880 FP64 words位级不变。
只核对Current，不将其扩大为Next/Scratch/完整ledger全部测量。
正常工程测试仍2/2 PASS（curvilinear_metrics / amr_flux_surface_plan）；架构和diff检查PASS。
首轮未捕获异常日志保留；最终诊断不abort，也不吞异常假装科学PASS。

## 规则、失败原因与职责映射

规则位置：src/numerics/integrator/HydroGeometryBinding.h 的 if(rz && gravity)；
规则现将所有非空 IGravityPolicy 等同于需要有限环体self-gravity contract。
TimeIntegratorEuler首先调用bind_hydro_geometry，再Clear register、建立阶段及evaluate/update。
因此观察器中的source预算尚未执行，cannot infer momentum/work公式是否科学正确。

ExternalGravity.h / GravitySource.h已有constant native orthonormal cell acceleration/work；
这一消费者本身不求解Poisson或finite ring。当前IGravityPolicy没有显式source-kind/chart contract。
Host integrator binding应消费policy提供的权威能力身份；不应靠浏览器传参、
case名字、dynamic_cast具体model白名单或删除整个RZ gravity guard来推断。

## 待Core review的最小候选，不在本节点应用

1. 由原 IGravityPolicy 明确区分 unknown、external native-orthonormal cell source、
   finite-ring/self-gravity producer，附显式geometry/state语义；Unknown默认拒绝。
2. ExternalGravity原owner仅声明其已确认的内部RZ source contract；
   SelfGravity仍要求authoritative finite-ring/current density/field身份，不降低原门槛。
3. HydroGeometryBinding消费typed能力；public RZ startup/API生产门槛继续保持，
   不借外源验证发布完整RZ支持。设备资源/同源数学在CPU签收后再处理。
4. 确认RZ代表状态的angular impulse和cell energy work范围后执行本候选组；
   source observer只读真实stage，以独立端点V/W积分记账，更新仍委托原owner。

候选账本：
abs(deltaJ+outwardImpulse-appliedImpulse)，原1e-12归一化；
mass/E/rhoX预算、repair=0分别核对。计划增加omit-applied与wrong-RK-weight负例，
不得只计最后一步或改dt/门槛寻找PASS。
这些预算代码已编译，但由于gate未放行尚无其实际stage数值结果，不能作为独立签收证据。

## 复现 / review资料

    cmake --build build-cpu --target arch_curvilinear_metrics --parallel 28
    OMP_NUM_THREADS=1 build-cpu/arch_curvilinear_metrics rz-applied-torque-audit

当前预期exit2 / NOT_CLEARED。此诊断不加入默认CTest科学PASS集合。
处理摘要：validation/amr/results/rz-applied-torque-gate-20261005/summary.json。
保留原始build/失败日志在 studio/.local/integration/rz-applied-torque-20261005*，
独立actual exit复核在 rz-applied-torque-direct-exit-20261005；不提交raw/ELF/数组。
仅增量测试编译，memory guard未停止，swap0；不作为平台benchmark。

下一步：Core确认external native source的typed/chart contract是否可独立于self-gravity
在内部RZ Hydro lane接入；确认后才修改binding并运行24组及相关负例。
RZ-VISC-01、RZ-AXIS-01、continuous force参考、JENS public CUDA授权和冻结长包分别仍开放；
本finding不能替代或关闭它们，不进入Windows工作。
