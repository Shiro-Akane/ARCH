# RZ external stage patch producer：私有候选节点

## 身份与范围

Core contract 固定为 CORE-RZ-20261006-v1 / 4639774fe3c94ae27b3e831d0f0b9d6340de34fd。
候选 source snapshot 基于 326626cb；公开源码、公开 RZ/CUDA JENS 门槛、原数学定义和验收阈值不变。
本节点为真实 FluidState patch producer 和真实调度器 preparation 拒绝路径；不是完整 RK 演化或续算签收。

两个前置测试缺口已在 a6df1f5450b192fdfee4df84aed437e357d9520c 修复并推送。
close 显式拒绝 actual/expected NaN 与正负 Inf；不同非零 radial/axial rate 验证 V 分母及方向。
原测试的 NaN、radial/W 假 PASS 已重现；修订后 13 个实际编译执行的变异反例均拒绝。

## 候选实现

累计三文件 inert patch：validation/gravity/candidates/rz-external-patch-producer-20261006.patch。
SHA256：595781d45fb26c9270845861b4fe489796811f1779bf69cb8295586742832a0f。
仅私有 snapshot 应用，未应用到公开 Core。git apply --check 在当前公开 source tree 通过。

ExternalGravity 显式携带 chart 和 typed source origin。RZ 不再允许无 frame 的旧 add_sources_on_patch 入口。
producer 校验 source、chart、block/topology epoch、slot/version、storage/config generation、macro step/stage、
acceleration 与实际 state 指针，并调用真实 StateResidencyLedger require_readable。
geometry 和有限斜率复用现有 GridMetrics 与 limiter，不复制几何公式或自建 RK tableau。
径向 affine reconstruction 使用 V/W 各自中心：torque 使用 rho_W，work 使用 m_phi_V。
radial/axial/energy 除 V，angular momentum 除 W。非有限值、非法测度及跨轴/不合适的常量源均拒绝。
完整 patch increments 先在私有 vector 生成，返回前不写 FluidState 或 publication ledger。

当前 candidate 针对 off-axis、常量 external native acceleration；不是 finite-ring 或任意空间变化 source 实现。

## 实测证据

validation/core_contracts/rz/test_candidate_patch_producer.cpp：
- 真实 16×16 patch / 256 个单元。
- Current、Next、Scratch 各自真实数组和实际 ledger publication；不同工作槽的场值不同。
- 独立 long-double polynomial 积分检验恒密度与 rho(r)=2+r 的 source totals。
- 13 个身份/非有限迟发值/stale ghost 反例被拒绝。
- 所有拒绝保持 Current/Next/Scratch 每个数组 bitwise 不变、ledger 字段不变。
- 独立积分比较使用原 J 1e-12 级相对界限，仅为该 arithmetic fixture，不代表 EOS/演化新阈值。

三个实际编译执行的 mutation 均失败：
1. W momentum 直接用于 V work；
2. V density 直接用于 W torque；
3. 移除真实 ledger readable 检查。
编译失败不计入 rejection。ELF/test/patch SHA 与结果见 results/rz-patch-producer-20261006/summary.json。

validation/core_contracts/rz/test_candidate_scheduler_preflight.cpp：
- 使用现有 StageScheduler execute_hydro_plan / HydroStagePreparation。
- Euler、RK2、RK3 三种 plan，各含两个真实 patch；第一块完成私有积分，第二块迟发 NaN。
- preparation failure 发生于 next_publication / executor 前。
- 六个实际 state（两块的 Current/Next/Scratch）、ledger、clock 均保持不变。
- executor/boundary/acceptance/rotation/reflux 回调次数均为零。
- 测试 adapter 提供 storage/config frame，尚非 production owner；不声称运行了 RK numerical executor。
结果见 scheduler-preflight-summary.json。

## 重现

在私有 snapshot 应用累计 patch 后，分别用 g++ -std=c++20 -O2，
-I<snapshot>/src -I<snapshot>/include 编译上述两个 test 文件并执行。
三个 producer mutation 的 include overlays、ELF、build/test logs 留在本机：
studio/.local/integration/rz-source-candidate-20261006/producer-mutations/。
scalar mutation 的可执行重现工具是 run_stage_integral_mutations.py；producer mutation 工具是 run_patch_producer_mutations.py，已实际重现 correct PASS / 三个 compiled mutants rejected。
原始 arrays / ELF / 全量日志不提交。

## 下一步及尚未关闭项

1. 从 production backend 的 storage_generation、真实配置 owner 和 scheduler descriptor 构造可信 stage identity；
   不使用 pointer 作为 generation，不用 fixture 常量冒充 owner。
2. 按真实 Hydro reconstruction policy 选择 producer limiter；所有 block preflight 后才消费缓存 increments。
3. 绑定实际 Euler/RK2/RK3 numerical executor、BC、flux/reflux/source budget 与拒绝保护。
4. 执行冻结 24 项演化与真实 checkpoint restart，对照保持 Core 原输入/阈值。
5. finite-ring source、对称粘性/能量功、轴邻格和连续面力参考仍分别保留 finding。

本节点没有执行 simulation 长跑、CUDA RZ、性能计时或改变公开能力。
2D diagnostics 仍等待实际 physical-core/type mapping 及 owned-resource/write-headroom guards。
用户报告 O8 与集成线 41 冲突文件保留为后续模块整合输入，未互相 merge 分支。
