# ARCH Studio — Phase 2D Real Initial Condition Preview Target

> **当前唯一目标 / Active target only**
>
> Phase 2C `Build Integration` 已封箱：
>
> ```text
> tag: studio-phase2c-v0.7.0
> branch: studio/phase2c-build-integration
> ```
>
> Phase 2D 的目标是第一次把：
>
> ```text
> 当前 Working Copy（允许未保存）
> +
> 当前受控 ARCH binary / Build provenance
> +
> selected case ID
>          ↓
> authoritative ARCH initialization path
>          ↓
> real initial-condition samples
>          ↓
> existing Studio Preview / Inspector
> ```
>
> 接成一条真实链路。
>
> **本阶段只打通 Sod 1D Real IC Preview。**
>
> 不实现 Cellular 2D、不实现 graphical parameter binding、不实现 AMR reconstruction、不运行正式 simulation、不做 SSH / scheduler / remote，也不允许前端重新实现物理。

---

# 0. Phase 2D 的最高原则

组长需求中这一阶段最重要的要求是：

```text
“更新预览”使用当前编辑内容，包括尚未保存的修改。
本地接入层将参数交给当前编译程序，返回真实初始化结果。
```

因此 Phase 2D 必须保证：

```text
Real Preview
=
真实 ARCH 初始化逻辑的结果
```

禁止：

```text
Frontend 根据 .par 猜一个结果
MockProvider 冒充 Real
从旧 Plotfile 反推 Initial Condition
复制一套 Setup/Init 逻辑到 TypeScript
通过手写公式复现 Sod
```

Real Preview 的科学来源必须是：

> **与 ARCH simulation 共用的 authoritative initialization implementation。**

---

# 1. Baseline / 基线确认

不要从聊天记录猜 commit。

任何修改前执行：

```bash
git status
git branch --show-current
git rev-parse HEAD
git describe --tags --always --dirty
git rev-parse studio-phase2c-v0.7.0^{commit}
```

要求：

- `studio-phase2c-v0.7.0` 必须存在；
- 确认 Phase 2C worktree clean；
- 不 reset / discard 用户工作；
- 不移动已有 tag；
- 从真实 Phase 2C checkpoint 创建独立分支。

建议：

```text
studio/phase2d-real-ic-preview
```

---

# 2. P2D-M0 是强制 Preview Contract 审计

**写任何实现代码前**，先只读审计当前 ARCH 是否已经存在 authoritative Preview entry point。

至少检查：

- unified `ARCH` executable 当前 CLI / case selection contract；
- `ProblemRegistry` / case registry；
- `Setup()` / `Init()` 的真实调用位置；
- 是否已有 init-only / preview / sample / diagnostic mode；
- 是否已有不会进入 timestep loop 的 test/helper executable；
- 是否已有 machine-readable output contract；
- 是否能从 stdin / pipe / explicit temporary config 输入参数；
- 是否已有 fields / units / coordinates metadata；
- 是否有安全取消边界；
- 是否会产生 simulation output、checkpoint、plotfile 或其他副作用。

**不得仅因为能找到 `Setup()` / `Init()` C++ 函数就宣布已有可用 Preview API。**

---

# 3. M0 Stop Gate / 停止门

如果审计结论是：

```text
当前 ARCH binary / 已授权 helper
不存在可直接调用的 authoritative init-only Preview interface
```

则：

> **立即停止 Phase 2D 实现，不修改 scientific Core，不自己发明物理。**

生成：

```text
studio/PHASE2D_CORE_PREVIEW_CONTRACT_GAP.md
```

至少写明：

```text
Current initialization call path
Why current executable cannot safely provide init-only preview
Minimum Core/local interface required
Suggested request fields
Suggested response schema
Required provenance
Cancellation requirements
No-physics-duplication constraint
```

然后停止，等待核心方确认。

**禁止为了“完成 Phase 2D”直接修改 ARCH scientific Core。**

---

# 4. 允许继续的 Preview Interface

只有 M0 找到或用户/核心方明确批准一个 authoritative interface 时才继续。

可接受形式例如：

```text
A. existing ARCH CLI preview mode

B. existing dedicated init-preview executable

C. explicitly approved thin Preview Bridge
   that links/calls the same ARCH Setup()+Init() implementation
   without reimplementing physics
```

不可接受：

```text
test-only fake provider promoted to production
frontend-side formulas
parsing case.cpp and emulating Init
reading a later Plotfile and calling it Initial Condition
starting a real simulation and stopping after one step
```

如果采用 Preview Bridge：

- 必须是 integration / adapter；
- scientific logic 仍来自 ARCH Core；
- 不复制 case physics；
- 不改变 simulation behavior；
- 不进入 timestep loop；
- 不生成正式 simulation output。

---

# 5. Protocol version

Phase 2C Local Host protocol：

```text
1.2
```

Phase 2D 增加 Preview contract，因此升级：

```text
1.3
```

要求：

- frontend 与 Local Host 同时检查 1.3；
- 1.2 Host 不得显示 Real Preview 可用；
- protocol mismatch 明确报错；
- 不 fallback 到 Mock 并冒充 Real。

---

# 6. Capabilities

Phase 2D 成功后，支持 Preview 的 Host：

```ts
interface HostCapabilities {
  readProject: true;
  writeConfig: true;
  build: true;
  preview: true;
  watchFiles: false;
}
```

但只有以下条件满足时：

```text
authoritative Preview Profile exists
+
required binary/provenance exists
+
Preview runner contract validated
```

才允许：

```text
preview = true
```

否则保持：

```text
preview = false
```

并说明原因。

---

# 7. Preview Profile 必须是 Host-owned

像 Build Profile 一样，Browser 不能提交 program / argv / cwd / arbitrary executable。

定义 Host-owned Preview Profile，例如：

```ts
interface PreviewProfile {
  id: string;
  displayName: string;

  buildProfileId: string;
  caseId: string;

  dimension: 1;
  defaultSampleCount: number;
  maxSampleCount: number;

  runnerKind:
    | "existing-arch-cli"
    | "approved-preview-helper";

  configured: boolean;
}
```

Frontend 只能提交：

```text
projectId
previewProfileId
config text / revision
```

不能提交：

```text
program
binaryPath
argv
cwd
env
shell
case source path
```

---

# 8. Sod-only scope

Phase 2D 只要求：

```text
caseId = Sod
dimension = 1
```

真实验收目标：

```text
Sod Working Copy
        ↓
Real Preview
        ↓
1D Density / Pressure
        ↓
field switch
        ↓
sample Inspector
```

如果 Core 同时返回更多字段：

```text
Temperature
Velocity
Energy
Composition
```

Frontend 可以按真实返回字段展示。

但最低验收只要求：

```text
Density
Pressure
```

或 Core 明确对应的实际字段。

不要硬编码“必须存在 TEMP”之类没有 contract 依据的字段。

---

# 9. Unsaved Working Copy 是 Preview 输入

Preview 必须使用：

> **当前编辑器 Working Copy 的 serialized text。**

不是：

```text
磁盘 .par
```

除非 Working Copy 与磁盘一致。

Preview request：

```ts
interface RealPreviewRequest {
  projectId: string;
  profileId: string;

  configText: string;
  configRevision: string;

  requestedSampleCount?: number;
}
```

其中：

```text
configRevision
```

必须稳定标识当前 Working Copy 内容。

推荐：

```text
SHA-256(serialized config text)
```

或等价稳定 revision。

---

# 10. Preview 不得隐式 Save

Real Preview：

```text
Working Copy Dirty
→ Preview
```

必须合法。

不得：

```text
Preview
→ Save .par
```

不得修改当前 project config file。

用户点击 Save 仍然是独立明确操作。

---

# 11. Config transport

优先级：

## Preferred

如果 authoritative Preview runner 支持：

```text
stdin / pipe / config-stdin
```

优先直接传入 serialized Working Copy。

## Allowed fallback

如果 runner **只能**接受文件路径，可使用 Local Host 私有临时 config：

```text
studio/.local/preview/<requestId>/config.par
```

或安全 OS temp directory。

要求：

- 不覆盖 project `.par`；
- private directory；
- unique request ID；
- no-follow / path confinement；
- request 完成/失败/取消后清理；
- temp 文件不是 Save；
- UI 不把 temp path 当 current config；
- provenance 使用 configRevision，而不是依赖 temp filename。

---

# 12. 不允许通过运行 simulation 获取 Preview

Preview runner 必须：

```text
Setup()
+
Init(sample coordinates)
```

或 Core 认可的等价 init-only 路径。

不得：

```text
进入 timestep loop
写 plotfile
写 checkpoint
更新 simulation output dir
```

M0 / integration UAT 中必须检查 Preview 调用没有产生新的正式 simulation outputs。

---

# 13. Preview request / response identity

每次 Preview request 必须有：

```text
requestId
projectId
caseId
configRevision
buildId / binary fingerprint provenance
```

Host 返回的数据必须携带相同身份。

例如：

```ts
interface PreviewIdentity {
  requestId: string;
  projectId: string;
  caseId: string;

  configRevision: string;

  buildId: string;
  binarySha256: string;

  profileId: string;
}
```

Frontend 不得仅根据“最后一个 HTTP response”决定 current。

---

# 14. Binary readiness policy

Phase 2C 当前真实 Profile：

```text
mapping = configured
full dependency freshness = unknown
tracked inputs = known subset
last successful Build Manifest = available
```

Phase 2D 不得把：

```text
full dependency freshness unknown
```

伪装成：

```text
fully verified current
```

第一版 Real Preview 可以在以下条件下允许：

```text
last successful Build Manifest exists
+
binary exists
+
binary fingerprint matches manifest
+
selected tracked inputs unchanged
+
Build Profile unchanged
+
mapping is configured/acceptable
```

同时 UI 明确：

```text
Preview uses the last successful tracked build.
Full dependency freshness is not independently verified.
```

如果：

```text
tracked input changed
Binary = needs-build
```

则 Real Preview disabled：

```text
Build required before real preview.
```

---

# 15. Build failure and Preview

如果：

```text
last successful Build A exists
source changes
Build B fails
```

则：

- last successful binary A provenance 保留；
- 但如果 tracked source 已变：
  - current source requires Build；
  - Real Preview 默认禁止以 A 冒充 current source；
- 已存在的旧 Preview 可以继续显示为 stale / previous build；
- 不清空 Working Copy。

---

# 16. Preview Data contract — 1D

定义明确的 machine-readable response。

示例：

```ts
interface RealPreview1D {
  schemaVersion: string;

  identity: PreviewIdentity;

  dimension: 1;

  coordinate: {
    name: "x";
    unit?: string | null;
    values: number[];
  };

  sampling: {
    kind: "uniform" | "nonuniform";
    count: number;
    valueLocation: "init-sample";
  };

  fields: Array<{
    key: string;
    displayName?: string;
    unit?: string | null;
    values: number[];
    min: number;
    max: number;
  }>;

  diagnostics?: PreviewDiagnostic[];
}
```

实际字段名以 Core contract 为准。

---

# 17. Response validation

Host / Adapter 必须验证：

```text
schema version
identity
dimension = 1
coordinate finite
coordinate monotonic where required
field array length = coordinate length
min/max finite and consistent
all rendered values finite
duplicate field keys
unsupported/malformed response
response size
```

如果含：

```text
NaN / Inf
```

不能默默交给 Renderer。

返回明确 Preview failure / diagnostic。

---

# 18. Machine-readable output only

Real Preview transport 不能依赖“从 human stdout 猜 JSON”。

可接受：

```text
stdout = only structured preview payload
stderr = diagnostics
```

或：

```text
private structured result file
+
stderr/stdout log
```

不接受：

```text
human log
JSON fragment
human log
```

再用 regex 硬扒。

---

# 19. Response bounds

Sod 1D Phase 2D 不需要巨大数据。

建议限制：

```text
sample count default = 512
sample count max = 4096
field count bounded
structured response <= 8 MiB
diagnostic log bounded
```

具体值可根据 M0 contract 审计调整。

禁止：

```text
unbounded stdout
unbounded JSON
```

---

# 20. Preview process execution security

沿用 Build security：

```text
127.0.0.1 only
exact Origin / Host
protocol 1.3
Host-owned Preview Profile
shell: false
bounded request/response
```

Browser 不得控制：

```text
executable path
argv
cwd
env
output path
temporary path
```

没有：

```text
generic /exec
generic /shell
```

---

# 21. Preview concurrency

同一个 Project Session：

```text
最多一个 active Real Preview
```

但用户可能：

```text
Preview A generating
→ edit parameter
→ Preview B
```

需要定义：

- 新请求可以先 cancel A 再启动 B；
- 或拒绝 B 直到 A cancel/finish；

推荐：

```text
new Preview request
→ invalidate/cancel previous request
→ start latest
```

前提是 cancellation 安全实现。

无论 process 最后如何退出：

> **旧 requestId 的结果绝不能覆盖新 configRevision。**

---

# 22. Cancellation 是 Phase 2D 验收项

组长需求明确要求 Preview 生成期间可取消。

实现：

```text
POST /api/preview/:requestId/cancel
```

要求：

- 只能取消 Host 自己启动的当前 Preview；
- frontend 不提交任意 PID；
- Linux 使用受控 process / process group；
- graceful termination；
- bounded timeout 后必要时强制结束；
- temp input/output 清理；
- Working Copy 保留；
- last successful Preview 保留并继续显示为 stale；
- Cancelled request 不产生 Current Preview。

---

# 23. Config edits during Preview

关键 race：

```text
revision A
→ Preview A starts

user edits
→ revision B

Preview A returns
```

正确行为：

```text
A result may be stored only as obsolete/history if desired
but MUST NOT become Current
```

UI：

```text
Preview result discarded: configuration changed while generating.
```

或静默保持：

```text
Preview stale
```

不要用 A 覆盖 B。

---

# 24. Build changes during Preview

Preview 开始时 snapshot：

```text
buildId
binary SHA-256
configRevision
```

Preview 完成前，如果 Host 发生新的 successful Build：

- Preview A 仍属于旧 build；
- 不得标为 current for new build；
- 应显示 stale / previous build；
- 下一次 Update Preview 使用新 build。

不要混淆 Build provenance。

---

# 25. Preview state model

现有 Preview state 扩展但不要推倒重写。

至少：

```text
none
current
stale-config
stale-build
generating
failed
cancelled
```

可以内部保持现有 reducer 结构，只要 UI 语义可区分。

必须允许：

```text
Config Dirty
Preview Current
```

如果 Preview 正是基于当前 unsaved Working Copy 生成。

也允许：

```text
Config Saved
Preview Stale-Build
```

各状态保持独立。

---

# 26. “Current” 的严格定义

Real Preview 只有同时满足：

```text
preview.configRevision == current Working Copy revision
+
preview.buildId == selected last-successful/accepted Build identity
+
binary fingerprint == preview provenance
+
case/profile match
```

才可显示：

```text
Preview: Current
```

只要任一不满足：

```text
Preview: Stale
```

并说明原因：

```text
Parameters changed
Build changed
Source needs build
Different case/config
```

---

# 27. 保留 last successful Preview

沿用 Phase 1C2 修复原则。

以下情况不得清空旧图：

```text
parameter edit
new Preview generating
Preview fails
Preview cancelled
Build changes
source changes
```

旧图保留，并标：

```text
Previous preview
Stale — parameters changed
Stale — build changed
```

`No preview generated` 仅用于：

> 当前 session 从未成功得到 Real Preview。

Mock Preview 与 Real Preview 仍是不同来源，不得互相冒充。

---

# 28. Real Config 模式接入

当前 Real Config 中央区域从：

```text
No scientific preview
```

升级为：

```text
REAL INITIAL CONDITION
```

当 capability available 且 binary ready：

```text
[ Generate Real Preview ]
```

有旧结果时：

```text
[ Update Preview ]
```

如果 Build required：

```text
Source changed · build required

[ Build ]
```

Phase 2D 不强制实现自动：

```text
Build & Preview
```

用户可以：

```text
Build
→ Preview
```

保持两个动作分离。

---

# 29. Real Preview 不使用 Mock 字段

Real Config / Real Preview：

- 字段列表必须来自 authoritative response；
- 不写死 Density/Temperature/Pressure；
- Mock Provider 不参与数据生成；
- UI source badge 必须明确：

```text
REAL IC
```

Inspector 也显示：

```text
Source: ARCH initialization
```

---

# 30. 1D visualization

复用已验证的 LineVis / 1D renderer 思路。

要求：

```text
Field selector
x coordinate
line plot
zoom
pan / reset where supported
min / max
sample Inspector
```

不要为了 Real Preview 新写第二套 line chart 系统。

尽量复用 Phase 1A Plotfile 的 visualization primitives / components，但不要把 Plotfile 数据语义和 IC Preview 数据语义混在同一个 provider 中。

建议：

```text
RealInitPreviewProvider
        ↓
PreviewData / 1D view model
        ↓
existing visualization
```

---

# 31. Inspector

点击真实 Preview sample 后：

```text
Coordinates
x

Field
current field value

Available fields
optional cross-field values if response supplies them

Provenance
case ID
config revision
build ID
binary fingerprint short form
sampling type
```

单位：

- Core 返回 → 显示；
- Core 未返回 → `Unit not provided`；
- 不猜。

---

# 32. Preview provenance UI

用户应能回答：

```text
这张图来自哪份参数？
来自哪次 Build？
来自哪个 case？
这是 Init sample 还是 simulation cell？
```

主 UI 不用塞完整 hash。

建议：

```text
REAL IC
Sod · Build bd... · Working Copy
```

展开详情：

```text
config revision
build ID
binary SHA-256
profile ID
sample count
sampling kind
generated at
```

---

# 33. Diagnostics

Core / Preview runner 返回的真实初始化错误，例如：

```text
parameter validation
EOS initialization failure
unknown case
invalid composition
domain/config issue
```

Frontend：

- 显示结构化 error；
- 尽量定位到 parameter key（如果 contract 提供）；
- Working Copy 保留；
- old Preview 保留；
- 提供 Retry；
- 不自动 Save；
- 不“智能修复”参数。

---

# 34. Preview diagnostic contract

建议：

```ts
interface PreviewDiagnostic {
  severity: "info" | "warning" | "error";
  code?: string;
  message: string;
  parameterKey?: string;
}
```

不要把 arbitrary raw exception stack 当主 UI。

technical details 可以折叠复制。

---

# 35. Parameter metadata — only pass through

组长需求还需要：

```text
default value
source
unit
description
checks
```

但完整 ParameterMetadata backend 不属于本阶段主目标。

Phase 2D 只允许：

> 如果 authoritative Preview/Core contract 已经返回 metadata，则安全 passthrough 到 UI。

禁止：

```text
解析 case.cpp 猜 default
按参数名猜单位
按当前值猜 range
```

没有则：

```text
Metadata unavailable
```

Phase 2E 再完整接 metadata / binding。

---

# 36. x_pos graphical marker 延后

Sod `x_pos` 竖线、拖拽联动是组长明确要求，但属于：

```text
Phase 2E — Graphical Binding
```

Phase 2D 只要求：

```text
用户从参数面板精确修改 x_pos
→ Working Copy revision changes
→ Update Real Preview
→ authoritative line data changes
```

不在 Phase 2D 根据 key 名自动画 marker。

---

# 37. Phase 2D Real Sod UAT

M0 contract 通过、实现完成后，必须用真实 Sod 做有限 UAT。

## A. Initial preview

```text
Open Project Config
Build provenance available
Generate Real Preview
```

验证：

- authoritative runner 被调用；
- 不进入 simulation timestep；
- 不产生正式 Plotfile / checkpoint；
- Density / Pressure 等实际返回字段出现；
- 1D curve 显示；
- Inspector 可读真实 sample；
- badge = REAL IC。

## B. Unsaved Working Copy

```text
disk x_pos = A
Working Copy x_pos = B
do not Save
→ Update Preview
```

验证：

- Preview provenance configRevision 对应 B；
- 实际 returned data 对应当前 Working Copy request；
- 磁盘 `.par` 保持 A；
- Config 仍 Dirty；
- Preview 可以 Current。

## C. Revision race

```text
start Preview revision A
→ edit to B before A finishes
```

验证：

- A 不成为 Current；
- B state remains stale / needs update；
- old successful Preview retained。

## D. Cancel

```text
start Preview
→ Cancel
```

验证：

- process ends；
- temp cleaned；
- Working Copy retained；
- last successful Preview retained；
- state = cancelled/stale；
- cancelled result never becomes Current。

## E. Preview failure

使用 authoritative, safe invalid Working Copy，例如：

```text
Core contract 明确认定的 invalid parameter
```

验证：

- real error returned；
- parameter edit retained；
- disk unchanged；
- previous Preview retained；
- Retry available。

不要修改 scientific source 制造错误。

---

# 38. Scientific verification boundary

Studio tests 不得宣称：

```text
Sod 的理论解正确
```

Phase 2D 验证的是：

```text
数据确实来自 authoritative Setup()+Init()
request identity正确
parameter changes reach Core
fields/coordinates faithfully transported
UI displays returned values correctly
```

如果需要理论物理验证：

> 使用 Core existing tests / scientist acceptance，属于独立证据。

---

# 39. Negative response tests

至少覆盖：

```text
preview capability false
missing Build Manifest
binary fingerprint mismatch
tracked source needs-build
unknown case
runner spawn failure
nonzero exit
cancel
timeout
malformed payload
wrong schema version
wrong requestId
wrong configRevision
wrong buildId
dimension != 1
coordinate length mismatch
field length mismatch
NaN
Inf
duplicate fields
oversized response
stale response after newer request
```

---

# 40. Security tests

继续保证：

```text
no generic exec
no browser-supplied executable
no browser-supplied argv
no browser-supplied temp path
no project .par overwrite during Preview
no output path traversal
private temp cleanup
bounded config body
bounded preview response
```

Preview Profile ID 不存在：

```text
reject
```

不能 fallback 到 arbitrary program。

---

# 41. Timeout

Preview 不是 simulation。

为 Sod 1D 设置合理 bounded timeout，例如：

```text
30–60 seconds
```

具体根据 M0 authoritative runner 现实成本确定。

超时：

```text
Preview failed / timed out
```

并终止 Host-owned process。

不要让 Preview process 无限挂住。

---

# 42. Phase 2D milestones

严格按顺序：

```text
P2D-M0   authoritative Preview Contract audit + STOP gate
P2D-M1   protocol 1.3 + PreviewProfile / schema
P2D-M2   fixed-profile Preview runner + Working Copy transport
P2D-M3   response validation + provenance
P2D-M4   cancellation / timeout / revision protection
P2D-M5   RealInitPreviewProvider + Real Config integration
P2D-M6   1D Line visualization + contextual Inspector
P2D-M7   diagnostics / failure / stale retention
P2D-M8   security / race / negative tests
P2D-M9   real Sod finite UAT
P2D-M10  regression + completion report + checkpoint
```

如果 P2D-M0 Stop Gate 触发：

> 不执行 M1–M10。

---

# 43. Regression requirements

必须保留：

## Phase 2C Build

- fixed Build Profile；
- build=true；
- Build Manifest；
- tracked freshness；
- output log；
- Build security；
- no arbitrary command。

## Phase 2B Config lifecycle

- Open Project Config；
- Working Copy；
- Save；
- Save As；
- Revert；
- Reload；
- conflict；
- atomic writes。

## Existing Studio

- Mock Demo；
- Preview retention；
- Hotspot；
- Undo / Redo；
- numeric scrub；
- Parameter Panel；
- Raw `.par`；
- dark plot；
- contextual Inspector。

## Plotfile

- Real 1D HDF5；
- field enumeration；
- LineVis；
- sample Inspector；
- invalid recovery。

Real Preview 不能破坏 Real Plotfile Viewer。

---

# 44. Automated checks

最终执行：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

如果 authoritative Preview helper 有自己的 integration tests：

```text
运行其明确的 preview-only tests
```

但不要自动重跑完整 CPU/CUDA scientific baseline，除非用户另行批准。

---

# 45. Phase 2D acceptance

只有全部满足才通过：

1. M0 证明存在 approved authoritative init-only Preview interface；
2. Protocol 1.3 正确；
3. Preview Profile 为 Host-owned；
4. Browser 不能控制 executable/argv/cwd/env；
5. Preview 使用当前 serialized Working Copy；
6. Dirty config 不需要 Save 也可 Preview；
7. Preview 不修改 project `.par`；
8. runner 不进入 timestep simulation；
9. runner 不产生正式 plot/checkpoint；
10. Preview identity 包含 request/config/build provenance；
11. old response 不能覆盖 new revision；
12. Cancel 有效且安全；
13. timeout 有效；
14. malformed/nonfinite response 被拒绝；
15. Real fields 来自 authoritative response，不写死；
16. 1D Real Preview 显示真实返回曲线；
17. Inspector 显示真实 sample / source / units when provided；
18. parameter edit → stale；
19. successful update → current；
20. failed/cancelled Preview 保留 last successful image；
21. tracked source needs-build 时 Real Preview 禁用；
22. full freshness unknown 被诚实显示；
23. disk config 与 unsaved preview input 分离；
24. Mock / Plotfile / Build / Config lifecycle 不退化；
25. 不实现 x_pos graphical binding；
26. 不实现 Cellular 2D；
27. 不实现 AMR；
28. 不运行正式 simulation；
29. 不复制/重写 scientific physics；
30. tests / host tests / lint / typecheck / build / diff check 通过。

---

# 46. Completion report

成功完成时生成：

```text
studio/PHASE2D_REAL_IC_PREVIEW_REPORT.md
```

至少包括：

```text
Baseline
M0 authoritative Preview Contract audit
Protocol 1.3
Preview Profile
Scientific call path
Working Copy transport
No-simulation evidence
Preview schema
Provenance
Cancellation / timeout
Revision race handling
Real Sod UAT
Unsaved Working Copy UAT
Failure UAT
Security
Regression
Automated checks
Deferred
Remaining issues
```

必须明确：

```text
哪些数据是 Core authoritative
哪些信息是 configured
哪些 metadata unavailable
```

---

# 47. M0 Stop-Gap report

如果无法继续，则生成：

```text
studio/PHASE2D_CORE_PREVIEW_CONTRACT_GAP.md
```

并停止。

此时不创建假的 Phase 2D success tag。

可以创建一个 audit commit，但不能标记：

```text
studio-phase2d-v0.8.0
```

为完成版本。

---

# 48. Commit / tag

真正完成后建议：

```text
feat(studio): integrate real ARCH initial-condition preview
```

建议 tag：

```text
studio-phase2d-v0.8.0
```

不要自动 push。

报告：

- commit hash；
- tag；
- worktree status；
- test summary；
- Preview runner contract；
- Real Sod UAT；
- remaining issues。

然后：

> **STOP。**

不要自动进入 Phase 2E。

---

# 49. Deferred to Phase 2E

完整参数 metadata 与 graphical binding：

```text
Core default parameter discovery
explicit/default source
unit
description
validation metadata

Sod x_pos vertical marker
parameter ↔ graphical handle
drag = one Undo
authoritative min/max

2D Initial Preview
Cellular
radius/interface/direction binding
```

Phase 2D 不提前实现。

---

# 50. Final rules

```text
Same Setup()+Init()
>
Look-alike physics
```

```text
Unsaved Working Copy preview
>
Implicit Save
```

```text
Request + Config + Build provenance
>
“Latest response wins”
```

```text
Last successful Preview retained
>
Blank screen on failure
```

```text
Authoritative fields
>
Hard-coded scientific assumptions
```

```text
M0 Stop Gate
>
Invent a missing Core API
```

```text
Real Sod 1D first
>
Sod + Cellular + AMR all at once
```

```text
Finish Phase 2D
>
“顺手做 graphical binding”
```
