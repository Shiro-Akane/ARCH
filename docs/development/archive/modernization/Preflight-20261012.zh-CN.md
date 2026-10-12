# 现代化前置整理（2026-10-12）

> 已完成阶段的历史快照；状态和下一工作包以[现代化主计划](../../../../ARCH_MODERNIZATION_PLAN.md)为准。归档仅调整阅读位置与相对链接，原调查／验收身份保留。

本轮只恢复工作入口、核对旧进度、集成已完成的 Studio review 并清理已合并分支。正式路线与后续工作包以[现代化计划](../../../../ARCH_MODERNIZATION_PLAN.md)为入口，阶段 1–8 尚未开始。原始讨论附件现已完整读到，路径为 `/mnt/c/Users/26428/.codex/attachments/e9f9cda2-2c55-4206-aa77-61785c917a6b/已粘贴的文本.txt`。

## 入口与环境

| 项目 | 本轮核实与处理 |
|---|---|
| 当前工作区 | `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，已绑定 `compute/optim` |
| 前置基线 | `9aaa5addde2cda05bf88b209ce46d1c82b33df93` |
| 路径读取 | WSL `/bin/bash` 与 `/mnt/c/Users/26428/...` 已实读成功；普通沙箱启动错误与 Windows Node 的 Linux cwd 兼容问题尚未根治，可用工具参数见 [AGENTS.md](../../../../AGENTS.md) |
| Python | 默认输入 `work`，即已有 `conda activate work` 别名；Python 3.12.14、NumPy、h5py 已验证，缺包可在该环境安装 |
| 进程工具例外 | 当前 `work` 缺两个 pidfd 标准库接口；仅资源护栏及依赖这些接口的工具沿用已验证的 `/usr/bin/python3`，科学 Python 默认仍为 `work` |
| Studio | 已有 Linux Node 24.21.0 / npm 11.19.0；应先把用户级 Node 目录前置到 PATH；当前工作区按锁文件独立安装依赖 |
| Git LFS | 首轮失败发现 Sod 夹具仍为 130 字节指针；从本地对象库恢复为 14336 字节真实 HDF5 后重测通过；未修改测试断言 |
| 原有文件 | 两个目录已有的项目 Skill／入口均保留；当前工作区入口与既有项目 Skill 纳入版本跟踪，全局 Skill／MCP 未修改 |

本次只做了环境可用性与记录核对，不把新建工作区中的构建缓存或旧执行物当作当前新回归身份。自动化加载 `work` 的方式及路径异常恢复顺序留在 AGENTS.md，避免在本页重复维护命令。

## 旧进度核对

依据基线中的 [O 总表](../../ComputeOptimizationPlan.zh-CN.md#发布进度与剩余出口2026-10-09复核)、[release 当前出口](../../ComputeStudioReleasePlan-20261006.zh-CN.md#当前验收出口)及各自处理后 receipt；本轮未重跑 CPU/CUDA 科学测试。历史复选框、移交分工和约 75% 的旧工程粗估不构成新的任务清单。

| 范围 | 可复用的已验事实 | 缺口／本轮处理 |
|---|---|---|
| CPU 与工具 | CPU 68/68、Tooling 749/749、LowDensity 72/72，均零跳过；公共 Host Self／四模块／AMR／production checkpoint 四个 owner 已过，混层 fresh 的 30720 存储字一致 | 保持原源码、执行物和输入身份；最终 CPU `a1c76c6f…` 的增量构建与单配置 owner 通过，不代签全部 68 项的新身份 |
| RZ 有限时段 | 原保存状态的 1.25／2.5 次声穿越满足原 2% 能量、0.2% 参考、1e-12 质量及信号预算；step534 修复全零；2.5 次的能量残差含估计为 0.1423%，参考不确定性 0.0491% | 原 5 次声穿越终点及对应长程严格续算仍按用户授权后置；重复 primary 约 80 分钟后 SIGTERM、退出 241，自身 fresh 未启动；不记完整 PASS |
| CUDA 原消费者 | 冻结 176 项源码 `e2bc6390…` 的原 9 项中 8 项通过，独立 registry 与既有 dispatch 证据保留；Burn 完整同层／混层／rollback 通过 | 完整 Hydro 在约 125 分钟后受控停止，包装器 143／外层 1，未签收；未记录新数值断言失败不能等同通过。后续只补这个缺项，身份变化时才重开受影响已验项 |
| CUDA 生产路径 | 新生产 ELF `0828b3c8…` 构建、链接和只读配置身份检查通过 | 公共 Device SNIaCoupled 四模块三宏步及 checkpoint→fresh 新进程 restart 未执行；需在后续合适阶段完成，不能由构建或服务叶检查替代 |
| 其余 O9 | Cartesian main 3997 步及严格续算 21 数据集一致；半 CFL 7988 步及 15976 次导热活动通过；预先冻结的 L2 补充 811 步、3 次实际 post-init AMR 变化通过 | main 原 1<2 AMR 事件 FAIL 保留；L2 补充与 RZ 分开签收，不重开已验 Cartesian 长跑 |
| Studio 与 CI | 基线完整 374/374；已签原生 Configure／Build／Preview／AMR、Sod Run／Restart／Plot、source 绑定／409、Retry 及 RZ 初始网格；本轮合并后的新结果见下节 | 本轮不重跑原生科学运行；新的完整远端 CI 结果尚未签收，不能把本地检查或历史 browser UAT 代签当前原生 GUI 验收 |
| 正式成本 | 旧 driver 1.22485 倍仅属历史身份；本轮定位到已有归因记录 | 本机公平端到端 CPU/CUDA 计时未执行；4070Ti 配对为 0 组，只作可选外部证据，不因外部机器不可取得阻塞本机验收 |

RZ 成本的已测归因：16×16 均匀网格自然 `dt≈398.183 s`；重复 primary 最后 90 次场调用中 source/boundary 占记录引力阶段约 99.985%，中位 7.804 s，Poisson 中位 0.000902 s。OMP8 生效而热点主要串行；该记录含准备成本、未定位具体缓存失效，也不是公平 benchmark。后续可优先检查共享环积分执行、精确缓存与调度的真实调用成本，本轮不确定具体优化方案、不改数学或门槛。

原始定义与完整身份继续引用[公共消费者摘要](../../../../validation/backend/results/release-closure-20261009/native-public-consumer-summary.json)、[O9 账本摘要](../../../../validation/backend/results/release-closure-20261009/o9-endpoint-ledger-summary.json)、[Studio 工程摘要](../../../../validation/backend/results/release-closure-20261009/studio-engineering-summary.json)。R1–R6 尚未全部关闭，完整 release 仍未签收。

## Studio 集成与分支清理

来源为 `studio/ui-video-review-20261010` 的 `594ca5c16695aaae38d57cfe291d1bf63eaf7cf8`，共同祖先为 `e69a859705da0efc0c7abad64c8949f9e4934951`。采用保留双亲的合并；当前集成提交：`c54e8cc64fcf6c76bd7e3aad058dad0e411fca85`（Codex 独立检查与完整 Studio 验收通过）。

合并涉及 19 个 Studio 文件；只有 ConfigurationBridge.tsx 与 ConfigPanel.tsx 需要解冲突。保留 compute 的 selected-source pending/fixed 权威、显式 Retry／作用域守卫、由 Core schema 提供的 retiredKeys 及未分类参数提示，同时接入 GUI 的流程布局、统一 Case 列表、物理单位和折叠帮助。没有修改科学核心、数学、物理输入、数值门槛或 CI 代码。

| 合并后检查 | 实际结果 |
|---|---|
| 锁文件依赖安装 | `npm ci --no-audit --no-fund` 成功 |
| 完整 Studio 测试 | 385/385，fail／skip／cancel 均 0，约 50.73 秒；按文件串行；Host 子集已包含，不重复计数 |
| lint | 通过，零警告 |
| 类型检查与生产资源构建 | `npm run build` 通过；保留大 bundle 的既有提示，不在前置阶段引入拆包工程 |
| 合并范围与格式 | 19 个 Studio 文件；冲突已清；`git diff --check` 通过 |
| 当前原生桌面／人类交互 | 本轮未重新执行；来源 review 的历史 browser UAT 与既有原生证据按各自身份保留 |

远端清理须在集成推送成功、祖先关系复核后执行，并用原 SHA lease 防止误删他人新提交。清理前保存的旧引用如下；**已执行并复核**：远端删除使用原 SHA lease 与 atomic；集成节点已先推送至 compute。

| 引用 | 原提交 | 清理状态／理由 |
|---|---|---|
| 远端 `codex/o8-boundaries` | `11a321d5604f9ee62b9f9587c81f14de4f128bc4` | 已在 compute 基线中，无独有提交；远端已删除 |
| 远端 `codex/core-rz-decisions-20261006` | `4639774fe3c94ae27b3e831d0f0b9d6340de34fd` | 已在 compute 基线中，无独有提交；远端已删除 |
| 远端 `studio/ui-video-review-20261010` | `594ca5c16695aaae38d57cfe291d1bf63eaf7cf8` | 本轮集成并推送后，远端已删除 |
| 本地 `codex/gnn-data-positivity` | `25adec4224497981a0c124a3485f786194975be4` | 已在 compute 中，无独有提交；本地 `git branch -d` 已删除 |
| 第二个 GNN 分支 | 本轮本地／远端均未发现 | 不按数量猜测或删除其他主题分支 |
| `codex/cellular-burn-rate-audit-20261008` | `110e769ddbf4bad6a8aafef744dffe4c7aadf27f` | 保留：独有审计报告尚未合并，属于后续全仓检查的资料 |

main 及全部既有标签保持。只存在当前工作区与主仓库两个 checkout，没有可删的独立 O8／GNN／Studio 工作树；主仓库已脱离 compute 分支、停在前置基线，以保留旧构建、执行物及未跟踪项目入口，不能为精简而删除它。当前工作区是 compute 的唯一分支 checkout。主仓库原有未跟踪 AGENTS.md 另增一个短恢复指针，不复制进度台账或修改旧源码。

已删除引用的提交均由集成后的 compute 历史保留，不做 GC／reflog 清理。需要恢复时按表中 SHA 建回相应本地分支；远端恢复须按届时授权执行。前后 Git 清单及验证日志保留在本机证据目录。

## 协作与接续

| 任务 | worker 与采用范围 |
|---|---|
| 旧进度核对 | DeepSeek 只读核对；首轮两项动作建议与“复用已验项／外部平台可选”冲突，返修交付解析失败，未作为验收依据；Codex 直接核对版本化记录形成上表 |
| Studio 合并边界 | DeepSeek 首轮只读审查已独立接受；合并后两个冲突文件的定点报告已保留，但因前缀文字触发网关格式失败，未授网关 ACCEPT；最终由 Codex 独立 diff／契约核对及 385 项检查验收，Codex 唯一写入 |
| 环境入口说明 | Gemini 官方 CLI 润色冻结文本，Codex 独立核对后采用；命令、路径、数字、限定条件保持 |

本轮共派发 5 项真实提供方任务，预算与原始产物由现有全局网关留存。没有建立新协作规则或项目网关。格式失败任务的本机完整 JSON 已作精确解析并留副本，保留网关 FAILED／未采用状态，不通过它解锁依赖。发现的交付格式失败和嵌套锁标记恢复问题仅记录为后续全局维护反馈；本轮未修改 Skill／MCP 基础设施。

本机证据目录：`/mnt/c/Users/26428/.codex/visualizations/2026/10/11/01a12b15-a669-76a0-b053-6fd9e79a6b50/preflight-20261012`，含 before、retirement-candidates-before、Studio 检查日志、首轮 LFS 失败及多模型原始产物；已补 retirement-receipt／after 清单。仓库只保留本页和必要薄摘要，不纳入 H5、依赖目录或全量运行日志。

下一轮从[总计划](../../../../ARCH_MODERNIZATION_PLAN.md)阶段 1 的完整覆盖检查开始，按已验身份复用旧结果；RZ 长程和正式性能实现仍需按计划选入具体工作包，本轮不自动启动。
