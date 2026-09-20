# ARCH Studio — Phase 2E Remaining Requirement Closure Target

> **当前唯一目标 / Active target only**
>
> Phase 2D `Real Initial Condition Preview` 已封箱：
>
> ```text
> branch: studio/phase2d-api-integration
> tag: studio-phase2d-v0.8.0
> commit: 43b381c3068824a373dd5477a92dc41627efca49
> ```
>
> 本阶段的目标不是继续扩基础设施，而是**收尾组长最初《ARCH Studio：本地文件接入与初始预览需求》中尚未完成的核心交互闭环**。
>
> Phase 2E 分为两个顺序执行的子阶段：
>
> ```text
> Phase 2E-A
> Parameter Metadata + Sod Graphical Binding
>
> Phase 2E-B
> CellularDet 2D Initial Preview
> ```
>
> **必须先完成并验收 2E-A，再进入 2E-B。**
>
> 本阶段不做：
>
> ```text
> SSH / scheduler / remote
> full AMR hierarchy reconstruction
> simulation monitoring
> arbitrary editor launch
> generic filesystem browser
> automatic parameter inference from C++
> frontend physics reimplementation
> ```

---

# 0. 当前完成度

严格按组长原始“本地文件接入与初始预览”需求计算，目前主线约完成：

```text
约 75%–85%
```

已经完成的核心链：

```text
Project / Local Host
        ✅
.par Working Copy
        ✅
Save / Save As / Revert / conflict
        ✅
Controlled Build
        ✅
Build Manifest / binary provenance
        ✅
Real Sod 1D Initial Preview
        ✅
Unsaved .par Preview
        ✅
Field switch / Inspector
        ✅
Cancel / timeout / revision race
        ✅
old-preview retention
        ✅
```

剩余主要集中在：

```text
Parameter Metadata
Sod x_pos graphical binding
CellularDet 2D real preview
2D Inspector / heatmap interaction
最后一轮 original-requirement continuity UAT
```

另外还有若干次要产品化项：

```text
recent projects
native/open-in-editor integration
full dependency freshness
portable build/preview profile setup
complete parameter source/unit/description coverage
```

这些不是当前 Phase 2E 的主要阻断项。

---

# 1. 最终主流程

Phase 2E 完成后，组长原始需求中的核心路径应达到：

```text
打开项目
→ 选择源码与 .par
→ 查看显式值 / 默认值 / 来源 / 单位 / 约束
→ 修改参数
→ 按需 Build
→ 生成真实 Initial Preview
→ 图上检查数值
→ 图上拖动已绑定参数
→ Working Copy 同步
→ 再次真实 Preview
→ Save
```

并覆盖：

```text
Sod 1D
+
CellularDet 2D
```

---

# 2. Baseline

任何修改前确认：

```bash
git status
git branch --show-current
git rev-parse HEAD
git describe --tags --always --dirty
git rev-parse studio-phase2d-v0.8.0^{commit}
```

要求：

- baseline 必须是 `43b381c3068824a373dd5477a92dc41627efca49`；
- working tree clean；
- 不移动 Phase 2D tag；
- 不 reset / discard 用户工作；
- 从 Phase 2D checkpoint 创建独立分支。

建议：

```text
studio/phase2e-metadata-binding-2d
```

---

# 3. P2E-M0：先审计现有 Core contract

在写代码前先只读检查：

```text
src/api/**
src/core/**
src/interface/**
simulation/Sod/**
simulation/CellularDet/**
```

以及当前远端：

```text
review/studio-v0.4.2
studio/phase2d-api-integration
```

需要确认：

```text
1. Core 是否已提供 Parameter Metadata API
2. Core 是否已提供 Parameter Binding API
3. x_pos 是否已有 authoritative axis-position binding
4. CellularDet 是否已有 2D Preview implementation
5. CellularDet 的 case ID / dimension / geometry
6. shock_dir / radiusPerturb / noiseAmplitude 的真实含义
7. 2D sampling / shape / order / axes contract
8. 哪些单位 / ranges / defaults / validation 是 authoritative
```

不要因为 Studio 已存在 TS 类型：

```text
ParameterMetadata
ParameterBinding
```

就假定 Core 已经实现对应 API。

---

# 4. M0 Stop Gate

如果 Core 仍未提供：

```text
Parameter Metadata
Graphical Binding
CellularDet 2D Preview
```

则不要在 Studio 猜。

生成：

```text
studio/PHASE2E_CORE_CONTRACT_GAP.md
```

其中分为两个独立 contract：

```text
A. Parameter Metadata + Sod Binding
B. CellularDet 2D Preview
```

然后停止，等待 Core 方确认。

如果两者只有一个缺失：

- 有的部分继续；
- 缺失部分保持 Stop Gate；
- 不阻塞另一个已经批准的部分。

---

# 5. Phase 2E-A — Parameter Metadata

目标：

> Studio 参数面板不再只显示“文件里有什么”，而能显示 Core 确认的实际值、默认值、来源、单位、说明和约束。

建议 contract：

```ts
interface ParameterMetadata {
  key: string;
  displayName?: string | null;
  description?: string | null;
  unit?: string | null;

  type:
    | "number"
    | "integer"
    | "boolean"
    | "enum"
    | "string";

  source:
    | "explicit"
    | "default"
    | "derived"
    | "unavailable";

  explicitValue?: unknown;
  effectiveValue?: unknown;
  defaultValue?: unknown;

  constraints?: {
    min?: number | null;
    max?: number | null;
    step?: number | null;
    allowedValues?: string[] | null;
  };

  diagnostics?: Array<{
    severity: "info" | "warning" | "error";
    message: string;
  }>;
}
```

实际结构以 Core approved contract 为准。

---

# 6. Metadata 来源规则

绝对禁止：

```text
Studio 根据 key 名猜 unit
Studio 根据当前值猜 min/max
Studio 根据 case.cpp regex 猜 default
Studio 把 UI schema 当 Core truth
```

允许：

```text
Core 返回 default
Core 返回 effective value
Core 返回 unit
Core 返回 description
Core 返回 validation
```

如果 Core 没提供：

```text
Default unavailable
Unit not provided
Description unavailable
Range unavailable
```

---

# 7. Explicit / Default 行为

组长原需求要求：

```text
文件中写了 → explicit
文件中没写但 Core 有默认 → default
修改 default 参数 → 成为 explicit Working Copy parameter
```

例如：

```text
x_pos

effective: 0.5
source: default
```

用户修改为：

```text
0.35
```

则 Working Copy 应增加：

```text
x_pos = 0.35
```

并变成：

```text
source: explicit
Config: Dirty
Preview: Stale
```

不允许 silently 修改其他参数。

---

# 8. Metadata 与 Working Copy 分离

Core metadata 是：

```text
authoritative parameter knowledge
```

Working Copy 是：

```text
user editable text
```

两者不能合并成一份不可逆 state。

必须保留：

```text
loaded text
working text
effective metadata
saved snapshot
```

---

# 9. Parameter Inspector

右侧 Inspector 对选中参数显示：

```text
Key
Display name
Current Working value
Effective value
Loaded value
Saved value
Source
Default
Unit
Description
Constraints
Diagnostics
```

没有的信息明确显示 unavailable。

不要制造空白或默认假值。

---

# 10. Phase 2E-A — Sod x_pos Binding

目标：

```text
x_pos 参数
↕
1D Real IC 图上的竖向 marker
```

Binding 必须来自 Core：

```ts
interface ParameterBinding {
  parameterKey: string;
  kind: "axis-position";
  axis: "x1";
  min: number;
  max: number;
  clamping?: "none" | "preview-only" | "edit";
}
```

实际结构以 Core contract 为准。

---

# 11. x_pos marker

真实 Sod Preview 上叠加：

```text
             │
             │ x_pos = 0.35
─────────────│────────────
```

要求：

- marker 使用当前 Working Copy value；
- marker 与 Real Preview curve 分离；
- 参数修改后 marker 立即移动；
- curve 保持 stale 直到 Update Preview；
- marker 显示为 pending / stale 状态；
- 不因为 marker 移动就伪造新 Real Preview。

---

# 12. x_pos drag

拖动流程：

```text
mouse down
→ drag candidate
→ marker follows pointer
→ mouse up
→ one Working Copy edit
→ one Undo entry
→ Config Dirty
→ Preview Stale
```

要求：

- 整次拖动只生成一个 Undo step；
- 不在每个 pointermove 写入 Undo history；
- Inspector 数值实时显示候选值；
- mouseup 后形成正式 Working Copy 修改；
- Escape / cancel drag 恢复原值；
- 不自动 Save；
- 不自动 Preview。

---

# 13. Drag 精度

Marker 显示可根据像素连续移动，但最终参数必须：

```text
使用真实 axis coordinate
```

不要：

```text
0..1 normalized UI value
```

代替实际 x1 坐标。

精确输入仍保留。

---

# 14. Binding range

可拖范围由 Core authoritative binding 提供：

```text
min
max
```

不要用：

```text
当前 plot viewport
```

作为科学参数范围。

如果 viewport zoom：

```text
marker 的科学坐标不变
```

---

# 15. Invalid manual input

用户精确输入：

```text
x_pos = outside valid range
```

要求：

- 输入保留；
- Config Invalid / Warning；
- marker 可以显示 out-of-range indication 或隐藏；
- 不 silently clamp；
- Update Preview 由 Core 返回真实诊断；
- Working Copy 不丢。

---

# 16. 2E-A UAT

至少验证：

```text
Core metadata load
default parameter
explicit parameter
default → explicit edit
unit unavailable
range unavailable
x_pos marker initial position
textbox edit → marker move
drag → textbox sync
drag = one Undo
Undo restores original marker/value
Preview stale during edit
Update Preview → Current
invalid manual x_pos retained
Core failure retains old preview
```

---

# 17. Phase 2E-A acceptance

2E-A 只有满足以下才完成：

1. Metadata 来自 Core；
2. 默认值/来源可区分；
3. 缺失 metadata 不推断；
4. default 修改后成为 explicit Working Copy；
5. x_pos binding 来自 Core；
6. marker 使用真实 x1 coordinate；
7. textbox → marker 同步；
8. drag → parameter 同步；
9. 一次 drag = 一次 Undo；
10. drag 不自动 Save；
11. drag 不自动 Real Preview；
12. stale/current 语义正确；
13. invalid input 保留；
14. existing Phase 2D Preview 不退化；
15. existing config/build lifecycle 不退化。

---

# 18. Phase 2E-B — CellularDet 2D Preview

只有 2E-A 完成后进入。

目标：

```text
CellularDet
→ authoritative Setup/Init
→ 2D Cartesian sample
→ field heatmap
→ 2D Inspector
```

第一版仅支持：

```text
dimension = 2
geometry = cartesian
```

不做 3D。

---

# 19. CellularDet authoritative case ID

不要硬编码 Studio 自己猜的名称。

Core 必须返回或确认：

```text
caseId = CellularDet
```

或真实 registry ID。

Host Preview Profile 只接受 approved case ID。

---

# 20. 2D request

建议：

```text
ARCH --preview CellularDet --config-stdin
  --samples-x 256
  --samples-y 256
```

或者 Core approved 等价形式。

Browser 不控制 arbitrary argv。

Host-owned Preview Profile 控制：

```text
case
dimension
sample limits
```

---

# 21. 2D schema

建议扩展 Core Preview 1.x：

```ts
data: {
  dimension: 2,
  kind: "heatmap",

  sampling: {
    kind: "uniform",
    valueLocation: "init-sample",
    shape: [ny, nx],
    order: "x1-fastest"
  },

  axes: [
    { name: "x1", values: [...] },
    { name: "x2", values: [...] }
  ],

  fields: [
    {
      key: "DENS",
      values: [...],
      min: ...,
      max: ...
    }
  ]
}
```

实际 schema 以 Core approved contract 为准。

---

# 22. shape / order 必须明确

2D 最危险的不是 Heatmap，而是：

```text
array flatten order
```

Core 必须明确：

```text
shape = [ny, nx]
order = x1-fastest
```

或其他真实定义。

Studio 不猜。

---

# 23. 2D sample limit

设明确上限。

例如：

```text
default 256 × 256
max 512 × 512
```

具体由 Core/Host contract 决定。

同时保留：

```text
response size bound
field count bound
timeout
```

---

# 24. 2D field display

字段全部来自 Core。

要求：

```text
field selector
heatmap
colorbar
min/max
zoom
pan
fit
```

不硬编码 Density。

---

# 25. 2D Inspector

点击 heatmap：

```text
x1
x2
sample i/j
selected field value
optional all field values
unit
source
provenance
```

point mapping 必须尊重：

```text
axes
shape
order
```

---

# 26. 2D provenance

与 Sod 相同：

```text
requestId
configRevision
buildId
binary SHA256
caseId
profileId
sampling
generatedAt
```

Current 定义不改变。

---

# 27. Cellular parameter behavior

组长原需求提到：

```text
radiusPerturb
shock_dir
```

但 Phase 2E-B 第一目标是：

```text
真实 2D Preview
```

不是马上做复杂 graphical binding。

如果 Core 已经同时提供 authoritative binding：

可以接。

否则：

```text
2D Preview 先完成
graphical marker later
```

---

# 28. shock_dir

如果 Core 已确认：

```text
shock_dir = 0 / 1 / 2
```

对应不同方向：

Studio 可以显示 enum label。

如果语义未确认：

```text
保留原始数值
```

不要猜：

```text
0=x
1=y
2=z
```

除非 Core contract 明确。

---

# 29. radiusPerturb

如果 Core 明确它对应：

```text
interface / perturbation radius / position
```

可以未来显示 marker。

如果 contract 只返回 metadata：

先只显示 metadata。

---

# 30. 2D revision / cancellation

沿用 Phase 2D：

```text
old request cannot overwrite new Working Copy
cancel keeps previous image
failure keeps previous image
build change → stale
config change → stale
```

不要为 2D 新建另一套状态机。

---

# 31. 2D UAT

至少：

```text
Open CellularDet config
Build ready
Generate Real 2D Preview
switch fields
zoom/pan/fit
click Inspector
edit parameter unsaved
old image stale
Update Preview
new data current
cancel
revision race
invalid config
failure retains old image
disk config unchanged
no simulation output
```

---

# 32. Phase 2E-B acceptance

必须：

1. authoritative CellularDet case；
2. Real Setup/Init data；
3. 2D Cartesian；
4. shape/order 明确；
5. field selector；
6. heatmap；
7. colorbar；
8. zoom/pan/fit；
9. point Inspector；
10. unsaved Working Copy；
11. cancellation；
12. race protection；
13. previous image retention；
14. no simulation；
15. no project config write；
16. no frontend physics；
17. Sod 1D 不退化。

---

# 33. Original requirement closure review

2E-A 与 2E-B 完成后，进行一次组长原需求逐项核对。

至少输出：

```text
Implemented
Partially implemented
Deferred
Not applicable
```

覆盖：

```text
Project startup
.cpp source
Build
.par lifecycle
parameter metadata
Initial Preview
Sod binding
Cellular 2D
state messages
failure continuity
```

生成：

```text
studio/ORIGINAL_REQUIREMENT_CLOSURE_REPORT.md
```

---

# 34. 明确保留的 Deferred

以下不是 Phase 2E blocker：

```text
Open in external editor
Recent Projects UI polish
Native Windows Host qualification
Build cancellation
full compiler log persistence
full dependency authority
SSH / cluster / scheduler
actual AMR hierarchy reconstruction
3D Preview
simulation monitoring
in-situ visualization
```

这些进入后续 Phase 3 / Productization。

---

# 35. Regression

必须继续通过：

```text
Phase 2B config lifecycle
Phase 2C Build
Phase 2D Real Sod Preview
Mock
Plotfile
Undo/Redo
numeric scrub
Inspector
security tests
```

---

# 36. Automated checks

每个子阶段最终执行：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

如果 Core 新增对应 tests：

```text
只运行其明确 metadata/binding/preview scoped tests
```

不要擅自跑完整 simulation baseline。

---

# 37. Milestones

```text
P2E-M0   Core contract audit / Stop Gate

P2E-A1   metadata schema
P2E-A2   metadata Host transport
P2E-A3   parameter Inspector
P2E-A4   default → explicit editing
P2E-A5   Sod x_pos binding
P2E-A6   drag / Undo / stale integration
P2E-A7   A regression + UAT

P2E-B1   CellularDet Core Preview contract
P2E-B2   2D Host schema validation
P2E-B3   Real 2D provider
P2E-B4   Heatmap + field selector
P2E-B5   2D Inspector / interaction
P2E-B6   cancel/race/error retention
P2E-B7   B regression + UAT

P2E-MF   original requirement closure report
```

---

# 38. Checkpoints

建议两个 checkpoint。

## 2E-A

```text
commit:
feat(studio): add core parameter metadata and Sod graphical binding

tag:
studio-phase2e-a-v0.9.0
```

## 2E-B

```text
commit:
feat(studio): add CellularDet 2D initial preview

tag:
studio-phase2e-b-v0.10.0
```

不要自动 push。

每个 checkpoint 后停止一次并报告。

---

# 39. Completion report

最终生成：

```text
studio/PHASE2E_COMPLETION_REPORT.md
studio/ORIGINAL_REQUIREMENT_CLOSURE_REPORT.md
```

包含：

```text
Baseline
Core contracts
Metadata
Binding
Sod UAT
CellularDet 2D UAT
Security
Regression
Remaining deferred
Original requirement completion matrix
```

---

# 40. Final rules

```text
Core metadata
>
Studio inference
```

```text
Authoritative binding
>
Parameter-name heuristics
```

```text
One drag = one Undo
>
Hundreds of history entries
```

```text
2D shape/order contract
>
Guess flattened arrays
```

```text
Real Setup/Init
>
Look-alike 2D physics
```

```text
2E-A first
>
Metadata + Binding + 2D all at once
```

```text
Original requirement closure
>
Endless feature expansion
```
