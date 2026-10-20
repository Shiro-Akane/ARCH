# RZ 正孤立 RHS 初始预算节点（2026-10-05）

基线：9976d722d5b881bfbf85c5e355d6fb4993cd4e35。Linux/WSL 唯一工作区；14700K + RTX 4070 Ti。
结论：内部 native service PASS，公开科学能力门槛保持。

## 数学改动与职责

改动位于原 GravityBoundary 数学所有者，复用原 CompositePoisson/native measure。
对于严格证明的 root-exact geometry、正密度、bounded native measure 和非负 Dirichlet B：
质量下界 M_l=down(sum rho*V.lower)，完整环体所有源/观察点的距离上界
D_u=up(sqrt((2*r_outer)^2+(z_high-z_low)^2))，从原 Newton 积分得
-Phi >= down(G*M_l/D_u)。source 与 B*Phi 同号，逐 cell 下界为
down(sourceMagnitudeLower + potentialLower*sum B.lower)，以 native RMS 得 RHS norm 下界。

使用原 rtol/atol 生成 T_initial，再以原 K_upper 分配 half 初始预算。
无新 floor、G、验收阈值、源替代或工作上限；不是点质量近似。
不满足严格几何/measure/B-sign 条件时显式回退 SourceScale。
Proposal 不能授予最终 Accepted；最终完整原请求误差 ledger 仍必须通过。

## 已完成证据

- uniform/mixed 轴接触及离轴四例 88 cells：原请求 solve 和独立 Fraction residual/reference PASS；
  20.483/25.417 秒。独立重建 B、质量/距离、RHS 下界及原预算不等式 PASS。
- heterogeneous mixed 两例 56 cells：独立预算证明 PASS；仅预算，没有 solve/evolution。
- 与上节点失败相同的真实两块 512-cell service：rho=1、r=[0,1]、z=[-.5,.5]，
  原 rtol=1e-10、atol=0、65536 boxes/leaf、100000 work。
  gather -> current source -> finite ring -> RHS -> solve -> force/work -> candidate publication PASS。
  原 residual upper=4.443476783658658e-15 <= tolerance safe=1.4684682266979847e-14。
  113.252 秒，peak owned RSS=20068 KiB，swap=0，无 guard 中断。
  两个输入身份及 source generation=1；普通物理 reader 拒绝 candidate；
  非首输入 stale 被拒绝；time=0、steps=0。
- CPU composite/lifecycle 2/2 PASS；实际 Cartesian Runtime 4->8->4、七次 gather PASS。
- CUDA-enabled build 的 Host lifecycle PASS（12.94秒）；不等同 Device RZ 数学测试。
- architecture audit / git diff --check PASS。

RZ-NATIVE-SERVICE-BUDGET-01 在上述原 512-cell 输入的初始预算范围关闭。
前次 WorkLimit 原 producer/日志保留，不覆盖；零密度原拒绝不改变。

## 尚未签收

512-cell 依原 outward ledger 验证，未另做完整数组独立参考。
连续 Phi/force、一般 rounded geometry、完整 RZ DriverRuntime regrid/all slots/recovery、
Hydro 耦合及能量/角动量科学守恒、axis/viscosity、Device RZ 和长跑仍未签收。
candidate scope 不可作为普通 physical field；公开配置/API/生产门槛保持。
生产 CPU/CUDA ARCH ELF 未重建，不能当成当前源码 Build Manifest fresh。
JENS CUDA 冻结 9+9 的生产门槛候选仍待明确批准，未借本节点绕过。

## 交付与后续

处理后摘要：validation/gravity/results/rz-positive-rhs-budget-20261005/summary.json。
原始数组、日志和测试 ELF 留 studio/.local/integration/rz-positive-rhs-budget-20261005
及同前缀本机文件；只提交源码、脚本、标量摘要及本报告。
下一步继续内部 RZ 消费者/独立连续参考核验；未签收子集不启动长跑，不更改原阈值。
