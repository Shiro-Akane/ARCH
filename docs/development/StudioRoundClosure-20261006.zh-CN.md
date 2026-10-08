# Studio / Compute：当前轮次收尾清单

## 文档角色与当前入口

2026-10-06移交时的有限任务与证据；文内两侧分工和当时未接线状态保留原身份。全部Core工作现由维护者接手，已验收项按当前总表复用。
当前执行顺序与已关闭项统一见[O总表](ComputeOptimizationPlan.zh-CN.md#当前执行校准2026-10-08)，当前证据见[release验收记录](ComputeStudioReleasePlan-20261006.zh-CN.md#当前验收出口2026-10-08)。

日期：2026-10-06。工作线：`studio/compute-optim-integration`。
审阅节点：`1f743efd7cf7d0793a766f3c2cd4fdca1bc9cadd`。
Core 契约：`CORE-RZ-20261006-v1` / `4639774fe3c94ae27b3e831d0f0b9d6340de34fd`。

本文件收束现有已授权工作。定义、输入和科学阈值沿用[Core RZ 契约](CoreRZDecisions-20261006.zh-CN.md)，整合路径沿用[分支归并说明](ComputeOptimConsolidation-20261006.zh-CN.md)。

## 已复核内容

| 节点 | 本轮确认 | 尚不能据此宣称 |
| --- | --- | --- |
| `a6df1f54` 标量测试修复 | NaN/Inf 显式拒绝；radial/axial 非零且不同；13 个实际编译的错误变体均拒绝，两个旧假 PASS 已重现 | 真实 RK 或完整 RZ 科学通过 |
| `1f743efd` patch producer | 真实 256 单元、Current/Next/Scratch 三槽、变密度积分、13 个拒绝路径；3 个实际 producer 错误变体均拒绝 | 已与生产 identity owner / numerical executor 绑定 |
| 同节点 scheduler preflight | Euler/RK2/RK3 plan 的第二块迟发失败时，六个 state 数组、ledger、clock 保持不变，executor 等回调为零 | 三种 integrator 已完成实际数值演化 |
| Core 独立参考 | 10 个 Fraction/多项式解析检查通过 | EOS、Poisson、AMR 或整程演化签收 |
| 私有 CUDA JENS | 既有提交报告记录 scoped tests 和原冻结短演化/真实续算通过 | 本次维护者重新运行 GPU，或公开能力已解除门槛 |

维护者在隔离目录独立编译并运行了上述 CPU fixture 和 mutation 工具。没有编译 GPU、运行完整 RK 演化或应用私有 patch 到生产源码。私有 patch 身份为 `595781d45fb26c9270845861b4fe489796811f1779bf69cb8295586742832a0f`。

## 接下来的有限任务

| 顺序 | 合作者执行 | 收尾证据 |
| --- | --- | --- |
| 1. 真实阶段身份 | 从实际 backend storage generation、配置 owner 和 scheduler descriptor 构造 identity，替换 fixture adapter；真实 state slot/version、ghost 可读性及 epoch 同步 | 正常来源及错配/过期反例；指针不是 generation，fixture 常量不是生产身份 |
| 2. 重构与缓存接线 | producer 使用实际 Hydro reconstruction policy 的 limiter；全域所有 block preflight 成功后，实际 stage 消费已准备的 increments | 第一个 block 已准备、后续 block 失败的拒绝保护；prepared 数据消费一次，并对应同一 stage |
| 3. 实际 RK 演化 | 贯通 Euler/RK2/RK3 numerical executor、BC、flux/reflux 和源账本；复用既有 integrator/tableau | 各阶段源、状态和真实 RK 权重对应；Current/Next/Scratch、ledger 和 clock 的拒绝保护，不只证明 preparation |
| 4. 原冻结短包 | 执行原 24 组合的离轴域、`g_phi=±0.025`、`dt=1e-4`、10 步演化及真实 checkpoint restart | 输入/源码/ELF 身份；实际分割和最终时间；质量、组分、repair、normalized-J 原 `1e-12` 预算及源/开放边界能量账本 |
| 5. finding 与最终报告 | 分别记录 finite-ring、对称粘性/能量功、轴邻格、连续面力参考的已有实施/验证结果、失败或依赖 | 每项列 COMPLETE / FAILED / NOT_RUN / BLOCKED 及证据；外源子集通过不替代四项整体签收，缺参考的项继续保留 |

这里的 24 组合来自已经冻结的外源短包，不新增长轨迹、科学阈值或修改输入来获得 PASS。模型白名单、硬编码旋流状态、补 heating 或用最终状态回填阶段功不属于本轮实现路线。

若某一项依赖仍缺失或候选失败，停该节点、保留具体反例并交付状态表。可以建立真实的阶段性 checkpoint，但必须写明哪些项目未完成，不能将其称为完整 RZ 科学签收。不要为等待同一前置条件持续扩大任务。

## 2D 诊断：沿原条件执行

有实际 physical-core/type 映射和资源 guard 后，按 Core 第 7 节执行一次 2D CPU 完整终点试跑及一次真实续算；通过后再执行对应 CUDA 两条。CPU/CUDA 分组串行、各组总墙钟不超过 2400 s；原终点 `1e-7 s`、分割 `5e-8 s`、单条 1200 s、进程树 RAM 12 GiB、owned GPU allocations 10 GiB、产物合计 10 GiB 和写入余量规则不变。

guard 或核心映射仍不能落实时，该项以 NOT_RUN / BLOCKED 收尾并明确缺口。不启动无保护试跑，不改变物理终点或网格凑结果。3D、正式重复计时及 `1e-6 s` 长包继续按原契约等待预算/科学参考，不追加到本轮收尾。

## 两侧分工与交付

合作者负责现有私有候选生产接线、受影响 scoped tests、冻结演化/真实续算、运行工具及最终状态报告。维持当前工作分支及公开门槛，按小提交 push；raw HDF5、checkpoint、ELF、全量日志保留本机，只提交摘要、必要图表与复现入口。

Core 维护者已提供冻结定义、独立参考及统一 `compute/optim` 汇总线，并完成当前 CPU 节点的独立复核。后续负责科学证据 review、O8/Studio 模块冲突整合，以及最终 main 审计，不同时改写合作者的候选 owner。

最终交付给出：准确 commit/ref、clean 工作树、各项实际执行结果及未完成原因、源码/输入/执行物身份、复现命令、公开能力变化表。JENS 与每项 RZ finding 独立结算；没有相应真实验收时公开 gate 保持。Linux/WSL 与已有 Studio/Host/Plotfile 工作按原范围交付，本轮不扩展 Windows、SSH/cluster 或新的数值方法。

先 fetch 本次 `compute/optim` 的说明和契约，继续在现有 Studio 基线上收尾。两条源码线暂不直接互相 merge；收到冻结交付后，由 Core 维护者统一将正向改动归入 `compute/optim`。
