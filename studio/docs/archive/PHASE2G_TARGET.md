# ARCH Studio — Phase 2G Target
## Continuous Preview Session, Initial AMR Workflow & Local Desktop Launch

> **当前唯一目标 / Active target**
>
> Studio 基线：
>
> ```text
> studio/phase2f-plot-presentation
> studio-phase2f-v0.13.0
> e97e571ba98641384aee44425a29b83255401856
> ```
>
> Core 最新交付：
>
> ```text
> branch: codex/studio-core-ui-contracts
> Local Workflow / AMR / Case Inspection: 97a2b50c440473dfe93ab82617c5631e8b9da24a
> Continuous Preview Session: d98f6f6e853ccb23eaa916ab1d20356019087622
> ```
>
> **如果当前 Studio 仍停在 v0.13.0，则先接 `97a2b50c`，再接 `d98f6f6e`。**
> 如果已经包含 `97a2b50c`，只接 `d98f6f6e`。
>
> 不 merge main，不重复 cherry-pick Core A/B/UI 旧提交，不进入 SSH / cluster / runtime simulation monitoring。

---

# 0. 阶段拆分

Phase 2G 拆成三个独立 checkpoint：

```text
2G-A — Continuous Preview Session Integration
2G-B — Case Discovery + Initial AMR UX
2G-C — Local Desktop Launcher / Packaged Window
```

严格：

```text
A → checkpoint/STOP → 用户确认
B → checkpoint/STOP → 用户确认
C → final checkpoint/STOP
```

禁止自动跨阶段。

---

# 1. Git / upstream integration

开始前：

```bash
git status
git branch --show-current
git rev-parse HEAD
git rev-parse studio-phase2f-v0.13.0^{commit}
git fetch origin
git rev-parse origin/codex/studio-core-ui-contracts
```

确认：

```text
Studio = e97e571ba98641384aee44425a29b83255401856
Core latest = d98f6f6e853ccb23eaa916ab1d20356019087622
```

建议分支：

```text
studio/phase2g-continuous-local-workflow
```

检查当前分支是否已有 97a：

```bash
git merge-base --is-ancestor \
  97a2b50c440473dfe93ab82617c5631e8b9da24a HEAD
```

若没有，先：

```bash
git cherry-pick --no-commit \
  97a2b50c440473dfe93ab82617c5631e8b9da24a
```

确认补丁范围和冲突后再提交。

随后：

```bash
git cherry-pick --no-commit \
  d98f6f6e853ccb23eaa916ab1d20356019087622
```

如果 Core/API/CMake/scientific implementation 出现无法直接解释的冲突：

> **STOP。不得自行重写 scientific Core。**

禁止 merge/rebase main、重复 5e96/A/B、reset/stash 用户工作。

---

# 2. Core 行为变化与 rebuild gate

`97a2b50c` 同时包含已批准的 Core 行为变化：

```text
IdealGas fallback Cv:
718.0
→ 7.18e6 erg/(g K)
```

这是统一 CGS 的 Core 改动。Studio 不得改回、不得用前端换算补偿。

两份 Core 增量接入后：

> **必须重新构建 CPU binary。**

不能继续使用 v0.13.0 旧 binary。

本阶段不要求 CUDA build / GPU validation。

---

# 3. Core verification gate

运行最新 18 组 scoped tests：

```text
preview_session_contract
preview_verified_resources
preview_exact_sample_cache
tabular_eos_ideal_gas
preview_initial_conversion
preview_api_contract
configuration_api_contract
preview_parameter_reads
preview_parameter_metadata
preview_sampling_limits
preview_cellular_2d
mainline_authority
ui_expansion_contract
refinement_indicator_math
amr_operation_plans
topology_transaction
initialization_probe
case_inspection_contract
```

要求：

```text
18/18 PASS
```

不运行完整 simulation / CUDA baseline / GPU tests。

Build Manifest 同步扩展本轮新增 API、session、AMR、discovery、EOS/cache 依赖；除非获得 authoritative dependency graph，`dependenciesComplete` 继续为 false。

---

# 4. Phase 2G-A — Continuous Preview Session

目标：

```text
首次准备可以较慢
后续连续改参复用已验证资源
每次仍执行真实完整初始化
绝不复用上一请求的场图冒充新结果
```

不增加低精度或截断预览模式。

## A1. Capability negotiation

每次使用 Preview binary 前查询：

```text
ARCH --preview-capabilities
```

只有：

```text
extensions.session.supported = true
```

才启动：

```text
ARCH --preview-session
```

Session v1 当前只支持 Linux/WSL CPU。

旧 binary 不支持 session 时：

```text
自动 fallback 到原 single-shot Preview
```

不能让旧 Preview 失效。

## A2. Host-owned session identity

Session 至少绑定：

```text
project
working directory
binary path
binary fingerprint
Build ID/profile
```

任一变化：

```text
terminate old session
new process generation
下次请求重新启动
```

Host 维护独立 `sessionGeneration/processToken`。Core `sequence` 只在当前进程内有意义。

## A3. Spawn / transport

Host 固定：

```text
approved ARCH
argv = --preview-session
shell = false
approved cwd/env
```

Browser 不提供 program/argv/cwd/env/PID/shell。

NDJSON：

```text
stdin 长期开启
stdout 按行读取
stderr 持续有界收集
```

不得等 EOF 再解析。

每行必须有 byte limit / UTF-8 / LF 完整性。fatal 协议错误使 session 失效。

## A4. Ready handshake

等待：

```text
kind = preview-session-ready
version = "1"
sequence = 0
```

仅表示进程可接收请求，不代表 EOS/模型/场图已准备。

## A5. Request envelope

Host 生成唯一 requestId。

Sod：

```json
{"command":"--preview","caseId":"Sod","requestId":"...","configText":"...","samples":512}
```

Cellular：

```text
samplesX1 + samplesX2
```

通用 session client 应可承载：

```text
--inspect-config
--inspect-case
--amr-resources
--preview
--preview-amr
```

A 的 UI 先重点接 `--preview`；B 再接其余工作流。

## A6. Progress

`preview-session-progress` 可显示阶段：

```text
request
configuration
support
setup
eos
sampling
initialization
initial-refinement
source-validation
complete
```

显示易读阶段，不映射虚构百分比。

**progress stage `complete` 也不是成功。**

## A7. Final authority

只有：

```text
kind = preview-session-result
```

才能成为候选最终结果。

检查：

```text
session generation
version
sequence
requestId
caseId
configRevision
build/binary identity
exitCode
response.status
```

嵌套的 `response` 直接进入现有 validated response handler，不新写第二套 scientific parser。

## A8. 输入合并

普通编辑：

```text
约 300 ms debounce
```

一次 session：

```text
1 active request
+
1 latest pending request
```

若 A 正在计算，用户连续产生 B/C/D/E：

```text
active = A
pending = E
```

禁止排成 A/B/C/D/E。

A 完成时若已 obsolete：

```text
不发布 Current
直接提交最新 pending
```

## A9. Invalid intermediate input

输入暂时非法时：

```text
不自动启动昂贵 Preview
```

可继续使用轻量 `--inspect-config` 做表单检查。

恢复为可提交状态后再 debounce schedule。

保留显式 Update/Retry 作为“立即提交当前最新版本”。

## A10. Drag

Sod marker：

```text
pointer move → local candidate
release → one Working Copy edit / one Undo
→ schedule real Preview
```

不在 pointermove 每帧请求 Core。

Linear/Log、配色、显示范围、clipping、zoom/pan 不得触发 session request。

## A11. UI 状态与旧图

首次：

```text
Preparing preview...
```

已有成功图后：

```text
Parameters changed · updating...
```

等待时保留旧图；Inspector 继续绑定旧图和旧 identity。

禁止显示：

```text
新参数 + 旧 Inspector 值
```

新结果成功后一次性替换：

```text
field
Inspector
effective metadata
state
provenance
```

失败：

```text
保留 Working Copy
保留上一成功图 / Inspector
单独显示新失败
```

## A12. Cancel / timeout

Cancel：

```text
kill entire session process
SIGTERM → bounded grace → SIGKILL
reap
increment generation
discard late stdout
```

不要把 cancel 作为 stdin 命令排队。

下一次请求启动新 session，资源重新 Cold prepare。

当前 contract budgets：

```text
field / inspect-case: CPU 300s / Host wall 360s
AMR: CPU 30s / Host wall 45s
config/resource: CPU 30s / Host wall ~45s
```

实际值优先读取 capability/API contract。

阶段事件不能续期 wall timeout。

## A13. Session request limit

当前：

```text
256 requests / session
```

`preview-session-closed reason=request-limit`：

```text
正常 recycle
```

若仍有 pending：

```text
启动新 session
提交 latest
```

不要显示成 crash。

## A14. Resource stats

Final outer envelope 提供：

```text
elapsedMilliseconds
stages
tableLoads / tableHits
retainedTables
fileContentMatches
fileHashes
sampleEvaluation
...
```

可放 Developer/Diagnostics。

不要仅凭 `tableLoads=0` 显示“跨请求缓存命中”；只有满足 Core 定义时才解释 reuse。

## A15. 性能 UAT

记录 Studio 端到端 wall time：

```text
Sod 512 cold
Sod warm edit

Cellular 128×128 cold
Cellular position warm
Cellular temperature warm
```

Core 样本只作参考：

```text
Cellular 128²:
cold ~6.84s
position ~0.451s
temperature ~0.479s
```

Studio 另外记录：

```text
Host queue
Core elapsed
transport/parse
render-to-current
total wall
```

不要把 Core elapsed 当 UI latency。

## A16. Queue / cancel UAT

快速编辑：

```text
x_pos .30 → .31 → .32 → ...
```

证明：

```text
最多 1 active + 1 latest pending
最终 Current = latest Working Copy
```

取消真实 Cellular 256×256：

```text
process gone
old image retained
next edit starts new session
new result Current
```

## A17. A checkpoint

完整回归：

```text
Phase 2F parameter editor
identity safety
Sod marker
Cellular 2D
plot transforms
Save lifecycle
Build
Mock / Plotfile
path preflight
strict validation
```

生成：

```text
studio/PHASE2G_A_SESSION_COMPLETION_REPORT.md
```

建议：

```text
studio-phase2g-a-v0.14.0
```

**STOP。等待用户授权 B。**

---

# 5. Phase 2G-B — Case Discovery + Initial AMR UX

只在 A checkpoint 后开始。

## B1. Model discovery

使用：

```text
ARCH --list-cases
```

模型列表必须来自 selected binary registry。

禁止：

```text
固定前端模型数组
从 .cpp 文件名猜 registered case
```

每个 case 显示：

```text
registered name
field Preview capability
AMR Preview capability
dimension / geometry support
source association if Host knows
build status
```

“已注册”不等于“支持完整场图”。

## B2. 11-model inspect-case

使用：

```text
--inspect-case CASE --config-stdin
```

当前 11 个模型统一使用：

```text
actual parameter reads
default/file/effective
unit evidence
small Init samples
diagnostics
source evidence
```

没有完整场图的模型仍可：

```text
编辑
Inspect initialization
```

明确显示：

```text
Full field Preview unavailable
```

Inspection 使用 capability 的长预算，支持 Cancel。

## B3. AMR panel

Grid 下增加稳定：

```text
Adaptive Mesh Refinement (AMR)
```

主要：

```text
lrefinemin
lrefinemax
refine_var
refine_threshold
derefine_threshold
max_blocks
```

Advanced：

```text
regrid_interval
```

说明和 applicability 使用 Core presentation。

可提供 `Enable refinement`，关闭时保证 min/max level 同时成为合法关闭状态；重新开启恢复上次有效值或要求用户输入。

## B4. Resource table

使用：

```text
--amr-resources
```

展示：

```text
Level
fullDomainLeafBlocks
activeCells
baseStateBytes
stateBytesIncludingSpecies
overflow
poolPreallocatedBaseBytes
configured max_blocks
assumptions
```

明确标题：

```text
Full-domain refinement estimate
```

不得宣传：

```text
Will OOM
Guaranteed runnable
Safe on N nodes
```

## B5. Actual AMR

使用：

```text
--preview-amr
```

当前仅：

```text
Sod 1D Cartesian
CellularDet 2D Cartesian
```

Preview budget 与 `.par max_blocks` 分离。

当前默认：

```text
meshMaxBlocks 512
meshMemoryMiB 128
```

具体限制读取能力/API，不自行扩大。

## B6. AMR response

消费：

```text
logicalKey
level
logicalIndex
lower/upper
cellShape
cellSpacing
levelCounts
leafCount
complete
completedPasses
snapshot
limitedReason
configuredMaxBlocks
workingCapacity
resources
```

不要用 pool index 作为稳定 identity。

## B7. limited semantics

`status=limited`：

```text
不是 error
也不是 complete
```

如果返回最后完整平衡 hierarchy：

```text
可显示
必须标 Limited / Incomplete
```

若：

```text
leaves=[]
snapshot=none
```

不能显示“0 blocks completed”。

## B8. AMR rendering

2D：

```text
block outline 粗线
cell line 细线
level 色彩区分
```

1D：

```text
block interval
cell ticks
```

缩小时优先块边界；放大后再画 cell lines。

绘制密度优化不改变 raw hierarchy / block counts。

## B9. AMR Inspector

块选择显示：

```text
logicalKey
level
logicalIndex
lower/upper
cellShape
cellSpacing
```

当前 AMR API 没有 AMR cell field arrays。

因此旧场 Inspector 继续称：

```text
Init sample
```

不能改称“AMR cell actual value”。

## B10. Field + AMR identity

可叠加的最低条件：

```text
same project
same case
same configRevision
same build/binary
same EOS sourceFingerprint
```

不匹配：

```text
隐藏旧 AMR
或明确旧版
```

不能把旧网格叠到新场并称 Current。

新 field 可以先显示；matching AMR 完成后再挂载。

## B11. AMR UAT

至少覆盖：

```text
actual binary registry
11-model inspect

Sod AMR
CellularDet AMR
1D/2D block orientation
2:1 transition
block Inspector
cell lines on zoom
level visibility

resource table
overflow representation

limited snapshot
root-grid-no-snapshot
cancel
timeout
late result rejection

field/AMR identity mismatch
EOS source mismatch
```

生成：

```text
studio/PHASE2G_B_AMR_COMPLETION_REPORT.md
```

建议：

```text
studio-phase2g-b-v0.15.0
```

**STOP。等待用户授权 C。**

---

# 6. Phase 2G-C — Local Desktop Launcher

只在 A/B checkpoint 后开始。

## C1. Architecture audit gate

先生成：

```text
studio/PHASE2G_C_DESKTOP_ARCHITECTURE_AUDIT.md
```

比较：

```text
Electron
Tauri
仓库已有可复用方案
```

评估：

```text
React build integration
Node Host reuse
Windows + WSL bridge
packaging
window lifecycle
file dialogs
port ownership
process cleanup
bundle size
dev/prod parity
```

需求文档当前认为 Electron 对 React + Node Host 改造较直接，但不是无审计强制结论。

若当前环境无法可靠完成 Windows+WSL desktop path：

> STOP 并报告，不伪装完成。

## C2. CLI / desktop goal

建议支持：

```text
arch-studio

arch-studio --case Sod \
  --config simulation/Sod/Sod.par

arch-studio --project /path/to/ARCH \
  --binary /path/to/build/bin/ARCH

arch-studio --source /path/to/case.cpp \
  --config /path/to/input.par
```

实际参数名可调整，但语义保持。

无参数时从 cwd 发现项目；无法唯一确定时打开 Project Picker。

## C3. Independent window

默认：

```text
独立应用窗口
taskbar entry
window title
native file dialogs
```

不依赖：

```text
Chrome
Edge
default browser
手动 localhost URL
```

Browser 模式可保留为 debug/optional。

## C4. Packaged assets / Host lifecycle

桌面生产包必须：

```text
加载打包后的 React assets
不要求 Vite dev server
```

Launcher 管理 Host：

```text
port selection
ready check
connection
shutdown
```

用户不需要手动配置 port。

## C5. Windows + WSL boundary

Windows desktop：

```text
对接 WSL/Linux Host/Core
保留 Linux cwd/path semantics
```

不得宣传：

```text
Native Windows Core
```

## C6. Missing prerequisite UX

缺：

```text
WSL
build directory
binary
runtime dependency
Host startup
```

界面必须显示：

```text
具体缺项
可执行下一步
```

打开窗口不自动触发长 build。

## C7. Cleanup

关闭窗口：

```text
graceful Host shutdown
preview-session cleanup
no orphan Node / ARCH processes
```

重启不能误接旧 session。

## C8. Desktop UAT

至少：

```text
project root launch
nested cwd launch
--case/--config
--project/--binary
path with spaces
missing binary
missing WSL/runtime
port collision
window close cleanup
relaunch
packaged assets without Vite
```

在 Windows + WSL 实际测试：

```text
open project
Build
Save lifecycle
warm Preview session
AMR overlay
```

生成：

```text
studio/PHASE2G_DESKTOP_LAUNCH_REPORT.md
```

---

# 7. 每阶段 automated checks

每个 checkpoint：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

A 和 B 最终还应运行 Core 18 scoped groups。

不得：

```text
削弱 tests
跳过新失败项换绿色
运行完整 simulation
运行 CUDA baseline
```

除非用户另行批准。

---

# 8. Final reports / versioning

最终生成：

```text
studio/PHASE2G_COMPLETION_REPORT.md
studio/PHASE2G_CONTINUOUS_PREVIEW_REPORT.md
studio/PHASE2G_INITIAL_AMR_REPORT.md
studio/PHASE2G_DESKTOP_LAUNCH_REPORT.md
```

建议 tags：

```text
2G-A: studio-phase2g-a-v0.14.0
2G-B: studio-phase2g-b-v0.15.0
2G final: studio-phase2g-v0.16.0
```

每个 checkpoint 后都 STOP。

---

# 9. Explicit deferred

不属于 Phase 2G：

```text
SSH
cluster scheduler
MPI deployment planning
runtime simulation monitoring
runtime plotfile streaming
3D AMR visualization
Cellular editable graphical binding
generic arbitrary C++ auto-UI
native Windows scientific Core
automatic arbitrary source-to-build integration
```

---

# 10. Final rules

```text
Warm session reuse
≠
Reuse old field result
```

```text
Progress complete stage
≠
Successful final result
```

```text
1 active + 1 latest pending
>
Queue every keypress
```

```text
Cancel
=
Kill / recycle session
```

```text
Old image retained
≠
Old image represents new parameters
```

```text
Field + AMR overlay
requires matching config / build / EOS source
```

```text
AMR limited
≠
AMR complete
```

```text
Resource estimate
≠
OOM prediction
```

```text
Desktop wrapper
≠
Native Windows Core
```

```text
Checkpoint / STOP
>
Automatically start next phase
```
