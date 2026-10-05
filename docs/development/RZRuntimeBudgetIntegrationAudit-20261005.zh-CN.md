# RZ Runtime 预算与完整消费链接线审计

基线 cfb6829d673faa06025422501a41518cf3569ffa。核对科学清单第7节、
原 GravityBoundary / CompositePoisson / SelfGravity / Workspace 的实际实现。
本节点是预算及职责审计，不是 Runtime 实施、科学验收或发布能力签收。

## 已确认的缺口

SelfGravity bind 仍在创建 RZ Workspace 前拒绝；prepare 仍是旧矩和
EvaluateBoundary 路径。typed ring 只在执行器合同接通。
现有真实 native uniform/mixed source→solve 原请求检查使用固定
1e-18 势积分预算；该数学 fixture 值不得直接复制到一般 Runtime。
原 RingBoundaryControl.face_absolute_target 量纲为 cm²/s²，
rtol 所控制的 residual/target 量纲为 s⁻²。

完整 Runtime 需要同一 Workspace 长期拥有原 GravityBoundary 与当前源 generation，
消费已经校验的每块 Current/Scratch/Next view 和 binding.storage 的 gather。
source tree 与 field publication 都须在失败/重绑/regrid/状态更新时失效；
不能重新用第一个 block 代表全域，也不能在输出中另起 solve。

## 原生映射的离线独立证据

新增 rz_boundary_budget_audit.py 读取本机已有真实 solved record，
用独立 Fraction root geometry/Gram 重建各 boundary mapping，
计算 K=norm_native(abs(B)*1)。这是均匀势误差 delta 的映射灵敏度：
residual 中的对应误差不超过 K*delta。K 的单位为 cm^-2。

两 uniform 与两 mixed 记录共四例：
K 约47.42555–49.12483 cm^-2；
以已记录 T_safe 的一半仅分给此势积分项，
诊断候选 delta 约1.17462e-17–1.74346e-17 cm²/s²。
这是候选份额，不是新物理阈值，也不是已配置的生产默认值。

同几何、密度线性缩为1e-6的纯数学诊断下，
固定1e-18的最坏映射贡献相对半份额约57357–85134倍。
这证明固定值无法保证这种尺度下的预算，不证明新物理 run 已失败；
没有实际运行该缩放样本。实际返回 enclosure 可能远小于请求上限，
仍须由完整 ledger 判定，不能单用这个最坏比值宣称失败。

Fraction 记录精确 K²；100位 Decimal sqrt 只是诊断。
Runtime 必须由原 CompositePoisson 的 outward native B/norm 接口提供 K_upper，
不可把本工具 Decimal 数字或一组 mesh 的 K 作为生产常量。
输入 provenance/原记录 SHA 与精简结果见处理后 summary；旧 producer 身份保留，
没有用当前 HEAD 宣称这些旧数组是本次生成的。

## 完整接线策略与保留条件

1. 原 bind 验证原生 RZ mesh/chart/storage 后，内部候选 Workspace 才能建立
   同一 GravityBoundary；公开配置/API/RZ release 门槛仍需完整科学签收。
2. 原 prepare 从真实 block view gather、检查密度，并用当前 topology/time/
   every block version/storage generation 更新原树。共享 CGS G 不变。
3. 初始积分预算可由原 source/RHS norm 和同 owner 的 K_upper 提出，
   只作为内部工作分配，不能把候选 RHS 当未知精确 RHS 来证明通过。
   计算完整当前 boundary/source/construction/assembly ledger 后，
   按原 T_safe 检查并必要时缩紧；原 rtol/atol/工作上限不能静默放宽。
   半份额只分配给一个误差项不足以签收，其他项必须扣除/同账本组合。
4. ring WorkLimit/PrecisionLimit、Unknown/Estimate、一般舍入几何和不匹配
   root-source/observer scope 均保留真实失败，不能从 stored scope 提升资格。
5. 原 solver 可使用更严格的内部代数目标；之后重新计算实际 residual，
   用 assess_native_ring_rhs 的 source/B/assembly/A/evaluation/native RMS
   验证原请求，不能只依赖 solver.report.status。
6. 原 gradient/face/work/cell acceleration 流水线继续唯一 owner；
   所有有限性、租约和 qualification 满足后才允许相应候选发布。
   native discrete residual qualification 不等于连续势/力科学签收，
   更不允许自动解除公开 RZ gate。
7. actual Runtime 的多块、非首块变更、Current/Scratch/Next、refine/coarsen、
   no-change、失败后的旧场退役及恢复必须实测。独立 continuous Phi/force、
   axis/viscosity/angular A→D、CPU→CUDA 出口分开验收。

下一步是原 Workspace/SelfGravity 的候选完整流水线接线和资格表达，
而不是继续添加一个抽象单块 ring fixture，也不是直接移除 bind gate。
若资格表达必须改变公开能力，先交维护者确认，不能借“内部候选”绕过审批。

## 本节点验证与范围

离线独立工具四例完成，架构审计与 diff check PASS。
未修改 Core、重编 ARCH、运行 simulation/Preview、新建 build/worktree、
生成 H5/checkpoint、开展 CUDA ring/长跑/benchmark 或 Windows 适配。
原始 record/数组继续在 studio/.local；提交脚本、处理后指标和审计。
本工具仍需由实际 outward Runtime policy 与真实流水线测试补齐，
不能把此节点当作完整科学 gate 或新阈值的批准。

[处理后摘要](../../validation/gravity/results/rz-runtime-budget-audit-20261005/summary.json)
