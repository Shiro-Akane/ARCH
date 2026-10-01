# ARCH Studio — Phase 2B Local Config Lifecycle & Safe Write Target

> **当前唯一目标 / Active target only**
>
> Phase 2 `Local Host Foundation` 已封箱：
>
> ```text
> tag: studio-phase2-v0.5.0
> branch: studio/phase2-local-host
> ```
>
> Phase 2B 只完成 **本地 `.par` 文件从“只读身份”到“可安全载入、保存、另存、检测外部冲突”** 的完整生命周期。
>
> **本阶段不实现 Build，不运行 ARCH，不实现真实 `Setup()+Init()` Preview，不做 AMR / 2D graphical binding，不做 SSH / remote。**

---

# 0. 为什么 Phase 2B 单独拆出

Phase 2 已经建立：

```text
React
  ↓
HttpLocalHostAdapter
  ↓
127.0.0.1 Local Host
  ↓
authorized project root
  ↓
source / config / binary identities
  ↓
fingerprint / external-change detection
```

当前能力明确为：

```text
readProject = true
writeConfig = false
build = false
preview = false
watchFiles = false
```

Phase 2B 只把：

```text
writeConfig
```

从 `false` 变成真实、受限制、可验证的能力。

这一步完成后，才允许 Phase 2C 接 Build。

---

# 1. Baseline / 基线确认

不要从聊天记录猜 hash。

开始任何修改前执行并报告：

```bash
git status
git branch --show-current
git rev-parse HEAD
git describe --tags --always --dirty
git rev-parse studio-phase2-v0.5.0^{commit}
```

要求：

- `studio-phase2-v0.5.0` 必须存在；
- 确认 Phase 2 working tree 状态；
- 不 reset / discard 用户已有工作；
- 不修改已有 tag；
- 从实际 Phase 2 checkpoint 建立独立分支。

建议：

```text
studio/phase2b-config-lifecycle
```

---

# 2. Phase 2B 一句话目标

实现完整但窄化的本地配置文件链：

```text
Project Session
      ↓
Open project .par through Local Host
      ↓
existing ParDocument / Working Copy
      ↓
edit
      ↓
Dirty / Invalid
      ↓
Save / Save As / Revert
      ↓
Local Host safe write
      ↓
new fingerprint / saved snapshot
```

同时安全处理：

```text
external disk edit
      ↓
fingerprint mismatch
      ↓
Conflict
      ↓
Reload / Save As / Cancel
```

---

# 3. 本阶段的核心原则

```text
Working Copy
>
Direct disk mutation
```

```text
Optimistic concurrency
>
Silent overwrite
```

```text
Atomic write
>
In-place truncation
```

```text
Exact serialized text
>
Local Host reformatting
```

```text
Explicit user action
>
Automatic reload
```

```text
Project-root confinement
>
Arbitrary filesystem write
```

---

# 4. Protocol version

Phase 2 的 Local Host protocol 是：

```text
1.0
```

Phase 2B 新增真实写入能力和 config endpoints，因此升级协议：

```text
1.1
```

Frontend 与 Local Host 都必须检查 protocol version。

旧 `1.0` Host 连接到 Phase 2B Studio 时：

```text
Local Host version is incompatible with this Studio build.
```

不得：

- 静默降级后又显示 Save；
- 假装 writeConfig 可用；
- 混用不同版本 response shape。

---

# 5. Capability 更新

Phase 2B 成功后：

```ts
interface HostCapabilities {
  readProject: true;
  writeConfig: true;
  build: false;
  preview: false;
  watchFiles: false; // unless separately and explicitly implemented
}
```

UI 必须由 capability 决定操作是否可用。

不要硬编码：

```text
connected → Save enabled
```

只有：

```text
connected
+
protocol compatible
+
writeConfig = true
+
current config associated with Local Host
```

才允许 Save in place。

---

# 6. 不重写现有 `.par` Parser / Serializer

Phase 1B 已经验证：

- comments preserved；
- unknown keys preserved；
- key order preserved；
- blank lines preserved；
- no-edit round trip；
- minimal single-edit diff；
- Custom Params；
- scientific notation；
- duplicate behavior；
- Working Copy；
- Invalid / Revert / Save As。

Phase 2B **禁止重写这一层**。

正确职责：

```text
ParDocument / Serializer
        ↓
exact text payload
        ↓
Local Host
        ↓
write exact bytes/text
```

Local Host 不解析 `.par` 科学含义，不重新排版，不重新生成参数文件。

---

# 7. Config association / 配置关联

Phase 2 报告明确指出：

> Local-host identity is separate from the manually opened editor file.

Phase 2B 必须解决这一点。

定义明确的 Config Source：

```ts
interface ConfigAssociation {
  projectId: string;
  relativePath: string;
  loadedFingerprint: FileFingerprint;
}
```

当用户从 Project Session 打开 `.par`：

```text
Local Host reads authorized project .par
        ↓
returns exact source text + fingerprint
        ↓
existing frontend ParDocument parses text
        ↓
Working Copy created
        ↓
association stored
```

不要在 Host 连接后自动覆盖当前编辑器内容。

必须由用户显式操作，例如：

```text
Open Project Config
```

或当前已有项目 UI 中等价入口。

---

# 8. Config read endpoint

增加窄化 endpoint，例如：

```text
GET /api/config
```

它只读取：

> 当前 Project Session 中已经配置的 parameter file。

不要做：

```text
GET /api/file?path=...
```

不要增加通用 filesystem read API。

Response 至少包括：

```ts
interface ConfigReadResponse {
  relativePath: string;
  text: string;
  fingerprint: FileFingerprint;
}
```

要求：

- UTF-8 / 当前项目明确支持的文本编码；
- 有响应大小上限；
- regular file only；
- project-root confinement；
- existing Phase 2 no-follow / canonical path protections 继续保留。

---

# 9. Working Copy 状态模型

区分四个概念：

```text
loaded disk version
saved snapshot
current Working Copy
current disk version
```

不要把它们压成一个字符串。

至少需要：

```ts
interface ConfigLifecycleState {
  association?: ConfigAssociation;

  loadedFingerprint?: FileFingerprint;
  savedFingerprint?: FileFingerprint;

  configState: "saved" | "dirty" | "invalid";
  diskState:
    | "in-sync"
    | "changed-externally"
    | "missing"
    | "read-error"
    | "unknown";
}
```

Preview state 继续独立：

```text
current / stale / generating / failed
```

正常存在：

```text
Config: Dirty
Preview: Current
```

或：

```text
Config: Saved
Preview: Stale
```

不要强制把这些状态绑定成一个总状态。

---

# 10. Save in place — 必须使用 optimistic concurrency

Save 请求必须带：

```text
expected fingerprint
```

例如：

```ts
interface SaveConfigRequest {
  projectId: string;
  relativePath: string;
  expectedFingerprint: FileFingerprint;
  text: string;
}
```

Host 收到后必须：

1. 再次读取 / fingerprint 当前磁盘文件；
2. 比较 `expectedFingerprint`；
3. 一致才允许写入；
4. 不一致则返回 Conflict；
5. 不得静默覆盖。

---

# 11. Conflict response

建议使用明确状态，例如：

```text
HTTP 409 Conflict
```

Response：

```ts
interface ConfigConflict {
  kind: "external-change";
  relativePath: string;
  expected: FileFingerprint;
  actual: FileFingerprint;
}
```

Frontend 显示：

```text
This configuration file changed on disk.

Your unsaved Working Copy has been kept.

[ Reload disk version ]
[ Save Working Copy As... ]
[ Cancel ]
```

Phase 2B 默认 **不提供 silent force overwrite**。

如一定要支持覆盖，必须是第二步明确确认，不得自动执行；但它不是本阶段阻断验收条件。

---

# 12. Atomic Save / 原子保存

禁止：

```text
open original
→ truncate
→ write
```

正确流程：

```text
serialize Working Copy
        ↓
create temporary file in SAME directory
        ↓
write full content
        ↓
flush / close
        ↓
atomic rename / replace
        ↓
fingerprint final file
```

要求：

- temp file 必须在目标同目录，保证 rename 可原子完成；
- 写失败时原文件不受损；
- rename 失败时原文件不受损；
- 尽量保留原文件权限 mode；
- 临时文件失败后清理；
- 不留 `.tmp` 垃圾文件作为正常结果。

在支持的平台上合理使用：

```text
fsync / sync where practical
```

不要为了理论上的 crash-consistency 大规模重写系统，但不能用危险的 truncate-first 保存。

---

# 13. Save success behavior

Save 成功后：

```text
disk file updated
        ↓
returned final fingerprint
        ↓
saved snapshot = current Working Copy
        ↓
loaded/saved fingerprint = final fingerprint
        ↓
ConfigState = saved
        ↓
DiskState = in-sync
```

Save 本身：

- 不生成 Preview；
- 不自动 Build；
- 不改变科学结果；
- 不自动清除 stale Preview；
- 不重新排序 UI 参数。

如果 Preview 因参数修改已 stale：

```text
Save
→ Preview 仍 stale
```

这是正常行为。

---

# 14. Save failure behavior

保存失败时：

- Working Copy 必须完整保留；
- ConfigState 保持 dirty / invalid；
- 原文件不应受损；
- 显示具体错误；
- 用户可以重试或 Save As；
- 不自动 Revert；
- 不关闭当前 Config。

覆盖：

```text
permission denied
disk read-only
directory missing
rename failure
file disappeared
fingerprint conflict
payload too large
```

---

# 15. Save As

Phase 2B 实现 Local Host-backed `Save As`。

由于当前仍是 Web-native + Local Host，而不是 Tauri：

> 不伪造 OS-native “Save File” picker。

使用明确的：

```text
project-relative destination
```

例如：

```text
simulation/Sod/Sod_experiment.par
```

UI 可提供：

```text
Save Working Copy As...

Relative path
[ simulation/Sod/Sod_experiment.par ]
```

要求：

- 路径只能位于 authorized project root；
- 默认扩展名 `.par`；
- absolute path 禁止；
- `../` traversal 禁止；
- symlink escape 禁止；
- 目标目录必须真实存在，除非后续另有明确 mkdir contract；
- 本阶段不增加通用文件浏览器。

---

# 16. Save As endpoint

增加窄化 endpoint：

```text
POST /api/config/save-as
```

Request：

```ts
interface SaveConfigAsRequest {
  projectId: string;
  destinationRelativePath: string;
  text: string;
}
```

默认：

```text
overwrite = false
```

如果目标已存在：

```text
409 destination-exists
```

UI 要求用户：

- 换名字；
- 或取消。

“覆盖已有目标文件”可以以后增加二次明确确认，但不是 Phase 2B 必须项。

---

# 17. Save As success semantics

原需求要求：

> Save As 成功后，新文件成为当前配置。

因此成功后：

```text
new .par written
        ↓
Project Session parameterFile updates
        ↓
ConfigAssociation points to new file
        ↓
saved snapshot = current Working Copy
        ↓
Config = saved
        ↓
Disk = in-sync
```

原文件保持不变。

不得：

- 同时继续把旧文件显示为 current；
- 自动修改 case.cpp；
- 自动 Build；
- 自动 Preview。

---

# 18. Revert 语义

`Revert` 继续保持：

> 回到最近一次成功 Load / Save / Save As 的 saved snapshot。

如果磁盘文件已经被外部修改：

```text
Revert
```

不能偷偷把外部磁盘版本读进来。

它只恢复：

```text
saved snapshot in Studio
```

真正读取外部新版本必须使用：

```text
Reload disk version
```

这样 Undo / Revert / Reload 三者语义清楚。

---

# 19. Reload disk version

当 `DiskState = changed-externally`：

提供：

```text
Reload disk version
```

行为：

1. 如果 Working Copy clean：可以直接 reload；
2. 如果 Working Copy dirty：必须先提示；
3. 不允许自动丢弃未保存编辑。

Dirty 时：

```text
File changed externally.
You also have unsaved changes.

[ Save Working Copy As... ]
[ Discard and Reload ]
[ Cancel ]
```

本阶段不做自动 three-way merge。

---

# 20. External change detection

Phase 2 已有：

```text
Refresh Project State
```

Phase 2B 继续使用它。

刷新时：

- re-fingerprint current project `.par`；
- 如果 fingerprint 与 association/saved fingerprint 不同：
  - `DiskState = changed-externally`；
- 不自动 reload editor；
- 不清空 Working Copy；
- 不自动保存。

保存操作本身也必须再次检查 fingerprint。

即使用户没有先 Refresh：

```text
Save
```

也不能覆盖外部变更。

---

# 21. Optional watcher

File watcher 仍然是可选项。

如果实现：

- 只 watch current associated `.par`；
- debounce；
- 只更新 `DiskState`；
- 不自动 reload；
- 不递归 watch repo；
- 不 watch build/output tree。

如果跨平台复杂度上升：

> 继续保留 manual Refresh 即可。

Watcher 不作为 Phase 2B completion blocker。

---

# 22. Unsaved-change guard

任何会替换当前 Working Copy 的操作必须检查 Dirty 状态：

至少包括：

```text
Open Project Config
Reload disk version
Switch associated config
Disconnect / replace project session if it would replace editor content
```

Dirty 时：

```text
You have unsaved changes.

[ Save ]
[ Save As... ]
[ Discard ]
[ Cancel ]
```

如果当前 Config 没有 Local Host association：

```text
Save
```

可以不可用，但 `Save As...` 继续可用。

不要让 browser navigation guard / beforeunload 成为本阶段的大项目；页面关闭提示可保留已有行为或后续处理。

---

# 23. Host write security boundary

Phase 2 的安全边界必须继续成立。

Local Host write endpoints 只能写：

> 当前授权 project root 内的 `.par` 配置文件。

明确禁止：

```text
arbitrary filesystem write
generic file writer
shell
exec
script
build command
chmod API
delete API
rename arbitrary file API
mkdir arbitrary tree API
```

Save/Save As endpoint 的 `text` 是数据，不是命令。

---

# 24. Request size limit

为 config write body 设明确上限。

建议：

```text
1 MiB
```

或根据当前真实 `.par` 文件尺寸选择一个保守上限。

要求：

- 超限返回明确错误；
- 不让 Local Host 接受无限 request body；
- 不因为超限清空 Working Copy。

不要沿用 Project fingerprint 的 64 MiB 上限作为写接口默认值，`.par` 不需要这么大。

---

# 25. Path security tests

新增写路径测试：

```text
../escape.par
../../...
absolute path
encoded traversal
backslash / separator edge cases
NUL / malformed path
symlink destination
symlink parent component
destination outside root
non-.par target if policy requires .par
existing destination
missing parent
permission denied
```

所有失败：

- 不得写 project root 外文件；
- 不得破坏原文件；
- 不得留下临时文件。

---

# 26. Atomic-write tests

至少验证：

```text
successful save
write failure before rename
rename failure
conflict before write
destination exists
temp cleanup
original preservation on failure
final fingerprint matches final bytes
```

测试使用临时非科学 fixture。

不要为了测试重新运行 ARCH。

---

# 27. Existing `.par` exactness regression

必须继续保持：

```text
load → no edit → serialize
```

与原文件一致或符合现有 exact-round-trip contract。

以及：

```text
load
→ edit one key
→ save
```

只有对应 value 发生预期变化。

Local Host 不能引入：

- newline normalization；
- whitespace rewrite；
- key reorder；
- comment removal；
- unknown key removal。

---

# 28. UI / UX

当前 Project Panel 中 config file 可以显示：

```text
Config
simulation/Sod/Sod_beginner.par

Saved
Disk: in sync
```

修改后：

```text
Dirty
Disk: in sync
```

外部变更：

```text
Dirty
Disk: changed externally
```

不要用一个模糊的：

```text
Config: problem
```

代替双状态。

---

# 29. Parameter Inspector source

右侧 Inspector 可以增加可靠来源信息：

```text
Current value
Loaded value
Saved value
Source file
Disk state
```

但不要把：

```text
default value
unit
physical meaning
```

在 Core 未提供 metadata 时猜出来。

Phase 2B 仍不实现 ParameterMetadata backend。

---

# 30. Preview behavior

Phase 2B 不实现真实 Preview。

但现有 Preview 状态必须保持正确：

编辑参数：

```text
Config = dirty
Preview = stale
```

Save：

```text
Config = saved
Preview = stale
```

Reload：

```text
Working Copy replaced
Preview = stale
```

外部 disk change 本身：

```text
Disk = changed externally
```

不能自动声称 Preview stale，除非当前 Working Copy 实际被 reload / replaced。

保持“磁盘状态”和“Working Copy / Preview 状态”分离。

---

# 31. Build / binary 状态

Phase 2B 不新增 Build freshness 逻辑。

仍然：

```text
build = false
preview = false
```

Binary：

```text
available / missing / unknown
```

不要因为 `.par` 保存成功就：

```text
Binary = current
```

这两个没有直接因果关系。

---

# 32. Error model

定义结构化错误，不把任意 Node exception 原样喷到 UI。

建议：

```ts
type ConfigFileError =
  | "not-found"
  | "permission-denied"
  | "outside-project-root"
  | "changed-externally"
  | "destination-exists"
  | "invalid-path"
  | "payload-too-large"
  | "write-failed"
  | "rename-failed"
  | "protocol-error";
```

UI 显示：

```text
human-readable message
+
optional technical details
```

technical details 可以复制，但不抢主视觉。

---

# 33. Phase 2B milestones

严格顺序：

```text
P2B-M0  baseline / Phase 2 regression
P2B-M1  protocol 1.1 + writeConfig capability
P2B-M2  Local Host config read + association
P2B-M3  safe Save in place + optimistic concurrency
P2B-M4  atomic write + failure recovery
P2B-M5  Local Host Save As
P2B-M6  external-change conflict UX + Reload
P2B-M7  unsaved-change guard
P2B-M8  security / exactness / regression
P2B-M9  manual UAT + report + checkpoint
```

每个 Milestone：

1. 完成；
2. 自动测试；
3. 更新 `studio/STATUS.md`；
4. Scope Review；
5. 再继续。

不得并行顺手进入 Build。

---

# 34. Regression requirements

必须保持 Phase 2 已通过内容：

## Local Host
- 127.0.0.1 only；
- exact Origin / Host checks；
- narrow fixed endpoints；
- project-root confinement；
- traversal rejection；
- symlink rejection；
- no arbitrary command；
- file fingerprint；
- project refresh；
- previous session preserved on refresh failure。

## Frontend
- Mock Demo；
- Preview retention；
- Preview centering；
- Update Preview；
- dark plot；
- contextual Inspector；
- Undo / Redo；
- numeric scrub；
- parameter panel；
- Raw `.par`。

## Real `.par`
- exact round-trip；
- Custom Params；
- Invalid；
- Revert；
- existing browser Save As fallback where applicable。

## Real Plotfile
- 1D `.h5`；
- field enumeration；
- LineVis；
- Inspector；
- invalid recovery。

---

# 35. Manual UAT / 手工验收

自动测试全部通过后，进行一次有限 Manual UAT。

至少验证：

### Save

```text
Open Project Config
→ edit nblockx1 4 → 8
→ Save
→ Config becomes Saved
→ reload from disk
→ value remains 8
→ comments / unknown text preserved
```

### Revert

```text
8 → edit 12
→ Revert
→ returns 8
```

### External conflict

```text
load config
→ edit Working Copy
→ externally modify disk file
→ Refresh or Save
→ conflict detected
→ Working Copy retained
→ no silent overwrite
```

### Save As

```text
current config
→ Save As project-relative new name
→ new file exists
→ original unchanged
→ new file becomes current config
```

### Failure

```text
invalid/outside path
→ write blocked
→ Working Copy retained
→ no filesystem damage
```

---

# 36. Automated checks

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

如果新增独立 write-security suite：

```bash
npm run test:host
```

必须包含它。

不得削弱现有测试换绿色。

不需要重新运行 ARCH CPU/CUDA scientific baseline。

---

# 37. Phase 2B acceptance

Phase 2B 通过必须满足：

1. Protocol 升级且兼容性检查有效；
2. `writeConfig=true` 只在真实支持时暴露；
3. Project config 可以通过 Local Host 显式载入 editor；
4. Working Copy 不被自动覆盖；
5. Save 写回当前 `.par`；
6. Save 使用 expected fingerprint；
7. 外部变更不会被 silent overwrite；
8. Save 为 atomic replace，而非 truncate-first；
9. 写失败保留原文件和 Working Copy；
10. Save As 只能写 project root 内；
11. Save As 默认不覆盖已有文件；
12. Save As 成功后新文件成为 current config；
13. Revert 回到最近 saved snapshot；
14. Reload 与 Revert 语义分离；
15. Dirty 状态下替换 Working Copy 有 guard；
16. Local Host 仍没有 generic write / exec endpoint；
17. path traversal / symlink escape 写测试通过；
18. `.par` exact round-trip 不退化；
19. Preview / Mock / Plotfile 功能不退化；
20. Build 仍未实现；
21. Real IC Preview 仍未实现；
22. ARCH Core 未修改；
23. tests / host tests / lint / typecheck / build / diff check 通过。

---

# 38. Completion report

生成：

```text
studio/PHASE2B_CONFIG_LIFECYCLE_REPORT.md
```

至少包括：

```text
Baseline
Protocol 1.1
Config association
Save algorithm
Atomic write behavior
Conflict model
Save As behavior
Security boundary
Manual UAT
Regression
Automated checks
Deferred
Remaining issues
```

不要宣称 Build / Preview 完成。

---

# 39. Commit / tag

建议 commit：

```text
feat(studio): add safe local config file lifecycle
```

建议 tag：

```text
studio-phase2b-v0.6.0
```

不要自动 push。

完成后报告：

- commit hash；
- tag；
- working tree；
- test summary；
- remaining issues。

然后：

> **STOP。**

不要自动进入 Phase 2C。

---

# 40. Phase 2C 以后

Phase 2B 完成后再单独制定：

## Phase 2C — Build Integration

```text
build manifest
case ↔ binary authoritative mapping
source/dependency freshness
compiler stdout/stderr
Build
Build & Preview state shell
```

## Phase 2D — Real IC Preview

```text
unsaved Working Copy
→ ARCH executable
→ Setup()+Init()
→ sampled PreviewData
→ fields / units / provenance
→ cancellation / revision protection
```

## Phase 2E — Graphical Bindings + 2D

```text
Sod x_pos binding
graphical marker
drag → one Undo
2D preview
Cellular authoritative binding
```

---

# 41. Final rules

```text
Safe Save
>
Convenient overwrite
```

```text
Expected fingerprint
>
Last writer wins
```

```text
Atomic replace
>
Truncate and hope
```

```text
Working Copy retained
>
Automatic reload
```

```text
Explicit Reload
>
Revert ambiguity
```

```text
Project-root-only write
>
Generic filesystem API
```

```text
Finish Phase 2B
>
“顺手做 Build”
```
