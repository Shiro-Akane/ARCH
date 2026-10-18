# RZ native 三状态缓冲区与失败恢复节点（2026-10-05）

基线：2e05e3e544934d8249861f1e20560d19f03867eb。
结论：内部服务真实缓冲区生命周期 PASS；不是完整 DriverRuntime / 科学签收。

## 改动

只扩展原 tests/host/gravity/test_self_gravity.cpp 的显式 opt-in 验证，
入口 build-cpu/arch_self_gravity native-rz-service-lifecycle。
生产 Core 实现、配置/API gate、物理、rtol/atol 和 work caps 未修改。

继续原真实 AMRControl 两块各16×16、512cells、完整 r=[0,1]/z=[-.5,.5]；
初次 Current -> Scratch -> Next -> Current，后面三次使用实际 Block 的
fluid_state/state_scratch/state_next 独立分配。
每次将非活动缓冲区 rho 置 NaN，选中缓冲区 rho=1、E=10；
验证 gather 不会默读 Current，未以小网格替代512cell输入。

每个后续请求先注入非首块 slot 与 view 身份不一致：
原服务在委托 gather/ring/solve 工作前拒绝，旧 assessment/field 不可读；
随后原合法请求完整重算并恢复，source generation=2、3、4。
普通 potential 与非首块 Hydro patch reader 始终拒绝候选 field。

## 终止证据

独立运行 exit=0，三条 RZ_NATIVE_SLOT_PASS 和
RZ_NATIVE_SERVICE_LIFECYCLE_PASS 已完整记录。
三个后续解及初次解原 residual upper=4.443476783658658e-15，
safe=1.4684682266979847e-14；均为原 conditional Accepted，
physical status 仍 UncertifiedInput。
465.035秒，peak owned RSS=20380KiB，swap=0，memory guard未中断。
time=0、steps=0，没有 timestep 或科学输出。

CPU 原 self_gravity_lifecycle PASS（13.47秒）；
architecture audit 与 git diff --check PASS。
仅测试代码变化，未重复不受影响的前次 CUDA / Studio 验收。

## 证据边界与后续

这里使用真实 native buffers，但版本/storage 身份由 fixture 显式给定；
没有验证 DriverRuntime residency ledger 发租约、真实 RZ regrid 或事务。
各 slot 使用同一正密度，不代表不同空间源或演化误差验证。
连续势/力、一般坐标、Hydro能量/角动量、axis/viscosity、Device RZ、
完整 A→D 科学出口与长跑仍待完成，公开 RZ 生产门槛不变。
下一步需要实际 Runtime 租约/拓扑消费者与独立连续参考，不能将本项升级为全链签收。

处理后标量 summary 在 validation/gravity/results/rz-native-slot-lifecycle-20261005。
producer identity、完整日志及测试 ELF 在 studio/.local/integration 同名目录/前缀，
全部留本机。生产 ARCH ELF 未重建，不声称当前 Build Manifest fresh。
