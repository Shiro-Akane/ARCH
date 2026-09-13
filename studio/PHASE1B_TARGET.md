# ARCH Studio — Phase 1B Real `.par` Working Copy & Parameter UX Target

> 目标：在已经完成并冻结的 Phase 1A 真实 Plotfile 查看链路基础上，接入 **真实 ARCH `.par` 配置文件的读取、Working Copy / 工作副本、编辑与安全导出**，并完成 Parameter Panel 的第一轮正式 UX 调整。  
> 本阶段不进入 `Setup()+Init()`、Initial Condition Preview、Build/Run、AMR、SSH 或远程执行。

---

# 0. 基线与分支

Phase 1A 已完成并封箱，基线 tag：

```text
studio-phase1a-v0.2.0
```

开始前必须：

1. `git status` 确认 clean；
2. `git rev-parse studio-phase1a-v0.2.0` 记录实际 commit hash；
3. 确认 Phase 0 Mock 与 Phase 1A Real Plotfile 功能仍可运行；
4. 从 tag 创建新分支：

```bash
git switch -c studio/phase1b-config studio-phase1a-v0.2.0
```

5. 不在 Phase 0 / Phase 1A 分支继续开发。

---

# 1. Phase 1B 一句话目标

实现：

```text
真实 ARCH .par
      ↓
ParDocument / Parser
      ↓
GUI Working Copy
      ↓
Parameter Panel
      ↓
Dirty / Invalid
      ↓
Revert / Save As
```

同时保留：

```text
Mock Preview
Real 1D Plotfile Viewer
```

全部可用。

本阶段重点不是科学求解，而是：

> **让 ARCH Studio 第一次真正读取和编辑 ARCH 的运行配置，同时不破坏原文件、不丢未知参数、不伪造科学语义。**

---

# 2. 首先审计真实 `.par` 格式

在写 Parser 之前，先只读检查：

- ARCH 配置解析实现；
- 仓库内代表性 `.par`；
- 标准参数；
- custom parameters；
- comment / 空行；
- bool / enum / int / float / string；
- scientific notation；
- duplicate key 的实际行为；
- unknown key 的实际行为。

不要从常见 INI/TOML/YAML 经验猜测 ARCH `.par` 语法。

需要在 `studio/STATUS.md` 中记录：

```text
Observed .par grammar:
Known value forms:
Comment syntax:
Duplicate-key behavior:
Unknown-key behavior:
Representative fixtures:
```

如果某个语法行为无法从 ARCH 源码或实际 fixture 确认：

> **不要自行定义；标记为 unsupported / ambiguous 并报告。**

---

# 3. `.par` 文档模型必须 Round-trip Safe

不要只做：

```text
.par
→ JS object
→ 重新生成整个 .par
```

因为这种方式容易：

- 丢 comments；
- 丢 unknown keys；
- 改参数顺序；
- 改空行；
- 改用户自定义格式；
- 改 scientific notation；
- 意外删除未来版本参数。

推荐建立两层：

```text
ParDocument
├─ raw lines / tokens
├─ comments
├─ blank lines
├─ key/value entries
└─ unknown / uneditable lines

Semantic Working Copy
├─ known core parameters
└─ custom parameters
```

编辑已识别参数时：

> **只改变该参数对应的 value token，尽量保留其他原始文本。**

最低要求：

- comments 保留；
- blank lines 保留；
- key order 保留；
- unknown keys 保留；
- unknown lines 不删除；
- 未编辑值尽量保持原文本；
- 不自动排序参数；
- 不自动格式化整个文件。

---

# 4. Working Copy / 工作副本

真实 `.par` 打开后：

```text
disk/file .par
      ↓
Load
      ↓
ParDocument
      ↓
GUI working copy
```

编辑期间：

```text
原文件不变
```

状态：

```text
ConfigState:
saved
dirty
invalid
```

任何有效参数改变：

```text
saved → dirty
```

如果当前 Preview 与配置有关：

```text
PreviewState → stale
```

但 Phase 1B 不调用真实 `Setup()+Init()`。

---

# 5. Parameter Panel UX — Core Parameters 改为常驻

当前 UI 的：

```text
Grid
EOS
Network
Runtime
```

需要点击 disclosure / 展开箭头才能看到内容，操作频率过高。

Phase 1B 调整为：

## Desktop / Tablet 主布局

当左侧 Parameter Panel 处于正常桌面/平板宽度时：

> **Core Parameters 默认常驻展开，不需要用户逐项点击 Grid / EOS / Network / Runtime。**

也就是类似：

```text
CONFIGURATION

GRID
  Geometry        [...]
  Resolution      [...]
  ...

EOS
  Type            [...]

NETWORK
  Network         [...]

RUNTIME
  Backend         [...]
  tmax            [...]
  ...
```

要求：

- Grid / EOS / Network / Runtime 的参数内容直接可见；
- 使用 compact section header / 紧凑分组标题；
- 不为每个 Core section 增加多余 card 嵌套；
- 保持 Preview 仍为视觉中心；
- 左栏宽度不要因为常驻展开无限增长。

## 空间不足时

如果内容高度超过可用空间：

> **优先让 Parameter Panel 自身纵向滚动，不要因为内容变多就强迫用户重新进入多层 accordion。**

只有在：

- 手机；
- 极窄窗口；
- 明显不适合持续展开的布局；

才允许使用 collapse 作为 responsive fallback / 响应式退化方案。

### Custom Parameters

`Custom Params` 可以继续：

- 分组；
- 折叠；
- 搜索；

因为 case-specific 参数数量可能很大。

**Core 常驻，Custom 可折叠。**

---

# 6. 0–1 Range 参数使用 Slider，但必须有明确 Range 依据

对于**参数合法范围明确为 `[0, 1]`** 的数值参数，改为：

```text
Parameter Name

[────────●────────]   [0.500]
0                       1
```

也就是：

> **Slider + Numeric Input 同时存在。**

不要只提供 Slider，因为科学配置可能需要精确输入。

## 双向绑定

```text
拖 Slider
→ Numeric Input 更新
→ Working Copy 更新
→ Dirty
```

```text
输入 Numeric Value
→ Slider 更新
→ Working Copy 更新
→ Dirty
```

## 非常重要：不能按“当前值在 0–1”自动判断

以下逻辑禁止：

```text
if currentValue >= 0 && currentValue <= 1:
    render slider
```

因为一个：

```text
temperature = 0.5
```

并不表示 temperature 的合法范围就是 `[0,1]`。

只有当以下来源之一明确说明 range：

1. ARCH 已存在的 parameter contract / 参数契约；
2. ARCH 源码中明确的 validation range；
3. 已验证的 UI metadata；
4. Phase 1B 内经过人工确认的最小 core parameter schema；

才允许：

```text
min = 0
max = 1
control = slider + number
```

不能自行猜范围。

---

# 7. Slider 精度与行为

对 `[0,1]` 参数：

- Slider 必须支持键盘操作；
- Number Input 始终可以直接输入；
- clamp / reject 行为必须与真实参数 contract 一致；
- 不要静默把用户输入的非法值改成另一个值；
- 非法值应进入 `invalid`，并显示原因；
- step 不能统一写死为 `0.01`，除非 range metadata 明确允许；
- 如果没有明确 step，number input 保持高精度，slider 可以只作为粗调控件。

例如：

```text
slider → 快速调整
number → 精确输入
```

这是优先设计。

---

# 8. Core Parameter Control 映射

如果真实类型与语义已经确认，可以使用：

```text
boolean
→ switch / checkbox

enum
→ select

integer
→ number input

float with known [0,1] range
→ slider + number input

float with other known finite range
→ number input
→ slider 是否启用留待 metadata 规则明确后决定

string / path / unknown
→ text input 或只读显示
```

不要凭 key 名称猜类型。

---

# 9. Custom Parameters

真实 `.par` 中未知于 Studio schema、但 ARCH 保留的 key：

> **必须保留。**

UI 可以显示：

```text
CUSTOM PARAMS
  custom_key_a    [...]
  custom_key_b    [...]
```

但如果没有 metadata：

- 不猜 unit；
- 不猜 role；
- 不猜 range；
- 不猜 slider；
- 不猜科学含义。

默认使用最保守控件：

```text
text / numeric input
```

并保留 raw key。

---

# 10. 参数来源标识

建议让参数在必要时能够区分：

```text
CORE
CUSTOM
UNKNOWN / RAW
```

不要求每一行都显示标签，避免视觉噪音。

可以在：

- tooltip；
- section；
- inspector；
- subtle badge；

中表达。

---

# 11. Open `.par`

新增：

```text
Open Config...
```

要求：

- 只读打开用户选择的 `.par`；
- 不自动扫描用户磁盘；
- 不修改文件；
- 显示 filename；
- 成功后建立 Working Copy；
- 解析失败显示可恢复错误；
- 打开第二个文件前不允许旧异步请求覆盖新结果。

如果浏览器文件 API 只能取得 `File`：

> 不要伪装成已经拥有可写磁盘句柄。

---

# 12. Save / Save As / Revert — 浏览器能力必须诚实

## Revert

必须真正工作：

```text
working copy
→ 最近一次 loaded/saved snapshot
```

## Save

只有在当前运行环境真正拥有 writable file handle 时才允许做：

```text
Save in place
```

如果当前浏览器模式没有安全、可靠的原文件写权限：

> **不要做假的 Save。**

可以：

- 将 `Save` 显示为 disabled / unavailable；
- 或明确显示 `Save As...`。

## Save As

Phase 1B 至少需要支持：

```text
Working Copy
→ serialized .par
→ 用户显式导出/下载新文件
```

文件名默认可基于源文件，例如：

```text
MyCase.par
→ MyCase_modified.par
```

不要静默覆盖原文件。

---

# 13. Serialization / 序列化

生成文件前必须检查：

- 当前 Config 不为 invalid；
- unknown lines 仍存在；
- comments 仍存在；
- key order 未无故改变；
- 未编辑值尽量保持原文本；
- 编辑过的值可被 ARCH parser 正确读取。

Phase 1B 应建立 round-trip tests：

```text
load → no edit → serialize
```

结果应与原文件尽量字节一致；若无法完全一致，必须解释具体差异且不能改变语义。

还要测试：

```text
load
→ edit one key
→ serialize
```

除对应 value 外，不应出现大面积无关 diff。

---

# 14. Duplicate / Ambiguous Keys

如果 ARCH 的 duplicate-key 行为明确：

- 按真实 contract 处理；
- UI 必须能说明正在编辑哪一个实际生效值。

如果行为未明确：

> 不要自行决定 first-wins / last-wins。

应：

```text
Config = invalid / ambiguous
```

并提示用户。

---

# 15. Validation 边界

Phase 1B 只做：

- syntax；
- type；
- explicit min/max；
- enum membership；
- required field；
- duplicate / ambiguity；
- NaN / Inf；
- 明确的软件 contract。

不做：

- EOS 科学判断；
- mass fraction 科学判断；
- hotspot 是否“物理合理”；
- Setup/Init；
- simulation validation。

---

# 16. UI Dirty / Stale 行为

真实 `.par`：

```text
Load
→ Config saved
```

修改：

```text
Config dirty
```

如果 Mock Preview 当前存在：

```text
Preview stale
```

但：

> Phase 1B 不允许把 Mock Preview 说成真实 `.par` 对应的 Initial Condition。

建议真实 Config 模式下明确：

```text
Real config loaded
Preview provider: Mock / not connected to ARCH initializer
```

防止用户误解。

---

# 17. Phase 1A Real Plotfile 功能不得破坏

必须回归：

- Open Plotfile；
- real metadata；
- field enumeration；
- LineVis；
- real Inspector；
- error recovery；
- Mock ↔ Plotfile switching。

Phase 1B 参数 UI 重构不得让 Plotfile Viewer 退化。

---

# 18. 这阶段明确禁止

Phase 1B 禁止：

- 修改 ARCH Core；
- 修改 `case.cpp`；
- `Setup()+Init()`；
- `ARCH --preview`；
- PreviewExtractor；
- 真实 IC Preview；
- 2D AMR reconstruction；
- Build / CMake / Ninja；
- Start / Stop / Restart；
- stdout/stderr monitor；
- SSH；
- Slurm / PBS；
- Tauri / Electron；
- remote API；
- 3D volume；
- 自动科学判断。

---

# 19. UI Visual Cleanup — 本阶段允许的小范围调整

允许：

- Core section 常驻；
- Slider + number input；
- 控件间距；
- section header；
- focus / hover；
- invalid / dirty visual state；
- Parameter Panel 内部滚动；
- responsive fallback。

不允许借机：

- 全面重做主题；
- 改 Preview Renderer 风格；
- 大规模重构 App layout；
- 换 UI framework；
- 为“更漂亮”增加大型依赖。

---

# 20. Milestones

严格顺序：

```text
P1B-M0  branch + .par grammar audit
P1B-M1  round-trip-safe ParDocument parser
P1B-M2  real .par open + working copy
P1B-M3  Core Parameter Panel persistent layout
P1B-M4  explicit-range slider + numeric control
P1B-M5  Custom Params + validation
P1B-M6  Revert + Save As + serialization tests
P1B-M7  Phase 0 / 1A regression + responsive QA
P1B-M8  completion report + checkpoint
```

每个 Milestone：

1. 完成；
2. 测试；
3. 更新 `studio/STATUS.md`；
4. Scope Review；
5. 再继续。

---

# 21. 最低测试矩阵

至少覆盖：

## Parser

- representative real `.par`；
- comments；
- blank lines；
- unknown keys；
- scientific notation；
- bool / enum / int / float / string（按真实语法）；
- malformed line；
- duplicate behavior；
- no-edit round trip；
- single-edit minimal diff。

## State

- open → saved；
- edit → dirty；
- invalid input → invalid；
- Revert；
- open second file；
- stale request protection。

## UI

- Core sections desktop 常驻；
- 左栏内容超过高度后可滚动；
- Custom Params 可折叠；
- known `[0,1]` range → slider + number；
- value 只是落在 0–1、但没有 range metadata → **不能自动 slider**；
- slider / number 双向同步；
- invalid input 不静默 clamp。

## Regression

- Phase 0 Mock；
- Phase 1A real Plotfile；
- Inspector；
- tests/lint/typecheck/build。

---

# 22. Phase 1B 验收标准

完成后用户必须可以：

1. 从 `studio-phase1a-v0.2.0` 派生的新 branch 启动；
2. 打开真实 ARCH `.par`；
3. 显示真实 filename；
4. Core 与 Custom parameters 可区分；
5. comments / unknown keys / order 不被丢弃；
6. 修改参数后 Config 立即 Dirty；
7. invalid 参数有明确提示；
8. Revert 恢复；
9. Save As 导出有效 `.par`；
10. no-edit round trip 不产生无意义改写；
11. 单参数编辑不产生大面积 diff；
12. Grid / EOS / Network / Runtime 在正常桌面布局中**不需要逐项点击展开**；
13. 左栏空间不足时可滚动或使用明确 responsive fallback；
14. `[0,1]` 明确 range 参数显示 slider + numeric input；
15. slider 与 numeric input 双向同步；
16. 没有 range metadata 的 0–1 当前值**不会被误判成 slider**；
17. Custom parameter 不猜 unit / range / role；
18. Phase 0 Mock 功能仍正常；
19. Phase 1A real Plotfile 功能仍正常；
20. tests / lint / typecheck / build 通过；
21. ARCH Core 无修改；
22. 没有 Setup/Init、Build/Run、SSH、AMR；
23. 更新 `studio/STATUS.md`；
24. 提交 Phase 1B checkpoint；
25. 停止，不自动进入下一阶段。

---

# 23. Completion / 封箱

完成后：

1. `npm test`
2. `npm run lint`
3. `npm run typecheck`
4. `npm run build`
5. round-trip fixture audit
6. `git diff` audit
7. dependency/license audit
8. 编写：

```text
studio/PHASE1B_COMPLETION_REPORT.md
```

9. commit 建议：

```text
feat(studio): integrate real ARCH parameter working copy
```

10. tag 建议：

```text
studio-phase1b-v0.3.0
```

11. 报告 commit hash / tag / tests / known limitations；
12. **停止。**

---

# 24. Phase 1B 最终原则

```text
Core parameters visible
>
Repeated expand clicks
```

```text
Slider + precise number
>
Slider only
```

```text
Explicit range metadata
>
Guessing from current value
```

```text
Round-trip preservation
>
Pretty reformatting
```

```text
Working copy
>
Direct disk mutation
```

```text
Unknown parameter preserved
>
Unknown parameter deleted
```

```text
Real config editing
>
Fake scientific preview
```

```text
Finish Phase 1B
>
“顺手接 Setup/Init”
```

