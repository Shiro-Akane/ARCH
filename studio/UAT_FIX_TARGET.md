# ARCH Studio — Phase 1C.2 Manual UAT Fix Pack Target

> 目标：基于第三方视角的 Manual UAT / 手动用户验收结果，对 ARCH Studio 当前已经存在的 UI / UX 做一次集中修复。
> 本阶段重点是 **发现性、控件一致性、上下文反馈、参数编辑效率、数据来源辨识和视觉舒适度**。
> 不增加新的科学计算能力，不进入 `Setup()+Init()`、真实 IC Preview、Build/Run、AMR、SSH、Tauri 或远程执行。

---

# 0. 冻结基线

Phase 1C.1 已完成并封箱，基线 tag：

```text
studio-phase1c1-v0.4.1
```

开始前必须：

1. `git status` 确认 clean；
2. `git rev-parse studio-phase1c1-v0.4.1` 记录实际 commit hash；
3. 确认以下功能仍正常：
   - Phase 0 Mock Preview；
   - Phase 1A Real Plotfile Viewer；
   - Phase 1B Real `.par` Working Copy；
   - Phase 1C Parameter Panel 信息架构；
   - Phase 1C.1 自定义 Open Config / enum / bool / numeric control；
4. 从 tag 创建新分支：

```bash
git switch -c studio/phase1c2-uat-fixes studio-phase1c1-v0.4.1
```

5. 不在已封箱分支继续开发。

---

# 1. 一句话目标

把当前版本从：

> **“自动测试通过、开发者能理解的前端”**

推进到：

> **“第一次接触 ARCH Studio 的第三方用户，也能理解当前模式、知道下一步该做什么，并且不会被 Mock / Real / 浏览器默认行为误导的工具 UI”**

本阶段来自真实 UAT，不允许重新发散为新功能开发。

---

# 2. UAT Issue 总表

本阶段需要处理：

```text
UAT-01  模式发现性不清楚：Real Config / Real Plotfile / Cellular / Mock 的输入需求不明确
UAT-02  Empty Preview 的提示位置和下一动作不够明确
UAT-03  Real Plotfile 仍暴露浏览器原生中文文件选择 UI
UAT-04  Real Plotfile 的 Field selector 仍是浏览器默认白色控件
UAT-05  Inspector 在不同模式下显示大量无效占位字段
UAT-06  Scientific plot 白底与深色应用亮度割裂
UAT-07  ARCH Logo 有可点击 cursor / affordance，但点击无行为
UAT-08  1D Result / Line Plot 对非 CFD 用户缺乏解释
UAT-09  Provenance 有价值，但数据处理长说明应降级到详情
UAT-10  Mock Temperature 左低右高的 X gradient 容易误导 Hotspot 语义
UAT-11  Mock Pressure 半黄半紫虽符合 step field，但缺少解释
UAT-12  Mock hotspot_temperature 命名像物理温度，但实际更接近 demo intensity
UAT-13  Mock Hotspot X/Y 缺少适合的 [0,1] Slider + Numeric Input
UAT-14  Preview 是高频操作，但只有窗口左下角全局入口
UAT-15  字体层级偏小，尤其 Inspector 数值和辅助文本
UAT-16  Slider / scrub 连续编辑会导致 Preview 频繁闪烁刷新
UAT-17  缺少 application-level Undo / Redo
UAT-18  Real Config 模式下 Inspector 缺乏有效上下文
UAT-19  长参数面板滚动时 sticky Core Navigator 占用过多垂直空间
UAT-20  Numeric field 缺少 DCC / Unreal-style horizontal scrub interaction
```

另外：

```text
Browser-native 右键中文 Context Menu
```

明确记录为：

> **浏览器 / Edge 原生行为，不作为 ARCH Studio Bug。**

不要为了统一英文而全局禁用右键菜单。

---

# 3. Mode Discovery / 模式发现性

右上角模式：

```text
Real Config
Real Plotfile
Cellular · real result
Hotspot · Mock demo
```

必须让第三方用户一眼理解每种模式需要什么输入。

## 3.1 模式摘要

Mode selector 附近或切换后的 Empty State，显示极短说明：

### Real Config

```text
REAL CONFIG

Edit an ARCH .par working copy.
Requires a local .par file.
```

### Real Plotfile

```text
REAL PLOTFILE

Inspect a completed ARCH plotfile.
Requires a local .h5 file.
```

### Cellular · real result

```text
REAL DATA · BUILT-IN SAMPLE

Read-only archived result.
No local file required.
```

### Hotspot · Mock demo

```text
MOCK / DEMO

Illustrative local demo.
No ARCH file required.
```

不需要写长说明，只要把“是否需要文件 / 是否真实数据 / 是否只读”讲清楚。

---

# 4. Empty Preview State 居中

当前：

```text
No preview generated
```

偏左上，不像 Editor Viewport 的 Empty State。

改为 Preview 画布视觉中心：

```text
        [ Preview icon ]

     No preview generated

Edit parameters, then generate a preview.

      [ Generate Preview ]
```

要求：

- 文案和按钮在主 Preview 区视觉中心；
- 不改变 Preview 实际尺寸；
- Empty State 不能看起来像普通网页正文；
- 如果 Preview = stale，则显示：

```text
Preview out of date

[ Update Preview ]
```

---

# 5. Contextual Preview Action

底部全局：

```text
Preview
```

可以继续保留。

但因为：

```text
Edit → Stale → Preview → Inspect
```

是高频循环，Preview 区必须增加上下文动作。

状态建议：

### Current

```text
Preview: Current
```

不需要高亮按钮。

### Stale

```text
Preview: Stale      [ Update Preview ]
```

### Generating

```text
Generating…
```

按钮 disabled。

### Failed

```text
Preview failed      [ Retry Preview ]
```

### No Preview

画布中央：

```text
[ Generate Preview ]
```

Preview 属于整个 Config，不是 Hotspot 专属动作，因此不要塞到 `Hotspot (4)` 标题右边。

---

# 6. Real Plotfile 文件选择产品化

当前 Real Plotfile 仍显示浏览器原生：

```text
选择文件
未选择任何文件
```

必须和 Real Config 一致。

隐藏原生 file input 的可见 UI，仅作为底层系统 picker。

改为：

### 未加载

```text
No plotfile loaded

[ Open Plotfile... ]
```

### 已加载

```text
sod-1d.h5

[ Open Plotfile... ]
```

系统 File Picker 可以使用 OS locale；ARCH Studio 自己的 UI 保持英文。

---

# 7. Real Plotfile Field Selector 统一视觉

当前：

```text
Field [ DENS ▼ ]
```

仍是浏览器默认白色 Select。

改为和顶部模式选择器、Geometry、Boundary 同一套 ARCH Studio Select 样式。

要求：

- 使用现有 select component / CSS；
- 不复制新的 UI framework；
- 不改变 field enumeration；
- 保留 keyboard accessibility；
- unknown/raw field 仍正常显示。

---

# 8. Context-sensitive Inspector / 上下文 Inspector

右侧 Inspector 不再永久展示：

```text
x
y
Density
Temperature
Pressure
Composition
AMR level
—
—
—
```

## 8.1 Mock Preview

只显示当前真实可用内容：

```text
Coordinates
x
y

Field values
Density
Temperature
Pressure
```

Composition / AMR 如果 Mock 没建模：

> 默认不显示。

可以在轻量 info 中说明 Mock 不包含这些数据。

## 8.2 Real Plotfile

显示：

```text
File
Time
Dimension
Geometry
Field
Min
Max

Selected sample
x
Value
```

不显示无关 rho/T/P 固定占位。

## 8.3 Real Config

当用户点击左侧某个参数时，Inspector 改成 **Parameter Inspector**：

```text
Parameter
Friendly label
Raw key
Current value
Type
Source line
Allowed values / range
Validation
Raw token
```

只有已有 schema / source contract 支持的信息才显示。

如果没有选中参数：

```text
Select a parameter to inspect its details.
```

不要显示 Mock 点位 Inspector。

## 8.4 未来 Real IC

本阶段不实现，只保留架构空间，不预放空字段。

---

# 9. Typography / 字体层级

当前 Inspector 数值、辅助文案偏小。

建立最低可读层级：

```text
Primary UI / parameter labels:
13–14 px minimum

Inspector values:
13–14 px minimum

Section titles:
14–16 px

Secondary metadata:
12 px minimum

Tertiary debug/source info:
11–12 px
```

要求：

- 不全局粗暴 scale 125%；
- 不显著减少 Preview 面积；
- 高 DPI 下仍易读；
- 1280×720 仍可用；
- 1920×1080 优先验收。

---

# 10. Plot Theme / 绘图区主题

纯白科学绘图区在深色 ARCH Studio 中过亮。

## 默认屏幕模式

优先提供：

```text
Dark Plot
```

要求：

- Plot background 与应用深色体系协调；
- axis / tick / grid / text 保持高对比可读；
- 数据 colormap 本身不因背景主题改变数值映射；
- 不更改 min/max；
- 不改变数据值；
- 不通过压暗数据来“伪暗色”。

## Paper / Light

如果 H5Web / 当前图表实现成本合理，可保留：

```text
Plot theme:
Dark | Paper
```

用于截图、打印、论文风格图表。

如果增加 theme toggle 会明显扩大实现范围：

> Phase 1C.2 只实现 Dark Plot 默认样式，Paper 留作 later enhancement。

---

# 11. ARCH Logo Affordance

当前 ARCH Logo：

- hover / cursor 暗示可点击；
- 点击无反应。

本阶段没有 Home 页面。

因此：

> **移除 pointer / clickable semantics。**

变成纯品牌 Logo。

不要为了“让点击有反应”临时发明 Home 页面。

---

# 12. 1D Result / Line Plot 解释

对 Real Plotfile / Cellular 1D result，增加短说明：

```text
1D Profile
Values sampled along the X axis.
```

不要写 CFD 教科书。

目标只是解释为什么这里是线，而不是 Heatmap。

如果存在真实 units，则继续显示真实 units。

---

# 13. Provenance / Source Details

`Source and provenance` 本身保留。

这是科研软件有价值的信息：

- source file；
- SHA-256；
- field source；
- coordinate source；
- file unchanged；
- import mode。

但类似：

```text
Native nonuniform cell-center samples...
No smoothing...
No physical-model reconstruction...
```

默认降级到：

```text
Data details / Source and provenance
```

折叠区域。

主图附近只保留最短必要状态。

---

# 14. Mock Temperature 语义修正

当前 Mock Temperature：

```text
radial hotspot
+
X directional gradient
```

导致：

```text
left low → right high
```

虽然符合早期 Mock Provider 测试设计，但会让用户误以为 Hotspot 本身不对称。

Phase 1C.2 改为更自解释的 Mock：

```text
uniform ambient
+
radially symmetric hotspot
```

要求：

- hotspot center 仍由 hotspot_x / hotspot_y 控制；
- radius 仍由 hotspot_radius 控制；
- 不额外叠加无说明的方向性 gradient；
- 必须明确仍是 Mock / Demo，不是 ARCH scientific result。

可以让 Density / Temperature 的 falloff / amplitude 不同，以便 Field Switching 仍能看出差别。

---

# 15. Mock Pressure 说明

Pressure 当前 step / shock-like field：

```text
left high
right low
```

这是早期 Mock 设计，不是 Renderer 错误。

可以保留，但必须明确标注：

```text
Pressure · illustrative step / shock field
```

或者 tooltip：

```text
Mock pressure uses an illustrative step field to exercise field switching.
```

不要让用户自己猜“为什么半黄半紫”。

---

# 16. Mock `hotspot_temperature` 命名

当前：

```text
hotspot_temperature = 1
```

容易让用户理解为物理温度 = 1。

但 Mock 里它实际更接近示意 amplitude / intensity。

本阶段优先 UI friendly label 改为：

```text
Hotspot intensity
```

底层 key 可以继续：

```text
hotspot_temperature
```

Raw key 放 tooltip/info：

```text
Raw key: hotspot_temperature
Mock-only illustrative amplitude.
```

不要修改真实 `.par` schema。

---

# 17. Mock Hotspot Slider

对于 Mock domain 已明确：

```text
x ∈ [0,1]
y ∈ [0,1]
```

因此：

```text
hotspot_x
hotspot_y
```

改为：

```text
Slider + Numeric Input
```

例如：

```text
Hotspot X
0 ─────●──── 1    [0.500]

Hotspot Y
0 ─────●──── 1    [0.500]
```

要求：

- 双向同步；
- Numeric 保持精确输入；
- 不静默 clamp 非法值；
- 使用 Mock schema 明确范围，不影响 Real Config slider policy。

## Radius

只有当 Mock Provider 已明确 radius 合法范围时才增加 Slider，否则保持 Numeric Input。

## Intensity

如果 Mock Provider 没有明确有限范围，则保持 Numeric Input。

---

# 18. Slider / Numeric 连续编辑不重建 Preview

这是本阶段的高优先级修复。

当前拖动 Slider 过程中 Preview 有频繁闪烁。

正确行为：

```text
drag slider / scrub numeric
       ↓
Working Copy 连续更新
       ↓
Config = Dirty
Preview = Stale
       ↓
旧 Preview 继续显示
       ↓
不自动反复重建 Renderer
```

只有：

```text
Generate / Update Preview
```

才重新生成结果。

如果保留可选 live preview：

- 必须显式 enable；
- 100–250 ms debounce；
- 不能每 pointermove 重建；
- 不作为默认 Phase 1C.2 行为。

---

# 19. Preview 切换过渡

在根因修复以后，可以增加非常轻的视觉过渡：

```text
100–150 ms crossfade
```

用于：

```text
old preview
→ new preview
```

但 Crossfade 只能作为 polish，不能掩盖每个 input event 都在重算的问题。

如果增加动画会明显复杂化 H5Web integration：

> 可以跳过，先保证无频闪。

---

# 20. Undo / Redo

实现 application-level parameter history。

快捷键：

```text
Ctrl+Z
→ Undo

Ctrl+Y
或 Ctrl+Shift+Z
→ Redo
```

只记录 **用户数据编辑**：

- numeric edit；
- slider edit；
- scrub edit；
- enum；
- bool；
- custom parameter edit。

不记录：

- block navigation；
- Raw view 展开；
- tooltip；
- field selector；
- mode selector；
- plot pan / zoom；
- inspector point selection。

## Drag 合并

一次完整 Slider / Scrub drag：

```text
mouse down
→ many intermediate values
→ mouse up
```

只能产生 **一个 Undo step**。

不能每个像素产生一个 history entry。

## Revert

`Revert` 仍然表示：

```text
回到 loaded / saved snapshot
```

不能被 Undo 语义替代。

---

# 21. Numeric Scrub / DCC-style Drag

所有明确 numeric 的可编辑字段，增加 DCC / Unreal-style scrub：

```text
hover numeric
→ cursor: ew-resize / horizontal resize

mouse down + horizontal drag
→ adjust value

click without meaningful drag
→ enter normal text editing
```

## Drag threshold

建议：

```text
3–5 px
```

以内视为点击。

## Fine adjustment

```text
Shift + Drag
→ fine adjustment
```

可使用普通速度的：

```text
0.1×
```

如果 contract 没有明确 step：

- 不擅自使用 `±1`；
- 使用相对 sensitivity；
- Numeric Input 始终可精确输入。

## Known integer

明确 integer：

- scrub 结果保持 integer；
- contract 明确 step=1 才用 1 作为逻辑步进；
- 合法 sentinel，例如 `-1`，必须保留。

## Known bounded float

有 min/max：

- scrub 可遵守真实范围；
- 不超范围。

## Unbounded float

- 不猜 clamp；
- sensitivity 应与当前 magnitude 合理相关；
- scientific notation 仍可直接输入。

## Cancel drag

```text
Esc
→ restore drag-start value
```

如实现成本合理。

## Pointer Capture

使用 Pointer Events / pointer capture，避免鼠标拖出控件导致 drag 丢失。

---

# 22. Geometry vs Dimension

当前：

```text
cartesian
cylindrical
spherical
```

是：

> **Geometry / coordinate geometry**

不是 Dimension。

因此现有 Dropdown 是合理的。

不要改成：

```text
[1D][2D][3D]
```

## Dimension 控件

只有确认 ARCH contract 表明 dimension 本身是用户可编辑参数时，才考虑：

```text
[ 1D ] [ 2D ] [ 3D ]
```

Segmented Control。

如果 dimension 是由 case / binary / config topology 推导：

> 只显示，不允许编辑。

Phase 1C.2 不擅自改变 Dimension contract。

---

# 23. Sticky Core Navigator 紧凑态

当前长参数页滚到底后：

```text
Grid
EOS
Network
Runtime
```

仍以四行 sticky 形式占据较多纵向空间。

保留 sticky navigation 的价值，但增加 compact sticky state。

## 顶部正常状态

```text
Grid       cartesian · 1D
EOS        ideal
Network    Disabled
Runtime    tmax 0.15
```

## Scroll past navigator 后

缩成：

```text
[ Grid ] [ EOS ] [ Network ] [ Runtime ]
```

一行或最多两行紧凑导航。

要求：

- 不丢失随时切 block 能力；
- Raw `.par` 展开时尤其减少占用；
- 不新增复杂滚动容器；
- 不造成 layout jump 明显闪烁。

如果 compact sticky 实现明显风险高：

> 可以留到 M 后段，但不能直接删除 sticky navigator。

---

# 24. Right-click Browser Menu

Edge / 浏览器原生中文菜单不属于 ARCH Studio UI。

本阶段：

- 不全局禁用 contextmenu；
- 不伪造自定义菜单；
- 不为统一英文而拦截浏览器行为。

记录为 known environment behavior。

---

# 25. Save As Manual Acceptance

Phase 1C.1 自动环境仍未确认真实下载落盘。

本阶段修复后必须再次手动验证：

```text
Edit .par
→ Save As...
→ Windows Downloads
→ file exists
→ content matches Working Copy
→ original file unchanged
```

这条由人工 UAT 验收，不用 Codex 用“Download requested”代替通过证据。

---

# 26. UI 状态文案

避免实现细节、长开发日志、大量 mock disclaimer 长期占据主 UI。

主界面状态保持简短：

```text
REAL CONFIG
MOCK / DEMO
REAL PLOTFILE
REAL DATA

Saved
Dirty
Invalid

Current
Stale
Generating
Failed
```

详细解释放：

- tooltip；
- info；
- provenance；
- Raw；
- temporary toast。

---

# 27. 明确禁止

本阶段禁止：

- Setup / Init；
- Real IC Preview；
- PreviewExtractor；
- AMR reconstruction；
- Build / Run；
- CMake / Ninja；
- SSH / Slurm；
- Tauri / Electron；
- Remote；
- 3D；
- 修改 ARCH Core；
- 修改 `.par` parser grammar；
- 修改 round-trip serializer contract；
- 猜科学 range / unit / enum；
- 新大型 UI framework；
- 移动端专用 UX。

---

# 28. Milestones

严格顺序：

```text
UATF-M0   branch + UAT issue inventory
UATF-M1   mode discovery + empty states
UATF-M2   Plotfile file/select control unification
UATF-M3   contextual Inspector
UATF-M4   typography + plot theme
UATF-M5   Mock semantic cleanup
UATF-M6   contextual Preview action + no-flicker state flow
UATF-M7   Undo / Redo
UATF-M8   numeric scrub
UATF-M9   compact sticky navigator + minor affordance fixes
UATF-M10  provenance / 1D explanation cleanup
UATF-M11  full regression + Manual UAT rerun
UATF-M12  completion report + checkpoint
```

每个 Milestone：

1. 完成；
2. 自动测试；
3. 更新 `studio/STATUS.md`；
4. Scope Review；
5. 再继续。

---

# 29. 自动测试最低要求

新增 / 更新测试覆盖：

## Mode / Empty

- 各 mode 空状态说明正确；
- Real Config / Plotfile 不混淆输入要求；
- Mock 无需文件。

## Inspector

- Mock 不显示无效 Composition / AMR；
- Plotfile 只显示实际 file/field/sample；
- Real Config parameter selection → parameter inspector；
- 无 selection → empty contextual state。

## Mock

- Temperature 无 X directional gradient；
- hotspot center / radius 正确；
- Pressure step field 有明确 label；
- Hotspot X/Y Slider 双向同步。

## Preview

- slider edit → stale；
- slider move 不触发 repeated preview generation；
- Generate / Update Preview 单次触发；
- stale/current/generating/failed 状态正确。

## Undo

- numeric edit undo/redo；
- enum/bool undo/redo；
- one drag = one undo step；
- navigation 不进入 history；
- Revert 与 Undo 语义不混淆。

## Scrub

- click → text edit；
- drag → numeric change；
- Shift drag fine；
- bounded/unbounded/integer 行为；
- invalid / special sentinel 保持。

## Regression

- Phase 0 Mock；
- Phase 1A Plotfile；
- Phase 1B `.par`；
- Phase 1C panel；
- Phase 1C.1 control semantics。

---

# 30. Desktop Manual UAT

完成自动测试后必须再次以第三方视角手动测试：

目标窗口：

```text
1280×720
1920×1080
2560×1440
```

重点手动检查：

1. 第一次打开能否理解模式；
2. Open Config / Plotfile 是否统一；
3. Real Config Inspector 是否有用；
4. Plot theme 是否不刺眼；
5. Plotfile / Cellular 1D 图是否能理解；
6. Hotspot Slider / Numeric 是否直观；
7. Slider / scrub 是否无频闪；
8. Ctrl+Z / Redo 是否符合预期；
9. Preview Update 按钮是否在正确上下文；
10. Save As 是否真实落盘；
11. Raw / Provenance 是否不抢主视觉；
12. Logo 不再假装可点击；
13. 字体是否无需眯眼。

---

# 31. UAT Fix 验收标准

至少满足：

1. 四个 mode 的输入需求可理解；
2. Empty Preview 居中；
3. Empty / Stale Preview 有上下文 Preview action；
4. Plotfile 不再显示原生中文 file input；
5. Plotfile Field selector 与 Studio Select 一致；
6. Inspector 按 mode / context 动态变化；
7. 无数据字段不永久占位；
8. 默认 plot 不再是刺眼纯白，或提供合理 Dark Plot；
9. Logo 没有无效点击 affordance；
10. 1D Result 有简短解释；
11. Provenance 保持折叠；
12. Mock Temperature 对称 hotspot；
13. Mock Pressure 明确说明 step/shock；
14. hotspot_temperature UI 不再误导为物理温度；
15. Hotspot X/Y 有 [0,1] Slider + Numeric；
16. slider / scrub 不反复刷新 Preview；
17. Ctrl+Z / Redo 生效；
18. one drag = one Undo step；
19. numeric scrub 支持点击输入 + 水平拖动；
20. sticky navigator 滚动后更紧凑；
21. 字体层级改善；
22. Save As 实际 Windows 落盘人工确认；
23. Mock / Plotfile / Config 旧功能不退化；
24. tests / lint / typecheck / build 通过；
25. ARCH Core 无修改；
26. 不进入下一科学阶段。

---

# 32. Completion

完成后：

1. `npm test`
2. `npm run lint`
3. `npm run typecheck`
4. `npm run build`
5. Manual UAT rerun
6. Save As disk delivery 手动确认
7. before / after screenshots
8. `git diff` audit
9. 编写：

```text
studio/MANUAL_UAT_FIX_REPORT.md
```

报告至少包括：

- 每条 UAT Issue 的处理结果；
- Fixed / Accepted / Deferred；
- 仍存在的 known limitations；
- 自动测试；
- 手动 UAT；
- Save As 落盘证据；
- before / after UI。

commit 建议：

```text
fix(studio): apply manual UAT usability improvements
```

tag 建议：

```text
studio-uat-fix-v0.4.2
```

提交后：

> **停止，不自动进入 Real IC Preview / Phase 2。**

---

# 33. 最终原则

```text
Third-party comprehension
>
Developer familiarity
```

```text
Contextual actions
>
Global buttons far from the task
```

```text
Stable preview
>
Flickering live updates
```

```text
Undoable editing
>
Fear of experimentation
```

```text
DCC-style numeric workflow
>
Browser-number-input behavior
```

```text
Contextual Inspector
>
Permanent empty fields
```

```text
Dark workstation comfort
>
White-canvas glare
```

```text
Mock self-explanation
>
“Programmer knows why it looks like that”
```

```text
Fix UAT issues
>
Add new scientific features
```
