# R3.1：联合审计首轮与候选排序

2026-10-13，`compute/optim`，源码起点 `34a6ccd5f409ec14ec11c4b798e5795e7592f26f`。[主计划](../../../../ARCH_MODERNIZATION_PLAN.md)是唯一进度入口；[R2收尾](R23PerformanceAndConfiguration-20261012.zh-CN.md)保留原数值、执行物和测量身份。

本文件是R3.1阶段快照；定点归因见[R3.2](R32RklCompilationAndInactiveIdentity-20261013.zh-CN.md)，当前取舍与接续统一见[R3收尾／R4工作包](R33CloseoutAndR4Handoff-20261013.zh-CN.md)，不反写本批次原始机器证据。

**R3.1已完成，整个R3尚未完成，R3.2未启动。** 本轮整理R1／R2证据、核查代表所有者、复现验证入口缺口并形成首批排序；没有修改生产／科学测试源码、原输入或数值门槛，没有编译或演化运行，也未重复RZ长程。用户追加的LFS流量处理已完成本机固定，并形成经过离线检查的CI补丁。

## 首轮核验与决定

| 项目 | 已确认事实 | 当前决定与证据边界 |
| --- | --- | --- |
| Hydro共享核心 | `HydroCellUpdate.h`的散度／阶段更新由Host `TimeIntegratorHelper.h`与CUDA `HydroStateKernels.cuh`／`HydroStageKernels.cuh`调用；CUDA重构策略引用公共PCM／MUSCL／PPM实现 | 保留共享数学和必要执行差异。代表边已核验，不宣称全物理组合数学等价；没有据此归并整个Driver |
| 状态与转发边界 | `driver/runtime`的DriverRuntime、ComputeBackend、StateResidency、RuntimeStateTransaction分别承担生命周期、后端、驻留和事务；`include/GlobalDefs.h`／`UserInterface.h`仍服务当前case边界 | 保留现有职责，依据是当前消费者和生命周期，不是旧接口兼容要求；没有证明所有状态写入均无问题 |
| 在用Hydro输入 | `validation/backend/cases.json`引用12份独立Hydro输入，涉及13个case消费者；均缺`hll_wave_speed`、`min_eint` | “在用”指被manifest引用，不是当前可运行。六份PPM／Sod拒绝复用R2，新增六份PCM／MUSCL由原CPU ELF静态inspection确认退出3、`MISSING_PARAMETER`、未执行Setup；两键属于R2关键配置拒绝契约。没有新的数值PASS |
| 选择case的组检查 | `validate_backend_results.py:1251–1252`对任何`--case`直接置空resolution groups；既有测试覆盖组函数，未保护main选择入口 | 已复现控制流缺口：受控误差通过逐case门槛，原组函数拒绝零收敛阶，mock main仍退出0。没有运行ARCH／比较器或写入科学验收记录。R2另行调用同一组函数且通过，原结果有效 |
| RKL编译成本 | R2同一单TU冷／增量约392.041／390.859秒；Hydro block目标约7.146秒 | 确认目标成本，尚无模板、头文件或优化阶段根因。只放行后续定点归因，不按文件大小拆分，也不改变编译数值选项 |
| O7与身份 | R2两个生产ELF的70个活动关键键缺项均拒绝；25项辅助默认有来源；未活动burn控制的存储值仍参与身份 | 保持O7；未活动身份差异作为维护候选，不改称活动物理静默回退，不另建配置框架 |

调用锚点：[共享更新](../../../../src/numerics/integrator/HydroCellUpdate.h)、[Host消费者](../../../../src/numerics/integrator/TimeIntegratorHelper.h)、[CUDA状态更新](../../../../src/cuda/hydro/kernels/HydroStateKernels.cuh)、[CUDA阶段](../../../../src/cuda/hydro/kernels/HydroStageKernels.cuh)、[Driver所有者](../../../../src/driver/runtime/DriverRuntime.h)、[原runner](../../../../tools/validate_backend_results.py)、[原入口测试](../../../../tests/tooling/validation/test_validate_backend_results.py)。文件职责继续采用[所有权规范](../../ImplementationOwnership.md)及[冻结源码注释规范](../../CommentAndDocumentationStyle.md)，无需新的目录层或规则体系。

## 候选排序与未来落点

编号`R3-A*`仅用于本轮动作，不覆盖历史R1候选编号。优先级表示后续安排，不表示本轮已经实施。

| ID／优先级 | 问题、预期收益 | 唯一写入落点与负责人 | 风险及最小验收 | 放行／停止条件 |
| --- | --- | --- | --- | --- |
| R3-A01／1，阶段4 | 维护12份在用Hydro输入，使既有验证入口可直接复现 | W1：`validation/hydro/inputs/`；只有消费者确需变化才改原manifest／所属说明 | 配置值影响科学身份；从已接受历史配置追溯显式值，inspection覆盖12份，再做各受影响方法最小科学代表，保持原误差、终点和组收敛 | 不能靠生产默认恢复；无法确认原科学值则保留拒绝并记录待定 |
| R3-A02／1，阶段4 | 修复选择case跳过已选完整收敛组，防止逐case通过被误报为组通过 | W2：原`tools/validate_backend_results.py`及`tests/tooling/validation/test_validate_backend_results.py`；复用组函数和证据格式 | 改变CLI验收语义；Codex先冻结完整／部分选择的规则，增加main级负对照、完整组三档正对照和部分选择检查，不降低原组预算 | 完整选组必须执行原门槛；部分选择须明确覆盖不足，不能宣称完整组通过。单例调试与完整验收语义清楚后再实现 |
| R3-A03／2，R3.2归因后决定阶段5 | 确认RKL重TU的真实成本来源，再判断局部构建调整收益 | W2只读原编译命令／依赖与`tests/cuda/numerics/test_diffusion_rkl_parity.cu`；W1核查其共享所有者；实际修改落点待归因决定 | 测量先于重构；先读现有证据，必要时至多两次有界定点profile，重负载串行，异常缓慢受控停止 | 必须得到可测成本归属、预期收益和最小验证；无收益则保留，不触发完整CUDA冷构建 |
| R3-A04／可选，R3.2 | 评估未活动配置的身份语义是否值得维护，避免未来初始化调整造成意外身份变化 | W1：仅调查现有配置／身份／checkpoint所有者；Codex决定是否单列实现包 | 身份／重启风险高；构造最小身份对照，核对受保护契约，区分活动与未活动，不调整科学默认 | 没有实际维护收益则保留；若需改变受保护身份契约，提交具体方案后再按授权边界决定 |
| R3-A05／低，可选阶段5 | 消除重复导航说明、改善冻结源码职责注释 | W1或文档worker：现有API／gravity说明及所属源码注释；按真实消费者归属 | 低风险；仅核验链接、符号、职责和冻结字节边界，不跑全量回归 | 只处理阅读障碍，不强制重命名两个职责不同的BoundaryPlan或拆小文件 |

A01／A02可划定文件后并行实现，由Codex统一集成并选择关联科学检查；使用runner签收组收敛前须先接入A02的保护。A01尚未逐份建立新增六份PCM／MUSCL的历史科学值来源，不能凭邻近文件或模型建议填值；来源与科学身份无法确认时保留拒绝／评估退役，涉及新的科学定义再按保留决策处理。A03／A04调查可独立进行，测量不得抢占数值验收资源。本轮没有解锁这些实现。

## 历史候选完整对照

| R1编号 | 本轮去向 | 未关闭范围／重开条件 |
| --- | --- | --- |
| C01 共享Hydro | 代表边保留；A01／A02先保护其验证入口 | 后续实际共享核心修改才扩大对应数值检查，不凭地图声称全组合等价 |
| C02 RZ source/boundary成本 | 延后，不重跑旧长程 | 保留旧热点与未完成项；新的有界归因包明确目标、终点、停止条件后再开展 |
| C03 Cellular速率差异 | 延后，沿用原精确状态参考报告 | 原失败与源码／报告身份保留，不能用输入修复或mock关闭科学差异 |
| C04 构建吞吐／依赖 | A03优先定点归因 | R2目标成本不是完整工程冷构建；头文件扇出不是可直接删除的依赖 |
| C05 导航／注释 | A05可选，保留合理目录对应 | 文档改善不授予算法、接口或验收改变 |
| C06 可选能力／生成网络 | 按实际消费者触发，暂不扩展 | 原能力身份与条件缺口保留，不为全仓审计强制下载／生成或执行全部网络 |
| C07 生命周期／耦合出口 | 保留已知所有者及原未完成科学出口 | 首批修改触及状态、regrid或restart时才补所需保护；短测／Mock不替代长期轨迹 |
| C08 合理后端层／转发／独立oracle | 保留 | 新的具体成本或重复行为证据才重开；取消兼容要求不意味着必须删除全部适配层 |

`simulation/Cellular/CellularFlash2D.par`仍被README描述为可运行的手工研究比较输入，在自动manifest之外，当前缺11项必需声明（原R2拒绝证据保留）。它不能被整体判为无用历史；后续单独决定明确退役还是修复手工模板，不与原Cellular科学差异混为一项。四个小负载CPU较快也不证明CUDA存在普遍缺陷；大规模profile须有明确用途。

## LFS本机固定与CI补丁

用户收到流量预警后，以下文件从已有共享LFS对象**离线**展开，同时保存普通文件副本到`/home/shiroakane/.local/share/arch-data/`下的同名仓库相对路径；`manifest.json`保存当前OID和大小。副本独立于build／output／LFS prune，表来源与许可保持。

| 固定路径 | 字节数 | 本机核验 |
| --- | --- | --- |
| `EOS_toolkit/tables/helmholtz/helm_table.dat` | 60,242,514 | 普通副本和当前worktree均与Git指针SHA-256一致 |
| `EOS_toolkit/tables/baryon/eos2.tab` | 143,166,842 | 同上 |
| `EOS_toolkit/tables/baryon/eos4.tab` | 143,167,115 | 同上 |
| `studio/tests/fixtures/sod-1d.h5` | 14,336 | 同上；小型测试夹具，不是原始模拟输出 |

本次网络下载为0；副本旁另存现有来源说明、Helm说明及许可共五份小文本。项目本地`filter.lfs.smudge/process`已设为`--skip`，覆盖关联worktree，没有改变全局Git配置。新检出先按所需版本指针复用匹配副本或`git lfs checkout`；缺对象明确报告路径／大小，不能自动全量拉取或用EOS默认替代。命令和适用边界集中在[构建指南](../../../guides/Build.zh-CN.md#离线表与-lfs-流量)，项目入口记在[AGENTS.md](../../../../AGENTS.md)。本地读取已实体化表不消耗LFS流量，账单来源尚无服务端日志确认。

本地[CI补丁](../../../../.github/workflows/ci.yml)将原CPU任务的Helm表改为：禁用初始自动下载→按Git原始指针计算精确缓存键→命中则离线展开，未命中才执行原Helm定点下载→按指针校验大小／内容→在昂贵科学步骤前单独保存缓存。无前缀回退；缓存命中但损坏时明确失败，不自动下载补洞。原科学门槛、其他job和action固定版本保持。

离线通过11项结构检查及4项数据对照（指针拒绝、真实对象展开成功、同长损坏拒绝、缺文件拒绝）。**补丁尚未提交／推送，未运行托管Actions、缓存服务或鉴权下载；本机无actionlint，未宣称该项通过。** 缓存有分支作用域和驱逐，首次未命中仍可能定点下载。关闭smudge本身不是科学静默回退：Helm loader对缺失／非数值表明确抛错；没有证据要求本CPU CI额外下载两张baryon表。

## 接续工作与证据入口

| 下一包 | Codex | W1 | W2 | W3独立复核 |
| --- | --- | --- | --- | --- |
| R3.2 定点归因／契约评估 | 确定A03测量边界、资源／停止条件及A04是否值得做；保持科学标准 | 核对RKL包含／实例化的共享所有者；可选评估未活动身份的最小对照 | 先复用命令、依赖和R2计时；必要时做有界profile，给出修改或保留依据 | 挑战归因与预期收益；检查是否把相关性写成根因、是否引入新层 |
| R3.3 排序收敛 | 综合归因形成最终首批实现包及验收／回退方法 | 冻结必要数学／生命周期边界 | 冻结A01／A02的最小验证与CI位置 | 检查工作包是否足够保护实际改动、未决项是否被误关闭 |
| 阶段4（后续） | 先集成A01／A02保护，再放行风险匹配的重构 | 显式维护当前科学输入，保留原身份 | 维护原runner和针对性的入口回归 | 原门槛正／负对照及覆盖声明一致后签收 |

原始收据与复现配方保存在`output/modernization/r31-20261013/`，不进入Git；[机器索引](../../modernization/R31AuditTriage-20261013.json)记录路径、哈希、数值范围及签收。配方保留原执行路径，复用时先另设新输出目录，不覆盖原收据。主索引只增加这一对人读／机器文件，R1归档及R2原始快照继续保留，不复制新的验收规则。

协作采用真实DeepSeek两项只读源码调查及两项无工具审查，另由Codex子worker唯一修改CI文件，Codex统一验收。首项源码交付因JSON外包文字失败，完整产物和失败元数据保留，仅采用从完整围栏确定解析且独立核对的有限证据，没有付费格式循环。初始任务包误标R1 C02／C03已按原档纠正；“Cellular全无用途”“smudge跳过会静默吞指针”“CPU CI需要额外baryon表”等无依据建议未采纳。一次API schema错误在推理调用前改正，不重置累计预算。不修改全局Skill或MCP。

本轮在R3.1停止。R3.2、阶段4实现及完整性能非回归均尚未执行。

收尾核对通过：12份修改文档的223个本地文件链接、31个标题锚点与32张表有效；本机证据哈希一致，`git diff --check`通过。四项表／夹具副本及工作区内容匹配原指针，CPU／CUDA生产ELF与R2机器快照哈希未变；mock目录没有科学H5或验收输出。
