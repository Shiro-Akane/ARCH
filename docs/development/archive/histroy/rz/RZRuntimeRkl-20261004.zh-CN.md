# RZ 实际 Driver RKL 接线

基线4593e8efe17625efdd2c5e06697af2230cba1ae4。

## 实现

单块RKL1/2和AMR RKL1/2接受内部GeometrySemantics，默认Existing。
真实Driver使用Runtime固定profile。single operator、composite face flux、
divergence、geometric sources、flux registration、每stage reflux和halo同步
均消费同一RZ profile。入口preflight验证所有Grid和BC profile，先于slot复制/
计算/发布。RKL系数、表达式分组、slot rotation、scheduler顺序与权重不修改。
NoDiffusion适配签名保持可接受统一调用。

## 真实证据

扩展实际DriverRuntime fixture，经DriverStages::advance_diffusion调用真实
dispatch和共享scheduler，单块/五叶mixed-AMR、轴/非轴、径向/轴向接口各两种
RKL，16组。每组RKL1两stages、RKL2五stages，确实覆盖recurrence。
输入rho2、axial momentum6、energy21.5，radial/phi momentum0，两组分.6/.4，
constant viscosity.01。零viscous operator应保持常态；最大state error
1.4210854715202004e-14，沿用2e-12工程算术门槛，不新增科学预算。
每个真实stage均推进ledger，final Current版本=before+stages、Host interior/
ghost同版本可读；混合网格flux topology报告RZ。Runtime halo日志version1指
初始halo witness，不是RKL后的最终版本。

fixture重新编译相关Runtime/fixture translation units并链接已有CPU依赖archive，
不冒称完整production ARCH binary rebuild。Driver time/step仍0；实际执行了RKL
阶段kernel，没有运行simulation driver循环，也没有生成科学H5/plt/checkpoint。
第一次编译暴露AMR RKL参数遗漏，已修复，原始error log本地保留。
16组实际fixture通过；受影响curvilinear/scheduler/AMR operation/flux surface
scoped4/4 PASS。原始日志/ELF在ignored studio/.local/integration/
rz-runtime-rkl-20261004-repair，失败编译日志在rz-runtime-rkl-20261004。
源码/header/fixture身份与逐案例结果见同名Summary.json。

## 限制

这16组是常态零算子和实际调度接线证据，不代替非零diffusion演化、独立参考、
收敛与科学误差预算。原有非零RZ空间operator制造解继续仅覆盖空间算子。
公共RZ配置、IO/checkpoint语义、AMR角动量迁移finding、finite-ring gravity、
CUDA及冻结O9演化/benchmark仍未完成。下一步补非零阶段证据和生产语义迁移。
