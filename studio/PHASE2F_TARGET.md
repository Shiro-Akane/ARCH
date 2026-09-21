# ARCH Studio — Phase 2F Target
## Studio UI Contract Integration & Usability Closure

> **唯一目标**
>
> Studio 基线：`studio-phase2e-b-v0.10.0` / `fb22178fe578b17120597633f427e38d2a2be582`
>
> Core UI contract：`5e96d4f004c9bd320fb232853d59b90006cdb0f2`
>
> 当前 Studio 已包含 Preview 基础、Core A、Core B。**本轮只接 `5e96d4f0`，不重复 A/B，不 merge main，不进入 Phase 3。**

---

# 1. 本轮性质

Phase 2E 已完成真实 Sod 1D、`x_pos` binding 与 CellularDet 2D Preview。

Phase 2F 不增加新的科学模型，目标是完成 2026-09-20 UI Review 中的 13 项问题，使用户不会因为界面状态或显示错误而：

- 认错模型；
- 改错 `.par`；
- 使用旧模型 metadata；
- 把 Setup 前解析值误认为模型最终采用值；
- 在缩放后看到与坐标轴不一致的图；
- 因缺少默认项、单位、坐标语义或类型校验而误操作。

分三段：

```text
Phase 2F-A — Core Configuration Contract + Identity Safety
Phase 2F-B — Complete Parameter Editor + Coordinates / Units
Phase 2F-C — Preview Interaction + Plot Presentation + Documentation
```

必须严格：

```text
A → checkpoint/STOP → B → checkpoint/STOP → C → final closure/STOP
```

---

# 2. Git / baseline

开始前：

```bash
git status
git branch --show-current
git rev-parse HEAD
git rev-parse studio-phase2e-b-v0.10.0^{commit}
git fetch origin
git rev-parse origin/codex/studio-core-ui-contracts
```

确认：

```text
Studio = fb22178fe578b17120597633f427e38d2a2be582
Core UI = 5e96d4f004c9bd320fb232853d59b90006cdb0f2
```

新分支建议：

```text
studio/phase2f-ui-contract-integration
```

先做：

```bash
git cherry-pick --no-commit 5e96d4f004c9bd320fb232853d59b90006cdb0f2
```

若 Core/API/CMake/scientific 文件发生实际冲突，停止并报告。

禁止：

```text
merge main
rebase main
重复 cherry-pick 40b7704d / 47517d1c / 91a46f8f
reset/stash 用户工作
自行重写 scientific Core
```

---

# 3. Core 新接口

本轮新增：

```text
ARCH --config-schema

ARCH --inspect-config CASE --config-stdin --request-id <id>
```

`--config-schema` 提供 RuntimeParams 当前 **90 个标准键**，包括：

```text
type
group
defaultValue/defaultSource
constraints
options
aliasOf
applicability
path
units
```

边界：

```text
standardParametersComplete = true
customParametersComplete = false
constraintsComplete = false
```

`--inspect-config` 接收当前未保存 Working Copy，返回：

```text
parsedValue
rawValue
valueSource
sourceKey
defaultValue
applicable
path
units
coordinates
resolved
diagnostics
```

它发生在 Setup 之前，不等价于模型最终实际采用值。

---

# 4. 三层值必须分开

UI 必须明确区分：

```text
Schema Default
= Core 标准参数解析缺省值

Inspection Parsed Value
= 当前 Working Copy 经 RuntimeParams 解析后的输入值
= Setup 前

Preview Effective / Source
= 模型 Setup 真正读取到的值及来源
= 只有现有 Preview metadata 覆盖时才存在
```

禁止把三者合并成一个模糊的 `Current Value`。

没有 Preview metadata 时：

```text
Model effective value: unavailable
```

不能拿 inspection parsed value 冒充。

---

# 5. Core verification gate

cherry-pick 后先不改 UI。

重新编译 CPU Preview，并运行 8 组 scoped tests：

```text
configuration_api_contract
mainline_authority
preview_initial_conversion
preview_api_contract
preview_parameter_reads
preview_parameter_metadata
preview_sampling_limits
preview_cellular_2d
```

要求全部 PASS；simulation oracle disabled；不跑完整 simulation，不跑 CUDA baseline。

验证真实：

```text
--config-schema
--inspect-config Sod
--inspect-config CellularDet
strict int/float parsing
coordinate metadata
units
field diagnostics
configRevision / request identity
```

Gate 通过后才进入 2F-A。

---

# 6. Build tracked inputs

至少增加：

```text
src/core/StandardParameters.h
src/api/Configuration.h
src/api/Configuration.cpp
src/api/PresentationMetadata.cpp
src/api/LogCapture.h
```

继续覆盖：

```text
RuntimeParams.h
ConfigParser.h
Preview.cpp
Response.h
main.cpp
策略注册表
坐标定义依赖
```

不要因此宣称 `dependenciesComplete=true`。

---

# 7. Phase 2F-A — Identity Safety

对应优先问题：

```text
UI-08 模型/参数文件身份
UI-09 旧模型 metadata 隔离
UI-10 Source / Model 同步
UI-13 统一类型校验基础
```

## A1. Host configuration adapter

新增受控 Host 调用：

```text
config schema
config inspection
```

Frontend 只传：

```text
projectId
approved caseId
Working Copy text
revision
```

浏览器不得提供 program/argv/cwd/env/shell。

Inspection response 至少按以下身份隔离：

```text
projectId
caseId
configRevision
build/binary identity
requestId
```

迟到结果不得覆盖当前状态。

## A2. Metadata isolation

Sod 成功 Preview 后切换 CellularDet：

- Sod `x_pos` metadata 可以保留在历史 Preview；
- 不能出现在 Cellular 参数默认项；
- 不能提供 Cellular 编辑入口；
- 不能显示 Sod binding；
- 不能驱动新模型 Inspector。

作用域最低要求：

```text
project + case/model + build/binary
```

实际读取结果还需要匹配：

```text
configRevision
```

## A3. Source / Model

当前已支持：

```text
Sod -> simulation/Sod/Sod.cpp
CellularDet -> simulation/Cellular/Cellular.cpp
```

Preview Model 切换时 Source 同步。

若 Source 面板是 project-level build source，则必须明确标注，不得让用户误认为它一定是当前 Preview 模型源码。

## A4. 常驻身份

左上角稳定显示：

```text
当前模型：CellularDet
参数文件：CellularPreview2D.par
```

辅助显示：

```text
源码路径
参数文件完整路径
```

不要只放 tooltip。

## A5. 配对疑点

例如：

```text
Current Model: CellularDet
Parameter File: Sod.par
```

只显示黄色 warning，不永久禁止编辑/Preview。

禁止推断：

```text
文件名前缀一致 = 已验证
目录一致 = 已验证
目录不同 = 错误
```

通用 `1.par/test.par` 使用中性“关联未确认”。

Preview confirmation 与 overwrite confirmation 分离。

## A6. 标准输入严格校验

所有标准参数入口统一使用 schema 类型：

```text
已有项
新插入默认项
paste
scrub
graphical binding
programmatic edit
```

int 拒绝：

```text
1.5
1.0
1e2
12abc
overflow
```

float 拒绝：

```text
NaN
Infinity
suffix
overflow
```

expression 按 Core 规则支持，例如 `pi`、`2*pi`。

非法文本可留在编辑框中修正，但不能当作 validated Save/Preview 输入。

## A7. Checkpoint

UAT：

```text
Sod → CellularDet → Sod metadata isolation
model/source sync
CellularDet + Sod.par warning
generic 1.par neutral state
overwrite target confirmation
existing/new int same validation
late inspection response rejected
```

最终：

```text
Core 8 scoped groups PASS
npm test PASS
npm run test:host PASS
lint/typecheck/build PASS
git diff --check PASS
```

生成：

```text
studio/PHASE2F_A_COMPLETION_REPORT.md
```

建议 tag：

```text
studio-phase2f-a-v0.11.0
```

然后 STOP。

---

# 8. Phase 2F-B — Complete Parameter Editor

对应：

```text
UI-01 / 02 / 03 / 04 / 05 / 12 / 13
```

## B1. 参数编辑器数据源

参数面板组合：

```text
Configuration Schema
+ Working Copy
+ Inspection result
+ current-model Preview metadata
```

不再只显示 `.par` 已有 key。

所有 90 个标准参数：

```text
可查
可搜索
可编辑
```

但省略项未编辑前不得自动写入 `.par`。

## B2. 六分块

```text
Grid
EOS
Network
Gravity
Diffusion
Runtime
Custom Parameters
```

Custom 参数没有 Core metadata 时不猜：

```text
type
unit
path
range
```

## B3. Network Advanced

加入：

```text
ode_solver
linear_solver
ode_rtol
ode_atol
ode_max_newton_iter
ode_max_substeps
ode_dt_safe_fac
ode_dt_fac_max
ode_dt_fac_min
ode_initial_dt_frac
ode_use_numerical_jac
ode_freeze_jacobian
```

文件没写仍显示 Core default。

## B4. Gravity / Diffusion

Gravity：

```text
gravity_type
gravity_g_x/y/z
gravity_G
```

self gravity 未实现时如实显示 unavailable / not runnable。

Diffusion：

```text
use_diffusion
diff_integrator
diff_cfl
diff_max_stages
use_thermal_diff
use_viscous_diff
use_species_diff
nu_visc
alpha_therm
D_spec
```

Helmholtz + diffusion 禁止显式系数的组合要显示 Core field error；保留用户输入，不自动删。

## B5. Default → explicit

标准参数省略时显示 default。

只有用户实际修改后才安全插入 Working Copy。

禁止把全部 defaults 批量扩写进 `.par`。

继续保留：

```text
comments
unknown keys
order
newline convention
```

## B6. Alias

如：

```text
timeintegrator -> time_integrator
```

不显示为两个独立功能。

源文件使用 alias 时保留其真实来源语义，避免生成竞争 key。

## B7. Applicable

`applicable=false` 时：

- 仍可找到；
- 标明当前不适用；
- 已有显式值保留；
- 不自动删除；
- 不自动插入 default。

## B8. Grid 三轴固定布局

Grid 中 X/Y/Z 始终同级、固定顺序。

`blocks` 永远不进 Advanced。

规则：

```text
x1 > 0
x2/x3: 0 off, >=1 on
x3 active requires x2 active
```

inactive axis：

- blocks 可见；
- min/max/BC 收起；
- hidden Working Copy 值保留。

中途非法 `blank/-/1./1.5/negative` 不驱动维数变化；布局用最近一次有效状态。

## B9. Geometry display names

原始 key 始终 x1/x2/x3。

显示使用 Core contract：

```text
cartesian: x / x,y / x,y,z
cylindrical: r / r,phi / r,z,phi
spherical: r / r,phi / r,theta,phi
```

尤其：

```text
2D cylindrical = r–phi
```

不能自行改成 r–z。

## B10. Units

CGS：

```text
DENS g/cm^3
TEMP K
PRES erg/cm^3
VEL* cm/s
ENER erg/cm^3
EINT erg/g
length cm
time s
angle rad
```

IdealGas 使用 Core `code_*` 单位。

区分：

```text
known
dimensionless
unknown
not-applicable
coordinate-dependent
model-dependent
mixed-state
```

Sod `x_pos` 的位置单位从 binding axis 继承，不根据旧 metadata 的 null 猜。

## B11. Host path preflight

只检查 schema 明确标记为 path 的字段。

Input file：

```text
resolved path
exists
regular file
readable
```

Output target：

```text
允许不存在
检查 parent / writable / creatable
预检不创建目录
```

相对路径按 Core process working directory，不按浏览器或 `.par` 文件目录猜。

Host unavailable：

```text
Not checked / Unable to check
```

不能冒充通过/失败。

## B12. Checkpoint

UAT：

```text
90-key search
ODE defaults
Gravity
Diffusion
default→explicit
alias
applicable=false
1D→2D→3D→2D→1D
blocks 0/1/2/blank/negative/decimal
x2=0 x3=1
three geometries
CGS units
IdealGas code_* units
custom unknown unit
all path states
```

生成：

```text
studio/PHASE2F_B_COMPLETION_REPORT.md
```

建议 tag：

```text
studio-phase2f-b-v0.12.0
```

然后 STOP。

---

# 9. Phase 2F-C — Preview Interaction / Presentation

对应：

```text
UI-06 缩放
UI-07 制图设置
UI-11 README
```

## C1. 先修 zoom correctness

在新增显示设置前先复现 UI-06。

测试：

```text
Sod 有明显断面
CellularDet shock_dir 0/1
非正方形 2D
```

zoom/pan 必须同步影响：

```text
data geometry
axes
Sod marker
selection marker
click hit test
Inspector mapping
```

固定物理点在 zoom 前后 raw value 一致。

Fit/Reset 恢复完整 view。

Zoom 不修改 Config、不 Save、不 Preview。

## C2. Plot Settings 分区

两个独立区域：

```text
Coordinate Axes
Field Values
```

1D：

```text
Spatial X: Linear / Log
Field Y: Linear / Log
```

2D：

```text
Spatial X: Linear / Log
Spatial Y: Linear / Log
Color value: Linear / Log
```

不能提供一个含义不清的全局 `Log`。

## C3. Advanced range

Coordinate：

```text
Auto
Manual min/max
Reset
```

Field：

```text
Auto
Manual min/max
lower clipping
upper clipping
Reset
```

用户输入原始物理值，即便显示 Log 也不要求输入 log(value)。

## C4. Log invalid values

遇 0/负值：

- 明确提示；
- 允许切回 Linear；
- 不 silent abs；
- 不 silent epsilon；
- 不删数据。

Inspector 始终显示原始值。

## C5. Colormap

至少：

```text
Viridis
Hot
```

其他 colormap 只有图库真实支持时加入。

提供小型 gradient preview。

## C6. Clipping

下/上 clipping 可独立启用。

持续显示：

```text
Clipped
threshold
```

二维允许 colorbar edge saturation；一维裁切显示并提供边缘提示。

不修改 Core response arrays。

## C7. Display settings 与科学状态隔离

以下操作均不得：

```text
Config Dirty
写 .par
改变 configRevision
触发 Preview
修改 Inspector raw value
```

包括：

```text
zoom
pan
linear/log
range
clipping
colormap
```

## C8. README

更新 `studio/README.md` 顶部为当前真实版本：

```text
Sod 1D
Sod x_pos binding
CellularDet 2D
config-schema / inspect-config
Build
Save lifecycle
当前真实 Preview timeout
```

移走过时的：

```text
only Sod
no binding
old 30s timeout
Phase2D-only startup
```

---

# 10. Final closure

生成：

```text
studio/PHASE2F_COMPLETION_REPORT.md
studio/PHASE2F_UI_REVIEW_CLOSURE_REPORT.md
```

Closure report 对 UI-01..UI-13 每项写：

```text
Implemented
Partially implemented
Deferred with explicit reason
Not applicable
```

并附：

```text
implementation evidence
automated tests
UAT
remaining limit
```

---

# 11. Final regression

运行：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

并重新跑 Core 8 scoped groups。

不得：

```text
削弱 tests
为了绿色跳过新失败项
运行完整 simulation
运行 CUDA baseline
```

除非用户另行批准。

---

# 12. Final UAT

至少覆盖：

```text
Parameter:
omitted standard → edit → explicit → inspect → Preview → Save/Reopen

Identity:
Sod+Sod.par
CellularDet+CellularPreview2D.par
CellularDet+Sod.par
generic 1.par
model switch before response

Grid:
1D→2D→3D→2D→1D

Plot:
Sod zoom/pan/Fit
四种 1D scale 组合
manual range
clipping
Cellular zoom/pan/Fit
2D X/Y log 独立
field-value log
colormap
Inspector unchanged

Path:
valid
missing
directory-as-file
relative
new output target
Host unavailable
```

---

# 13. Final checkpoint

建议：

```text
commit:
feat(studio): complete UI configuration and preview usability integration

tag:
studio-phase2f-v0.13.0
```

不要自动 push。

完成后报告：

```text
commit
tag
branch
working tree
Core tests
Studio tests
Host tests
UAT
remaining deferred
```

然后：

> **STOP。不要自动进入 Phase 3。**

---

# 14. Explicit deferred

以下不阻塞 Phase 2F：

```text
Cellular editable marker
actual AMR hierarchy
3D Preview
new scientific models
generic arbitrary C++ auto-UI
full dependency authority
SSH / scheduler / cluster
simulation monitoring / in-situ
native Windows Host qualification
generic external editor launcher
```

---

# 15. Final rules

```text
Schema Default ≠ Inspection Parsed ≠ Preview Effective
Current model identity > old-preview convenience
Stable X/Y/Z layout > controls jumping between sections
Host filesystem truth > browser path guesses
Coordinate scale ≠ field-value scale
Display clipping ≠ scientific data mutation
Fix zoom correctness before adding more plot controls
Finish UI review closure before Phase 3
```
