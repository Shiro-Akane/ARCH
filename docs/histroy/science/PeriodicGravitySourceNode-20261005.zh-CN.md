# 周期重力源抵消修复与整体源误差界（2026-10-05）

## 结论

PERIODIC SOURCE SCOPED PASS；FROZEN JENS PASS；FULL RZ / CUDA PENDING。
GRAV-PERIODIC-CONTRAST-01 的现有 multiply-before-subtract 抵消问题已修复；
source certificate 仍为 companion，未冒充完整物理 RHS 签收或 publication token。

## 实际 finding 与修复

SelfGravity 原先用 factor*rho + (-factor*mean)，随后 project；factor 为
-4*pi*shared CGS G。UniformGravity 则用 factor*(rho-mean)，随后 project。
真实 CPU CompositeExecution 的旧公式在 rho=1e12 的微小密度对比 fixture
（offset 为 ±0.000244140625）产生明显抵消，不是物理 signal 为零。

uniform 16-cell fixture：数学参考最大源 2.1756276941743414e-10 s^-2，
旧路径最大误差 2.9819789464802343e-11（约 13.7% of max signal）；
修复后最大误差 1.5090465391908822e-26。
mixed 28-cell fixture：参考最大源 2.0796441194313557e-10，
旧误差 2.850421051782577e-11；修复后 1.4424709565795198e-26。
不以通过 companion containment 掩盖原数值问题，也未改验收阈值。

原 shared CompositeWork 增加 DifferenceScaleWork：
out_i=(x_i-offset)*scale。SelfGravity periodic 使用该数学 work；
isolated 仍使用 factor*rho。CPU/CUDA 共用同一 scalar descriptor，
没有另造 backend-specific 物理算法；本节点只验证 CPU，不宣布 CUDA PASS。
旧公式只保留在测试诊断 lane；不再是生产周期源入口。

这是同一 -4*pi*G*(rho-mean) 公式的运算顺序修复，没有修改共享 G、物理定义、
EOS、density floor、solver tolerance、projection 或 checkpoint 语义。

## 周期源整体 companion

参考为 -4*mathematical_pi*stored_shared_G*(rho-normalized_stored_weight_mean)。
明确 normalized weights，仅参考数学运算：a=rho_0，
rho_i-mean(rho)=(rho_i-a)-sum(w*(rho-a))/sum(w)。
该精确代数式避免参考自身引入大背景抵消；不替换生产 mean 的实现。

输入真实最终 source，整体捕获生产 mean、subtraction、multiplication 和
final projection 的误差。数学 pi 围住常数表示；区间算术向外舍入，
cell error 与 stored native norm 均给上界，不假定实际 reduction 完全精确。
权重和/系数/理想几何误差仍须独立证明，不能将 stored weights 当理想体积。

本节点不批准对总 rho 按 isolated 公式处理 periodic；两种域显式分离。
nonperiodic、错 extent、负/非有限 rho、非有限 output 拒绝；unrepresentable
finite bound 为 Overflow。恒定正密度为精确 zero-source/zero bound，
没有 artificial budget floor。

极小非恒定 density 的 source 可能在 FP64 舍入为全零；本节点仍给非零误差界，
独立参考 magnitude 约 4.2～4.4e-330，norm bound 1.5e-323。
这是数学 underflow fixture，不能以 zero array 宣称精确 zero physical source，
也不是实际流体 floor/可用输入范围的批准。

## 验证

36 个 lanes：uniform/mixed × 6 density fixtures × 三种路径：
修复后真实 host provider、scalar subtract-first、旧 host formula diagnostic。
fixtures：constant、普通对比、大背景微小对比、巨大有限值、次正规值、
最小次正规密度的 unrepresentable contrast。

独立 Python 用 exact Fraction 计算 normalized stored-weight contrast，
再以 literal high-precision pi 和 exact-from-float shared G 做 Decimal100/140
参考。全部逐 cell source interval/error 及 weighted norm 平方通过。
修复后 provider 与 scalar subtract-first output 逐值完全一致。
旧路径的 containment 是错误大小证据，不是其科学精度通过。

隔离源 28-cell 独立参考与 targeted CTest 四项通过；
architecture audit、diff check 通过。未更改既有科学阈值。

## 新 binary 的冻结 JENS 短包

CPU ARCH SHA256：
a6ce69f59969af7afd0ca35153f0afac0571f8af8a0812b7b81a120cddf52331。

由于生产 binary 发生变化，重新执行完整已冻结 uniform-lifecycle-1：
1D/2D/3D × disabled/output-only/active，9 evolutions + 9 actual checkpoint restarts。
tmax=0.02，checkpoint split=0.01，全部 EOS/密度/域/NJ/regrid/capacity/阈值不变。
9 cases 原生 uniform fields、restart payload 精确一致；off/output state 与
solve counts 一致。质量和能量 drift 为 0；
max JENS relative error=4.50750606346323e-17，仍原 16*epsilon 门槛。

active accepted transaction steps 29/55/83；cells 128/4096/131072。
这仅覆盖已冻结 uniform Cartesian single-caloric-species CPU 生命周期，
不能替代所有 JENS/nonlinear/RZ/CUDA 验收。

## 构建与留存

baseline 674494a674bb553b82c289fc461e180a15581f40；仅 unique worktree、
现有 CPU Release build-cpu，standard incremental parallel 28。
memory guards 无触发、swap growth=0。冻结 campaign guard 47.130秒，
peak owned RSS 817080 KiB、min available 22424980 KiB（工程资源观察，不是 benchmark）。

复现 source check：

    python3 validation/gravity/periodic_gravity_source_reference.py --probe build-cpu/arch_composite_poisson --output <local-summary.json>

冻结短包使用既有 gravity_box.BoxCampaign.jeans_uniform_lifecycle 与
check_jeans_uniform.consolidate；本机 runner、inputs、full logs、
H5/plt/checkpoint 全保存在 studio/.local/integration/periodic-gravity-source-20261005。
处理后 summary 包含 source file fingerprints、前后 ELF identity、
36 lanes 及 9-case scalar summary；原始数组不上传。

## 下一依赖

native geometry/stencil/weight construction、source/tree/AMR身份、可靠
whole original RHS/residual acceptance 仍待贯通。一般父节点 far certificate、
RZ A→B→C→D 与未决 RZ-VISC-01/RZ-AXIS-01 科学参考保持。
对应 CPU 科学 gate 后再统一 CUDA和冻结长跑；production RZ gate 保留。
本节点无新 workspace、main merge、tag 修改或 Windows 适配。
