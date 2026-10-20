# ARCH Studio — Phase 2H Target
## Mainline Core Synchronization & Runtime-Capability Refresh

> **当前唯一目标 / Active target**
>
> Studio 当前桌面基线：
>
> ```text
> branch: studio/phase2g-desktop-launcher
> tag: studio-phase2g-v0.16.0
> commit: e13b4bcd00fd763029a50c17b95c002d84cf780b
> ```
>
> Core UI contract 最新远端：
>
> ```text
> branch: codex/studio-core-ui-contracts
> head: 502eadcb33a9e2d3c8bd079a10dbdc8af208ef10
> synchronized main parent: 48f6d357d6b8085501d2012d70e923080bae3402
> ```
>
> `502eadcb` 本身主要是当前同步说明 / API README；真正需要纳入的是该 branch 所代表的**最新 mainline Core tree + UI contract**，不能只 cherry-pick 这一颗 docs commit。
>
> 本阶段不是 Phase 3，也不增加新的科学 Preview 模型。目标是：
>
> ```text
> 将 v0.16.0 Desktop Studio 同步到最新 Core mainline
> → 重新构建 CPU ARCH
> → 从运行中 binary 查询实际 schema / cases / capabilities
> → 适配 92 标准参数 / 14 注册模型 / 参数退役 / 新 Gravity contract
> → 保证 Session / AMR / Desktop 既有功能不退化
> ```

---

# 0. 当前 Core 事实

最新 handoff 明确：

```text
configuration-schema version = 2
standard parameters = 92
registered cases = 14
```

完整初始场图 / Initial AMR 仍只支持：

```text
Sod 1D
CellularDet 2D
```

因此不能只更新 UI 中的数字或静态列表；必须先同步 Core source + binary，再从实际 binary 查询：

```text
ARCH --config-schema
ARCH --list-cases
ARCH --preview-capabilities
```

---

# 1. 阶段拆分

Phase 2H 分三段：

```text
2H-A — Mainline Core Sync Audit + CPU Binary Revalidation
2H-B — Runtime Capability / Parameter Migration Integration
2H-C — Desktop Regression + Final Compatibility Checkpoint
```

严格：

```text
A → checkpoint / STOP
B → checkpoint / STOP
C → final checkpoint / STOP
```

禁止自动跨阶段。

---

# 2. H0 — Git / Core ownership audit

开始前：

```bash
git status
git branch --show-current
git rev-parse HEAD
git rev-parse studio-phase2g-v0.16.0^{commit}

git fetch origin
git rev-parse origin/codex/studio-core-ui-contracts
git rev-parse origin/main
```

预期：

```text
Studio baseline:
e13b4bcd00fd763029a50c17b95c002d84cf780b

Core UI branch:
502eadcb33a9e2d3c8bd079a10dbdc8af208ef10

synchronized main parent:
48f6d357d6b8085501d2012d70e923080bae3402
```

建议新分支：

```text
studio/phase2h-mainline-capability-sync
```

创建独立 audit worktree，不直接在已封箱 v0.16.0 worktree 上试验同步。

---

# 3. 不允许直接猜同步方式

当前 Studio 历史中，旧 Core UI/AMR/session 增量曾以 integration/cherry-pick 形式接入；最新 Core branch 已把这些内容同步进新的 mainline history，并继续增加主线改动。

因此：

> **不要直接 `git merge main`，也不要只 cherry-pick `502eadcb`。**

先生成：

```text
studio/PHASE2H_MAINLINE_SYNC_AUDIT.md
```

至少记录：

```text
current Studio HEAD
502ead parent
merge-base
left/right commit counts
current Core/API tree vs upstream branch tree
renamed/moved Core files
deleted files
Studio-owned paths
Core-owned paths
```

在 disposable worktree 中验证候选同步策略后再选最终方式。

---

# 4. Integration strategy acceptance rule

最终同步方式必须同时满足：

1. 不丢失 `studio/` 的 Phase 2G Desktop/Session/AMR 实现；
2. 不重复或回滚旧 Core A/B/UI/session 功能；
3. Core-owned source 最终与 `502eadcb` branch 的 authoritative tree 一致；
4. upstream 删除 / rename 的旧 Core 文件不会残留并继续被 Build 使用；
5. 不手工重写 scientific physics 来“解决冲突”；
6. 最终 diff 能明确区分：
   - upstream Core mainline sync；
   - Studio compatibility changes。

如策略不明确：

> STOP，在正式实现 worktree 修改前报告。

---

# 5. Core tree verification

同步完成后，对 Core-owned 区域做 snapshot/diff verification，至少：

```text
CMakeLists.txt
cmake/
src/
simulation/
tests/api/
tests/host/（Core-owned部分）
docs/development/
src/api/
```

`studio/` 保留 Studio 自己实现。

任何 Core scientific difference 如果不是 upstream 502 branch 本身的一部分，必须列出原因；默认不允许。

---

# 6. Phase 2H-A — Mainline Core Sync + CPU Rebuild

Core source 同步完成后，重新 configure/build 独立 CPU binary。

要求：

```text
ARCH_ENABLE_CUDA=OFF
CPU Debug / approved local profile
BUILD_TESTING=ON
```

不复用 v0.16.0 旧 CPU binary 作为验收依据。

本阶段不要求：

```text
CUDA build
GPU validation
完整 simulation
```

---

# 7. Current Core scoped gate

按最新 handoff 执行：

```bash
ctest --test-dir <current-build> \
  -R '^(preview_|configuration_api_contract|ui_expansion_contract|initialization_probe|case_inspection_contract)' \
  --output-on-failure -j 1
```

当前 handoff 预期定向组数：

```text
13/13 PASS
```

如果实际 test registry 数量改变：

- 记录真实列表；
- 不为了匹配“13”删除或跳过测试；
- 以最新 Core branch 的 handoff + CTest registry 为准。

---

# 8. Mandatory runtime capability snapshot

使用**新构建 binary**保存原始响应：

```bash
ARCH --config-schema
ARCH --list-cases
ARCH --preview-capabilities
```

保存 ignored evidence：

```text
studio/.local/phase2h/config-schema.json
studio/.local/phase2h/list-cases.json
studio/.local/phase2h/preview-capabilities.json
```

当前预期：

```text
schema version = 2
standard parameters = 92
registered models = 14
```

但 production code 不能硬编码 92 / 14 作为永久事实。

---

# 9. Registered models current snapshot

当前 handoff 中 14 个 built-in models：

```text
Sod
CellularDet
Gaussian
Sedov
RT
SmoothAdvection
GravityBox
JeansWave
ExternalGravity
DiffusionMode
BurnOneZone
BurnGradient
CooperativeHotspots
SNIaCoupled
```

实际 UI 始终以：

```text
--list-cases
```

为准。

不要维护独立静态 14-model truth table。

---

# 10. Field / AMR support boundary

即使 model registered：

```text
≠ initial field preview supported
≠ initial AMR preview supported
```

当前完整 field / Initial AMR 仍只：

```text
Sod 1D
CellularDet 2D
```

UI 继续读取每个模型 capability 和 `preview-capabilities.modelCapabilities`。

不能因为新模型注册就显示 Full Preview / AMR 按钮。

---

# 11. Phase 2H-A checkpoint

A 完成条件：

```text
Core sync strategy documented
Core-owned tree verified
new CPU ARCH built
latest targeted Core tests PASS
runtime capability snapshots saved
Build profile/manifest updated to current tree
Studio semantic migration尚未开始（除构建/路径兼容所需）
```

生成：

```text
studio/PHASE2H_A_MAINLINE_SYNC_REPORT.md
```

建议：

```text
tag: studio-phase2h-a-v0.17.0
```

**STOP。等待用户授权 B。**

---

# 12. Phase 2H-B — Runtime Capability / Parameter Migration

只在 A checkpoint 后开始。

---

# 13. 92-key catalog migration

新的 `--config-schema` 当前返回：

```text
92 standard parameters
```

Studio catalog 必须完全由当前 binary schema 驱动。

更新：

```text
catalog/search tests
group counts
omitted-default editing
schema cache identity
Build/binary identity cache
```

production 逻辑不能继续断言：

```text
90 keys
89 editors
```

测试可以对当前 binary 验证 92，但实现必须动态。

---

# 14. 新 Gravity 参数

当前新增：

```text
gravity_boundary
gravity_rtol
gravity_atol
gravity_max_cycles
```

直接消费 schema：

```text
type
default
description
options
constraints
applicability
units
```

不要复制前端默认值。

---

# 15. 五个 retired 参数

当前 retired keys：

```text
enforce_mass_conservation
burn_verbose_level
ode_use_numerical_jac
ode_freeze_jacobian
timeintegrator
```

Core inspection 返回：

```text
RETIRED_PARAMETER
```

而且它们不能通过 Custom 参数绕过。

Studio 必须：

- 不再显示为可插入标准参数；
- 不生成 default entry；
- 不允许 Custom 重新创建同名 retired key；
- 显示明确 retired / migration diagnostic。

---

# 16. Retired key document safety

如果旧 `.par` 已包含 retired key：

> **禁止 silent delete。**

推荐：

```text
load legacy file
→ preserve raw Working Copy
→ inspection RETIRED_PARAMETER
→ highlight retired key
→ current config remains invalid for new Core
→ provide explicit Remove retired parameter action
→ one Undo-able Working Copy edit
→ user explicitly Save
```

不要：

```text
读取时自动删
Save 时偷偷删
把 retired key 当 unknown custom 继续写回
```

---

# 17. Alias regression — timeintegrator

旧 UI 曾把：

```text
timeintegrator
```

作为 `time_integrator` alias。

当前它已 retired。

因此：

```text
time_integrator = canonical current key
timeintegrator = retired legacy key
```

不能继续把两者作为等价编辑 destination。

旧文件含 `timeintegrator` 必须进入 retired migration flow。

---

# 18. 新标准 timing 参数

当前 mainline catalog 还包含：

```text
dt_init
dt_min
tstep_change_factor
```

验证：

```text
可搜索
正确分组
Core default / description
首次编辑只写当前 key
```

---

# 19. Gravity capability semantics

最新 Core 中：

```text
gravity_type=self
```

生产计算能力已经扩展。

因此旧 UI 的全局：

```text
self gravity unavailable
```

必须改为当前 Core schema / applicability / inspection 的真实状态。

但是：

> **self gravity production support ≠ Gravity field Preview support。**

当前 Preview / AMR 不提供 gravity potential / acceleration field。

UI 不得凭 production gravity capability 新增这些场图。

---

# 20. JENS refinement indicator

最新 handoff 明确：

```text
JENS still unavailable
```

AMR refine_var UI 必须按当前 Core options/presentation：

- JENS 不得作为正常 supported indicator；
- 旧文件包含 JENS 时保留输入并显示 Core diagnostic；
- 不自动替换成 DENS 或其他指标。

---

# 21. 14-model discovery regression

用新 binary 查询实际 models。

新增模型不能被：

```text
旧 11-model whitelist
旧 source mapping
旧 capability cache
```

拦截。

检查：

```text
registered
inspect-case
field Preview capability
AMR capability
source evidence
unit evidence
```

模型切换继续保持 metadata/session/AMR identity isolation。

---

# 22. Source directory reorganization

最新 mainline 包含路径重组 / rename。

验证：

```text
Source View
model→source association
Build tracked inputs
Build stale detection
Desktop launcher project discovery
Core build/test paths
```

都不引用已移动旧路径。

原则：

```text
runtime binary/Core discovery
>
stale frontend hardcoded path
```

---

# 23. Build tracked input migration

检查 v0.16.0 manifest 每个 tracked path：

```text
exists?
renamed?
deleted?
still relevant?
```

旧路径不能 silent skip。

若 tracked input 不存在：

```text
profile invalid / migration required
```

更新后重新 Build 并生成新 manifest。

除非获得完整机制，仍保持：

```text
dependenciesComplete=false
```

---

# 24. Session compatibility

重新验证新 mainline binary 的：

```text
--preview-session
```

覆盖：

```text
ready
cold request
warm request
latest-only queue
cancel/restart
request-limit recycle
```

未知 optional response fields 应兼容忽略，不得破坏 v0.16 Host/Studio handler。

---

# 25. AMR compatibility

重新验证：

```text
Sod Initial AMR
CellularDet Initial AMR
resource table
limited snapshot
field+AMR identity
EOS source matching
```

Gravity mainline 更新不能让 UI 误认为 Initial AMR 支持全部 14 models。

---

# 26. Retired config UAT

创建 disposable legacy configs，分别包含：

```text
timeintegrator
ode_use_numerical_jac
burn_verbose_level
enforce_mass_conservation
ode_freeze_jacobian
```

逐个验证：

```text
load preserved
retired diagnostic visible
not custom
not silently deleted
explicit removal Undo-able
after removal inspection valid where otherwise valid
Save/Reopen does not reintroduce retired key
```

---

# 27. Phase 2H-B regression

执行：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

并重跑当前 Core targeted suite。

生成：

```text
studio/PHASE2H_B_CAPABILITY_MIGRATION_REPORT.md
```

建议：

```text
tag: studio-phase2h-b-v0.18.0
```

**STOP。等待用户授权 C。**

---

# 28. Phase 2H-C — Desktop / Workstation Compatibility Regression

C 不增加新功能，只确认 v0.16 Desktop 产品在最新 mainline Core 上仍正常。

---

# 29. Windows + WSL desktop startup

使用 packaged Desktop：

```text
arch-studio.exe
```

验证：

```text
packaged assets
Host startup
project discovery
new Core binary detection
new build identity
14-model discovery
clean shutdown
```

不能依赖：

```text
Vite dev server
manual localhost URL
old build artifacts
```

---

# 30. Full workstation smoke

## Sod

```text
open
inspect
warm session
field Preview
x_pos marker
AMR
Save/Reopen
```

## CellularDet

```text
2D Preview
warm update
field switch
AMR overlay
cancel/recovery
```

## 新模型

至少选一个新增模型：

```text
GravityBox / JeansWave / SNIaCoupled
```

验证：

```text
registered
inspect-case works
unsupported full Preview shown honestly
no fake AMR button
```

---

# 31. Gravity smoke

用合法 minimal Gravity config：

```text
new gravity parameters visible
self gravity no longer globally marked unavailable
inspection follows Core
```

本阶段不以完整 simulation 验收 gravity 数值求解。

---

# 32. Retired desktop smoke

packaged app 打开 legacy `.par`：

```text
retired diagnostic
explicit removal
Undo
Save/Reopen
```

确保 desktop 与 browser/dev 流程一致。

---

# 33. Session / AMR / Desktop cleanup

关闭窗口期间分别测试：

```text
idle warm session
active Preview
active AMR
```

关闭后确认：

```text
no managed Node process
no ARCH preview-session worker
no AMR worker
no orphan launcher child
```

---

# 34. Final mainline-compatibility self-audit

重点检查：

```text
92-key schema
14-model registry
5 retired keys
4 new gravity keys
self gravity status
JENS unavailable
source path migration
Build manifest
Session warm/cancel
AMR identity
Desktop cleanup
```

不需要重新扩展 Phase 2G 的所有历史功能。

---

# 35. Phase 2H final checkpoint

生成：

```text
studio/PHASE2H_COMPLETION_REPORT.md
studio/PHASE2H_MAINLINE_COMPATIBILITY_REPORT.md
```

建议：

```text
commit:
feat(studio): synchronize current Core capabilities

tag:
studio-phase2h-v0.19.0
```

不要自动 push。

报告至少包含：

```text
integration strategy
upstream Core ref
new binary SHA
schema count
model count
retired migration
Gravity capability behavior
Core tests
Studio tests
Host tests
Desktop UAT
remaining limits
```

然后：

> **STOP。不要自动进入 Phase 3。**

---

# 36. Explicit non-goals

本阶段不实现：

```text
新的 Gravity field visualization
Gravity potential/acceleration Preview
JENS refinement
新的 field Preview models
新的 Initial AMR models
runtime simulation monitoring
SSH / scheduler
MPI deployment planner
3D AMR visualization
generic arbitrary C++ auto-UI
CUDA/GPU Studio qualification
```

---

# 37. Final rules

```text
Running binary capability
>
Historical handoff counts
```

```text
92 / 14 are current evidence
>
Permanent hardcoded UI truth
```

```text
Retired parameter
≠
Unknown custom parameter
```

```text
Explicit migration
>
Silent deletion
```

```text
Self gravity available in production
≠
Gravity field Preview available
```

```text
Registered model
≠
Full Preview / AMR support
```

```text
JENS unavailable
≠
Auto fallback to another indicator
```

```text
Upstream Core tree authority
>
Local scientific workaround
```

```text
Mainline compatibility sync
>
Phase 3 feature expansion
```
