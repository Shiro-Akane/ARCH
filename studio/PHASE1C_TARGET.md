# ARCH Studio — Phase 1C Parameter Panel Information Architecture & UI Cleanup Target

> 目标：在已经完成并冻结的 Phase 1B 真实 `.par` Working Copy 基线上，对左侧 **Parameter Panel / 参数面板** 做一次专门的信息架构与密度优化。  
> 本阶段只处理 **参数面板的呈现、导航、分组、可读性和空间效率**，不增加新的科学功能，不进入 `Setup()+Init()`、真实 IC Preview、Build/Run、AMR、SSH 或远程执行。

---

# 0. 基线与分支

Phase 1B 已完成并封箱，基线 tag：

```text
studio-phase1b-v0.3.0
```

开始前必须：

1. `git status` 确认 clean；
2. `git rev-parse studio-phase1b-v0.3.0` 记录实际 commit hash；
3. 确认以下功能仍正常：
   - Phase 0 Mock Preview；
   - Phase 1A Real Plotfile Viewer；
   - Phase 1B Real `.par` Working Copy；
   - Dirty / Invalid / Revert / Save As；
4. 从 tag 创建新分支：

```bash
git switch -c studio/phase1c-parameter-ui studio-phase1b-v0.3.0
```

5. 不在 Phase 1B 分支继续开发。

---

# 1. 一句话目标

把目前左侧参数栏从：

> **“真实 `.par` 的完整长表单镜像”**

调整为：

> **“Block Navigator / 参数块导航 + 当前 Block Editor / 当前参数块编辑器 + Raw 信息按需查看”**

核心目标：

```text
更少视觉噪音
更短扫描路径
更少重复展开操作
更少垂直滚动
更清楚的层级
```

同时保持：

```text
.par round-trip safety
Working Copy
Dirty / Invalid
Revert / Save As
Mock Preview
Real Plotfile
```

全部不变。

---

# 2. 参考项目与借鉴边界

本阶段允许参考以下三个开源项目，但只借鉴其 **信息架构 / UX 思路**：

## Kitware Peacock

借鉴：

- Tree / Block navigation；
- 当前 block 对应的参数编辑；
- schema-driven / 元数据驱动控件；
- 原始文本与 GUI 分离；
- 高级用户可查看 raw config。

不复制：

- 源码；
- CSS；
- 图标；
- 品牌；
- UI 文案。

## SplashFOAM

借鉴：

- task-oriented / 任务导向；
- 只显示当前任务真正相关的参数；
- 按 Geometry / Meshing / Physical / Runtime 等工作语义分组；
- 根据当前维度/模式减少无关参数。

不复制：

- 源码；
- 视觉资产；
- LGPL 代码。

## H5Web

借鉴：

- Sidebar 是辅助导航，不应吞噬中央 Visualization；
- sidebar 可保持紧凑；
- 主 Preview 持续是视觉中心；
- 复杂信息按需展开。

---

# 3. 重新定义左栏结构

当前左栏不要继续：

```text
GRID
  geometry
  nblockx1
  nblockx2
  nblockx3
  max_blocks
  x1_min
  x1_max
  x2_min
  x2_max
  ...
EOS
  ...
NETWORK
  ...
RUNTIME
  ...
CUSTOM
  ...
```

这种“全部永久摊开”的长表单形式。

改成两层：

```text
PARAMETERS

CONFIGURATION

Grid        cartesian · 1D · 32
EOS         Ideal
Network     None
Runtime     CPU · tmax ...

────────────────────────

[当前选中的 Block Editor]

────────────────────────

CUSTOM PARAMETERS
Search...
Hotspot
Composition
Other
```

也就是：

> **Block 永久可见；Block 内参数只显示当前选中项。**

---

# 4. Core Block Navigator

以下入口始终常驻：

```text
Grid
EOS
Network
Runtime
```

以后如真实 schema 需要，可以增加：

```text
Hydrodynamics
AMR
Output
Solver
```

但 Phase 1C 不应主动扩大 block 数量，只使用当前已有、已验证的 Core 分类。

每个 block 行只显示：

```text
Block Name
Compact Summary
```

例如：

```text
Grid        cartesian · 1D · 32
EOS         Ideal
Network     None
Runtime     CPU · tmax 5e-8
```

点击 `Grid`：

```text
Grid row = selected
↓
下方 Block Editor 显示 Grid 参数
```

点击 `Runtime`：

```text
Runtime row = selected
↓
Grid 参数消失
↓
Block Editor 切换为 Runtime
```

这是：

```text
selection / 选择
```

不是：

```text
accordion / 手风琴式多块同时展开
```

---

# 5. Block Summary 规则

摘要必须来自 **当前已知真实值**，不猜科学含义。

允许：

```text
Grid
cartesian · 1D · 32
```

如果这些值有明确来源。

允许：

```text
Runtime
tmax 0.15
```

如果 tmax 已存在。

不允许：

```text
Grid
High quality
```

这种主观科学判断。

如果没有适合摘要：

```text
Network
Configured
```

或：

```text
Network
—
```

不要制造信息。

---

# 6. Block Editor — 参数只显示当前 Block

Block Editor 必须保持单一焦点。

例如选择 `Grid`：

```text
GRID

Geometry
[ cartesian        ]

Dimension
1D

Resolution
[ 32 ]

Domain
X   [0.0]  —  [128.0]

Boundary
X   [outflow] [outflow]

AMR
Min level    [0]
Max level    [4]

Advanced (9) >
```

不要在同一屏继续同时显示 EOS、Network、Runtime 全部字段。

---

# 7. Dimension-aware / 维度感知

这是本阶段的重点之一。

如果当前 case 为：

```text
1D
```

则默认主编辑视图只强调：

```text
x1 / X
```

相关参数。

像：

```text
nblockx2
nblockx3
x2_min
x2_max
x3_min
x3_max
x2 boundary
x3 boundary
```

不要和 X 同级永久展示。

处理方式优先：

```text
Inactive dimensions / Other dimensions
```

放入 `Advanced` 或次级区域。

如果：

```text
2D
```

显示：

```text
X
Y
```

如果：

```text
3D
```

显示：

```text
X
Y
Z
```

不要删除原参数，只改变默认呈现层级。

---

# 8. 成对参数并排

所有天然成对的 min/max 参数，优先同一行：

```text
X Domain
[ 0.0 ] — [ 128.0 ]

Y Domain
[ 0.0 ] — [ 12.8 ]
```

不要继续：

```text
x1_min
[input]

x1_max
[input]
```

分成两大行。

要求：

- 底层仍绑定真实原始 key；
- 不改变 round-trip；
- 不因为 UI 并排而合并或改名真实 `.par` key。

---

# 9. Boundary 紧凑矩阵

Boundary 相关参数改为紧凑矩阵或双列：

```text
BOUNDARY

X   Left [outflow]   Right [outflow]
Y   Left [outflow]   Right [outflow]
Z   Left [outflow]   Right [outflow]
```

根据 dimension-aware 规则：

- 1D 默认只显示 X；
- 2D 默认 X + Y；
- 3D 显示 X + Y + Z。

其他维度仍可以在 Advanced / Raw 查看。

---

# 10. `line xx` 默认隐藏

当前：

```text
geometry · line 9
nblockx1 · line 11
...
```

视觉噪音过大。

Phase 1C 改为：

> **普通编辑视图默认不显示 line number。**

Source line 信息保留，但降级到：

- hover tooltip；
- parameter info popover；
- Raw / Source mode；
- 或辅助详情区。

例如：

```text
Geometry
[ cartesian ]
```

hover / info：

```text
Raw key: geometry
Source line: 9
Type: string/enum
```

不要删除 source line 数据，只隐藏默认展示。

---

# 11. Raw Key 与 Friendly Label

如果某个参数存在明确可验证的友好 label，可以显示：

```text
Geometry
```

并把：

```text
geometry
```

作为 raw key 放入 tooltip/info。

但：

- 不批量猜测所有 key 的自然语言意义；
- 没有可靠 label 时直接显示 raw key；
- 不改变底层 key；
- 不改变 `.par` serialization。

---

# 12. Advanced / 低频参数

每个 Core Block 可以有：

```text
Advanced (N) >
```

用于：

- 低频参数；
- 非当前维度参数；
- 内部 tuning；
- 只有高级用户需要的参数；
- source-specific 辅助参数。

Advanced 默认折叠。

但必须保证：

- 参数仍可访问；
- 不删除；
- 不改变 Working Copy；
- 搜索仍能找到。

---

# 13. Custom Parameters

Custom Parameters 不再默认整列平铺全部内容。

建议结构：

```text
CUSTOM PARAMETERS

[ Search parameters... ]

▸ Hotspot
▸ Composition
▸ Other
```

## 但分组必须谨慎

如果 ARCH 没有 metadata 明确告诉 UI 参数属于：

```text
Hotspot
Composition
Ambient
Solver
```

就不要根据名称自行永久分类。

允许的安全做法：

### 已有明确 schema / 已人工确认
可以正式分组。

### 没有 metadata
使用：

```text
All Custom Parameters
[ Search... ]
```

必要时：

```text
Other
```

不要通过字符串前缀猜科学语义并永久写死。

---

# 14. Custom Search

Custom Params 必须提供搜索。

搜索至少匹配：

- raw key；
- friendly label（如有）；
- value text（可选）。

搜索只是过滤 UI：

> 不改变文档，不删除参数。

---

# 15. GUI + Raw Source，不维护两套完整表单

本阶段不做：

```text
Simple Form
Raw Form
```

两套独立表单。

推荐：

```text
GUI
Raw .par
```

其中：

## GUI

就是本阶段的新 Block Navigator + Block Editor。

## Raw `.par`

只读优先。

允许：

- 显示完整原文件；
- line number；
- 当前 working copy；
- 语法高亮（如果不需要大型新依赖）；
- 点击参数时定位 source line（可选）。

Phase 1C **不要求 Raw 模式可编辑**。

这样避免维护两套参数编辑 UI。

---

# 16. Slider 规则保持 Phase 1B 原则

明确 range 的参数：

```text
[0,1]
```

继续：

```text
Slider + Numeric Input
```

不改变。

必须保持：

- Numeric Input 可精确输入；
- Slider 只是快速调整；
- 非法值不静默 clamp；
- 没有 range metadata 的参数不能因为当前值在 0–1 就自动 slider；
- Custom Params 不猜 range。

---

# 17. Parameter Panel 宽度

左栏保持辅助区域，不得侵占 Preview。

本阶段原则：

```text
Preview width priority
>
Parameter width expansion
```

如果某个参数名很长：

- truncate；
- tooltip；
- wrap（谨慎）；
- Raw/Info 查看。

不要为了显示完整 raw key 把左栏无限加宽。

---

# 18. Parameter Panel Scrolling

左栏允许纵向滚动，但应明显减少目前的滚动距离。

滚动策略：

```text
Block Navigator
尽量固定/紧凑

Block Editor
可滚动

Custom
在下方或独立区域
```

如实现简单，允许：

```text
整个 Parameters panel
overflow-y: auto
```

但不要产生嵌套三层滚动区域。

---

# 19. 桌面优先

目标平台：

```text
Primary:
1920×1080
2560×1440

Supported:
1280×720+
```

窄窗口只要求：

- 不严重破版；
- 不横向溢出；
- 控件可访问。

不做移动端专用 UX。

---

# 20. 保持 Preview 为视觉中心

本阶段任何 UI 优化都不能导致：

- 左栏明显变宽；
- Preview 被压缩；
- Inspector 被挤出；
- 主画面视觉权重下降。

验收截图必须包含：

```text
1280×720
1920×1080
```

对比优化前后的 Preview 占比。

---

# 21. 当前功能不得退化

必须保持：

## Phase 0
- Mock Preview；
- Dirty/Stale；
- Point Inspector；
- Save/Revert 逻辑。

## Phase 1A
- Real Plotfile；
- LineVis；
- field enumeration；
- real Inspector；
- error recovery。

## Phase 1B
- Real `.par`；
- Working Copy；
- round-trip；
- Custom preservation；
- Slider；
- Invalid；
- Revert；
- Save As。

---

# 22. 不修改 `.par` 科学语义

本阶段是 UI cleanup。

禁止：

- 新增自动参数；
- 自动删参数；
- 自动补 optional key；
- 根据 UI 结构改 `.par` 分组；
- 改参数名；
- 猜 unit；
- 猜 range；
- 猜 role；
- 猜物理意义。

UI 分类不等于文件结构变化。

---

# 23. 本阶段明确禁止

- Setup / Init；
- Initial Condition Preview；
- PreviewExtractor；
- AMR reconstruction；
- Build / Run；
- CMake / Ninja；
- SSH / Slurm；
- Tauri；
- Electron；
- 3D；
- Remote；
- 改 ARCH Core；
- 重写 Parser；
- 改 round-trip serialization；
- 大型 UI framework；
- 全面主题重做；
- 移动端专用 UX。

---

# 24. Milestones

严格按顺序：

```text
P1C-M0  branch + current UI audit
P1C-M1  Core Block Navigator
P1C-M2  Selected Block Editor
P1C-M3  dimension-aware Grid compression
P1C-M4  paired min/max + boundary matrix
P1C-M5  line/source info de-emphasis
P1C-M6  Custom search + progressive disclosure
P1C-M7  optional read-only Raw .par view
P1C-M8  regression + desktop QA
P1C-M9  completion report + checkpoint
```

如果 M7 Raw View 明显需要额外大型依赖：

> 跳过 M7，记录为 later enhancement。

不要因此扩大 Scope。

---

# 25. UI 验收标准

完成后至少满足：

1. Grid / EOS / Network / Runtime 入口始终可见；
2. 同时只显示一个 Core Block 的详细参数；
3. 切换 block 不需要 accordion 展开/收起；
4. Grid 在 1D case 中默认不平铺 X2/X3 全量字段；
5. 2D/3D 根据维度显示对应轴；
6. min/max 参数同一行配对；
7. boundary 使用紧凑布局；
8. 默认不显示 `line xx`；
9. source line 仍可通过 tooltip/info/raw 获取；
10. Advanced 参数可访问但不抢主视觉；
11. Custom Params 不默认全平铺；
12. Custom 有搜索；
13. 无 metadata 时不乱分科学组；
14. slider 行为保持 Phase 1B 规则；
15. Parameter Panel 比 Phase 1B 明显减少默认纵向长度；
16. Preview 可视面积不减少或只发生极小变化；
17. 1280×720 可用；
18. 1920×1080 可用；
19. Mock Preview 不退化；
20. Real Plotfile 不退化；
21. Real `.par` Working Copy 不退化；
22. round-trip tests 继续通过；
23. tests / lint / typecheck / build 全通过；
24. ARCH Core 无修改；
25. 不进入下一阶段。

---

# 26. 建议的视觉目标

不是：

```text
File mirror / 文件镜像
```

而是：

```text
Block navigation
+
task-relevant controls
+
progressive disclosure
+
raw source on demand
```

参数栏应感觉像：

> **专业 Editor 的属性面板**

而不是：

> **把 `.par` 每一行转换成 Input 后垂直堆叠。**

---

# 27. 完成后

完成后：

1. `npm test`
2. `npm run lint`
3. `npm run typecheck`
4. `npm run build`
5. responsive desktop screenshots
6. Phase 0 / 1A / 1B regression
7. round-trip audit
8. `git diff` audit
9. 编写：

```text
studio/PHASE1C_UI_COMPLETION_REPORT.md
```

10. commit 建议：

```text
refactor(studio): simplify parameter panel information architecture
```

11. tag 建议：

```text
studio-phase1c-v0.4.0
```

12. 报告：
    - commit hash；
    - tag；
    - tests；
    - before/after UI；
    - skipped items；
    - known limitations；
13. **停止。**

---

# 28. 最终原则

```text
Block always visible
>
All parameters always visible
```

```text
Current task
>
Raw file structure
```

```text
Progressive disclosure
>
Long form dump
```

```text
Dimension-aware
>
Showing inactive dimensions equally
```

```text
Preview space
>
Sidebar expansion
```

```text
Raw info on demand
>
line number everywhere
```

```text
Schema evidence
>
UI guessing
```

```text
UI cleanup
>
New scientific functionality
```

