# ARCH Studio — Phase 2C Build Integration & Binary Provenance Target

> **当前唯一目标 / Active target only**
>
> Phase 2B `Local Config Lifecycle` 已封箱：
>
> ```text
> tag: studio-phase2b-v0.6.0
> branch: studio/phase2b-config-lifecycle
> ```
>
> Phase 2C 只负责把 **已授权本地项目 → 明确 Build Profile → 真实编译 → compiler output → Build Manifest → binary 状态** 接入现有 ARCH Studio。
>
> **本阶段不运行 simulation，不实现真实 `Setup()+Init()` Initial Preview，不做 AMR / 2D graphical binding，不做 SSH / scheduler / remote。**
>
> Build 的目标不是“让浏览器能执行任意命令”，而是让 Local Host 在一个固定、可审计、不可由浏览器任意改写的 Build Profile 下调用已有 ARCH 构建系统。

---

# 0. 为什么 Phase 2C 单独拆出

当前已经完成：

```text
Phase 2
Local Host + Project Session + source/config/binary identity

Phase 2B
real .par read/write
Working Copy
Save / Save As / Revert / Reload
atomic write
external conflict
```

现在组长需求中尚未闭环的下一段是：

```text
case.cpp / relevant build inputs
        ↓
source/build state
        ↓
Build
        ↓
real stdout / stderr
        ↓
success / failure
        ↓
Build Manifest
        ↓
binary identity / provenance
```

只有这一段稳定后，Phase 2D 才允许：

```text
Working .par
+
current built binary
→ Setup()+Init()
→ Real Initial Preview
```

---

# 1. Baseline / 基线

不要根据聊天记录猜 commit。

任何修改前执行并报告：

```bash
git status
git branch --show-current
git rev-parse HEAD
git describe --tags --always --dirty
git rev-parse studio-phase2b-v0.6.0^{commit}
```

要求：

- `studio-phase2b-v0.6.0` 必须存在；
- baseline working tree 必须明确；
- 不 reset / discard 用户已有工作；
- 不移动或覆盖已有 tag；
- 不修改 ARCH scientific Core；
- 从实际 Phase 2B checkpoint 创建独立分支。

建议：

```text
studio/phase2c-build-integration
```

---

# 2. Phase 2C 一句话目标

实现：

```text
Project Session
      ↓
authoritative Build Profile
      ↓
Build Request
      ↓
Local Host fixed build runner
      ↓
real stdout / stderr events
      ↓
Build Result
      ↓
Build Manifest
      ↓
Binary State / Provenance
```

结束时用户应能回答：

```text
当前选中了哪个 case source？
这个项目配置了哪个 Build Profile？
最后一次 Build 成功还是失败？
最后成功 Build 对应哪份 source / inputs？
当前 executable 是哪次成功 Build 的产物？
selected source 之后是否又发生了变化？
```

本阶段仍然不能回答：

```text
这个 binary 生成的真实 Initial Condition 是什么？
```

那属于 Phase 2D。

---

# 3. Protocol 版本

Phase 2B 当前 Local Host protocol：

```text
1.1
```

Phase 2C 增加 Build endpoints、event stream 和 Build Manifest，因此升级：

```text
1.2
```

要求：

- frontend 与 Local Host 都校验 `protocolVersion`；
- 1.1 Host 不得被 Phase 2C Studio 误认为支持 Build；
- incompatible Host 给出明确提示；
- 不静默回退到“假 Build”。

---

# 4. Capability 更新

Phase 2C 完成后，支持的 WSL/Linux Local Host：

```ts
interface HostCapabilities {
  readProject: true;
  writeConfig: true;
  build: true;
  preview: false;
  watchFiles: false; // unless already separately implemented
}
```

UI 只能在以下条件都满足时 enable Build：

```text
Local Host connected
+
protocol compatible
+
build capability = true
+
valid Build Profile exists
+
no active Build
```

不能：

```text
Host connected → Build enabled
```

---

# 5. Build Profile 必须是 Host 侧权威配置

Phase 2C **禁止浏览器发送任意 program / command / shell string**。

定义 Host-owned Build Profile，例如：

```ts
interface BuildProfile {
  id: string;
  displayName: string;

  caseId?: string;

  buildDirRelative: string;
  target: string;
  outputBinaryRelative: string;

  parallelism?: number;

  trackedInputs?: string[];

  configured: boolean;
}
```

核心原则：

> Frontend 只选择 / 请求 `profileId`，不能提交 command line。

Local Host 根据 server-side Build Profile 组装固定命令。

---

# 6. Phase 2C 只支持已有 CMake build tree

本阶段优先支持：

```bash
cmake --build <authorized-build-dir> --target <fixed-target> --parallel <N>
```

要求：

- 使用 `child_process.spawn` 或等价 argv API；
- `shell: false`；
- `cwd` 为 Project Root 或 Build Profile 指定、且已验证位于授权范围的目录；
- program 固定为经过 Host 侧允许的 CMake executable；
- target 来自 Host-owned Build Profile；
- frontend 不得覆盖 program / args / cwd。

Phase 2C **不负责创建一个全新的 build tree**。

如果当前 Build Profile 对应的 build directory：

```text
missing
not configured
invalid
```

则 Build State 显示：

```text
Not configured
```

并停止。

不要为了让测试通过自动执行：

```bash
cmake -S ... -B ...
```

除非仓库现有正式 Build Contract 已明确授权并且在进入实现前报告。

默认情况下，**configure/reconfigure 留给后续扩展，不作为 2C blocker**。

---

# 7. 开始实现前必须审计 ARCH 当前构建方式

P2C-M0 中只读检查：

- repository build documentation；
- CMake targets；
- 当前已存在 build directories；
- executable 实际位置；
- 当前 Sod / selected case 如何进入 executable；
- CPU / CUDA build profile 区别；
- 是否已有 case registry / compile definition / build metadata；
- 是否存在 authoritative case ↔ binary mapping。

不要猜。

如果实际项目构建方式与：

```text
cmake --build existing-dir --target fixed-target
```

不兼容：

> 停止并报告真实 Build Contract，再调整 Phase 2C 实现。

不要偷偷改 CMake 来迎合 Studio。

---

# 8. UAT 使用轻量已有 Build Profile

当前阶段的 integration UAT：

- 优先使用已有、已配置、可快速增量编译的 CPU / lightweight Build Profile；
- 不因为 Studio UAT 重新配置 CUDA；
- 不重新做完整 CPU/CUDA baseline；
- 不执行 simulation；
- 不生成新的 scientific output。

如果只有 CUDA build tree 可用：

> 先报告其成本和现状，不自动做大规模 rebuild。

---

# 9. Case ↔ Build Profile ↔ Binary mapping

组长需求要求：

> 算例名称与编译程序的对应关系由核心／本地接入接口核对。

Phase 2C 不允许通过文件名猜。

权威关系必须来自：

```text
Build Profile / existing Core interface / project-local authoritative metadata
```

至少形成：

```ts
interface BuildIdentity {
  profileId: string;
  caseId?: string;
  sourceRelativePath?: string;
  outputBinaryRelative: string;
  mappingState:
    | "verified"
    | "configured"
    | "unknown"
    | "mismatch";
}
```

语义：

```text
verified
= authoritative interface verified mapping

configured
= local profile explicitly configured it, but Core has not independently verified it

unknown
= no authoritative relation available

mismatch
= authoritative verification says it does not match
```

不要把：

```text
configured
```

显示成：

```text
scientifically verified
```

---

# 10. Build states

建立独立 Build State：

```ts
type BuildState =
  | "not-configured"
  | "ready"
  | "queued"
  | "building"
  | "succeeded"
  | "failed"
  | "cancelled"
  | "unknown";
```

Binary State 建议：

```ts
type BinaryBuildState =
  | "missing"
  | "available"
  | "built-from-current-tracked-inputs"
  | "needs-build"
  | "freshness-unknown";
```

重要：

> 不能仅用 `source mtime > binary mtime` 宣称 authoritative freshness。

---

# 11. Build freshness / 过期判断

这是 Phase 2C 的高风险点。

需要区分：

```text
selected case source changed
```

和：

```text
all relevant dependencies are known current
```

Phase 2C 可以可靠支持：

```text
selected source fingerprint
+
explicit trackedInputs from Build Profile
+
last successful Build Manifest
```

成功 Build 时记录这些 input fingerprints。

之后任一明确 tracked input 改变：

```text
BinaryBuildState = needs-build
```

如果 Build Profile 没有完整依赖输入信息：

```text
BinaryBuildState = freshness-unknown
```

不要撒谎显示：

```text
Up to date
```

---

# 12. 不扫描 / 推断任意 C++ include dependency graph

Phase 2C 禁止自己实现：

```text
C++ preprocessor
include graph parser
clang dependency analyzer
source code semantic scanner
```

如果需要完整 dependency freshness：

> 应由 existing build system / Core-side metadata / future authoritative interface 提供。

Phase 2C 的 Build Profile 可以显式提供：

```text
trackedInputs
```

前端只展示结果。

---

# 13. Build request contract

Frontend request 必须很窄：

```ts
interface BuildRequest {
  projectId: string;
  profileId: string;
}
```

禁止 request 包含：

```text
program
command
args
cwd
env
shell
script
```

Local Host 自己从：

```text
project session
+
profileId
```

解析实际 fixed Build Profile。

---

# 14. Build result / event contract

建议：

```ts
interface BuildStarted {
  buildId: string;
  profileId: string;
  startedAt: string;
}

interface BuildEvent {
  buildId: string;
  sequence: number;
  timestamp: string;

  kind:
    | "state"
    | "stdout"
    | "stderr";

  state?: BuildState;
  text?: string;
}

interface BuildResult {
  buildId: string;
  state: "succeeded" | "failed" | "cancelled";
  exitCode?: number;
  signal?: string;
  startedAt: string;
  finishedAt: string;
}
```

每个 Build event 必须带：

```text
buildId
sequence
```

防止旧 Build 输出覆盖新 Build UI。

---

# 15. Build API endpoints

建议保持窄接口：

```text
GET  /api/build/profile
GET  /api/build/status
POST /api/build
GET  /api/build/:buildId/events
POST /api/build/:buildId/cancel   // optional but recommended if safely implemented
```

Event transport 推荐：

```text
SSE / Server-Sent Events
```

原因：

- 单向；
- 实现轻；
- 适合 stdout/stderr；
- 不需要为了 Build 引入 WebSocket framework。

如果当前架构已有更合适的 typed polling：

> 可以使用 bounded polling，但必须保持 requestId/buildId 和 sequence。

不要引入大型 realtime framework。

---

# 16. Build concurrency

同一个 Local Host Project Session：

```text
最多一个 active Build
```

第二次 Build request：

```text
409 build-busy
```

或等价明确状态。

不能并行启动两个：

```text
cmake --build
```

去争抢同一 build tree。

如果项目切换 / session 变化：

- 不能把旧 Build 结果标成新 Project 的结果；
- UI 必须继续按 `buildId + projectId` 归属。

---

# 17. Build cancellation

组长原需求没有把取消编译作为硬性验收条件，因此 Phase 2C：

> cancellation 是推荐项，但不是必须以高风险方式强行实现。

如果实现：

- 只取消当前 Host 自己启动的 Build process；
- Linux 下使用明确 process-group 生命周期；
- 先 graceful termination；
- 必要时有限超时后强制结束；
- 不能 kill 任意 PID；
- frontend 不提交 PID；
- 不提供通用 process API。

如果安全可靠的 cancellation 会显著扩大 Scope：

> defer，并明确 UI 暂不提供 Cancel Build。

---

# 18. Environment / 环境控制

Build process 不继承一个不可控的巨大环境。

允许：

- 必需 PATH；
- HOME；
- known compiler/CMake variables；
- Host startup 时已经存在、项目构建确实需要的受控环境。

禁止 frontend 注入：

```text
PATH
LD_PRELOAD
CC
CXX
CUDA flags
arbitrary env
```

如果 ARCH 现有 build 需要特殊环境：

> 在 P2C-M0 审计并记录，由 Host Build Profile 固定配置。

---

# 19. Build log safety

Compiler output：

- 作为纯文本显示；
- 不 render HTML；
- ANSI 可 strip 或安全解析；
- 单 event / 单 line 设长度上限；
- UI log buffer 设总上限，例如最近若干 MiB / 若干千行；
- 超限时明确：

```text
Earlier build output truncated in Studio view.
```

不要无限把 stdout/stderr 堆进 React memory。

完整 build output 如需落盘：

> 本阶段默认不新增持久 log 文件，除非 existing build system 已经产生。

---

# 20. Build Manifest

每次**成功 Build**后生成 Studio-side Build Manifest。

不要修改 ARCH source。

推荐存放于被忽略的 Local Host state，例如：

```text
studio/.local/
```

或等价非版本控制 runtime state。

Manifest 示例：

```ts
interface BuildManifest {
  manifestVersion: string;

  buildId: string;
  projectId: string;
  profileId: string;
  caseId?: string;

  startedAt: string;
  finishedAt: string;

  sourceFingerprint?: FileFingerprint;
  trackedInputFingerprints: Array<{
    relativePath: string;
    fingerprint: FileFingerprint;
  }>;

  buildProfileFingerprint: string;

  outputBinary: {
    relativePath: string;
    fingerprint: FileFingerprint;
  };
}
```

可选记录：

```text
git HEAD
dirty state
tool versions
```

但不要让获取 git metadata 成为 Build 成功的 blocker。

---

# 21. Manifest 必须对应最终 binary

Build exit code = 0 后不能立刻宣称成功。

必须继续检查：

```text
configured output binary exists
regular file
inside expected allowed path
can be fingerprinted
```

成功条件：

```text
build process exit = 0
+
expected binary exists
+
binary fingerprint success
```

否则：

```text
BuildState = failed
```

错误应类似：

```text
Build command succeeded, but the expected executable was not found.
```

---

# 22. Failed Build 不覆盖 last successful manifest

例如：

```text
Build A succeeded
→ binary A + manifest A

source changes

Build B failed
```

此时必须保留：

```text
lastSuccessfulBuild = A
```

同时：

```text
currentBuildState = failed
BinaryBuildState = needs-build
```

不能因为 Build B 失败就忘掉之前 binary 的 provenance。

UI 应能表达：

```text
Last successful build: ...
Current source: changed
Latest build attempt: failed
```

---

# 23. Source change after successful Build

成功 Build 时 snapshot：

```text
source fingerprint
tracked input fingerprints
```

之后：

```text
Refresh Project State
```

或 optional watcher 检测到明确 tracked input 变化：

```text
Source: changed since build
Binary: needs build
Preview: stale / previous build
```

但 Phase 2C 没有真实 Initial Preview，因此不要伪造：

```text
real preview stale
```

只更新 Build/Binary provenance。

现有 Mock Preview 状态保持自身逻辑。

---

# 24. `.par` 变化不应触发 needs-build

除非 ARCH 项目实际 Build Contract 明确 `.par` 被编译入 binary，否则普通 `.par` 修改：

```text
Config Dirty / Saved
```

不能自动：

```text
Binary = needs-build
```

`.par` 是 runtime config。

Build State 与 Config State 分离。

---

# 25. Source viewer / 搜索

组长需求要求：

```text
在 Studio 中只读查看源码、支持搜索
```

Phase 2C 可以加入一个**轻量只读 Source View**，前提是：

- 只读取当前 Project Session 已配置的 source file；
- 使用已有 Local Host confinement；
- 不新增 generic file browser；
- 不修改源码；
- 搜索只在已载入 source text 内进行。

可以显示：

```text
Source
Sod.cpp
[ Search... ]
```

代码高亮不是本阶段 blocker。

不要为了高亮引入大型 editor framework。

---

# 26. Open in external editor

组长需求还包括：

```text
Open in Editor
```

但这涉及本地 process launch。

Phase 2C 默认 **defer**。

允许先提供：

```text
Copy path
```

或：

```text
Show source path
```

禁止为了一个按钮增加：

```text
POST /exec-editor
```

或 generic arbitrary command API。

如果未来实现，应有独立 trusted editor contract。

---

# 27. Project UI

沿用当前 Project Panel，增加：

```text
Build Profile
CPU Release

Build
Ready / Building / Succeeded / Failed

Binary
available
needs build
freshness unknown
```

以及：

```text
[ Build ]
```

Build 期间：

```text
Building…
```

并可以展开：

```text
Build Output
```

失败：

```text
Build failed
[ View output ]
[ Build again ]
```

成功：

```text
Build succeeded
Binary fingerprint: ...
```

不要把完整 stdout 永久塞在主参数区。

---

# 28. Build log UI

建议独立可折叠区域：

```text
BUILD OUTPUT

[stdout/stderr stream...]

[Copy]
[Clear view]
```

`Clear view` 只清 UI buffer，不改变 Build history / manifest。

错误位置如果 compiler output 提供：

```text
file:line:column
```

可以作为可复制文本显示。

本阶段不要求把错误点击后自动跳 IDE。

---

# 29. Build & Preview

组长需求允许：

```text
Build & Preview
```

但真实 Preview 要到 Phase 2D。

因此 Phase 2C：

- **不实现 Build & Preview 真链路**；
- 可以保留 disabled placeholder；
- 或完全不显示；
- 不在 Build 成功后自动启动任何 Preview process。

下一阶段才接。

---

# 30. Binary readiness for Phase 2D

Phase 2C 完成后，应向 Phase 2D 提供稳定查询：

```ts
interface CurrentBinaryProvenance {
  mappingState: ...;
  binaryState: ...;
  lastSuccessfulBuild?: BuildManifest;
}
```

Phase 2D 只有在：

```text
expected binary exists
+
acceptable mapping state
+
Build provenance available / explicitly accepted
```

时才允许请求 Real IC Preview。

不要在 2C 实现这个 Preview。

---

# 31. Local Host Build security

Phase 2 / 2B 的安全边界继续成立。

新增 Build 后特别禁止：

```text
generic /exec
generic /shell
frontend-supplied argv
frontend-supplied cwd
frontend-supplied environment
frontend-supplied binary path
```

Local Host 只能执行：

> Host-owned Build Profile 定义的固定构建动作。

必须继续：

```text
127.0.0.1 only
exact Origin
exact Host
protocol header
request size bounds
fixed endpoints
```

---

# 32. Process security tests

至少覆盖：

```text
unknown profile ID
frontend attempts command field injection
frontend attempts args injection
frontend attempts cwd injection
frontend attempts env injection
concurrent build
missing build directory
build directory outside root if policy forbids it
missing CMake
missing target
failed process spawn
non-zero exit
output binary missing after exit 0
oversized log event / output flood handling
stale buildId event
```

不能因为加入 Build 破坏原有 traversal / path tests。

---

# 33. Build Profile fingerprint

Build Profile 自身也要 fingerprint。

至少考虑：

```text
buildDirRelative
target
outputBinaryRelative
parallelism
trackedInputs
caseId
```

Build Profile 改变后：

```text
lastSuccessfulBuild
```

不能继续被标成完全对应当前 Profile。

显示：

```text
Build configuration changed
```

或：

```text
Binary freshness unknown
```

---

# 34. Project switch / disconnect during Build

如果当前架构允许 project/session replacement：

Building 时不要静默切掉归属。

最安全策略：

```text
active build
→ block project replacement
```

提示：

```text
A build is currently running.
Wait for it to finish before switching projects.
```

如果实现 cancel 足够可靠：

```text
Cancel Build and Switch
```

可作为后续增强。

本阶段优先阻止切换。

---

# 35. Build failure preserves editor state

无论 Build：

```text
spawn failed
compiler error
target missing
binary missing
cancelled
```

都不得：

- 清空 `.par` Working Copy；
- Revert 参数；
- 覆盖磁盘 config；
- 清除最后成功 Mock Preview；
- 修改 ParDocument；
- 自动 Save。

Build 是独立状态通道。

---

# 36. Phase 2C milestones

严格按顺序执行：

```text
P2C-M0  baseline + real ARCH build contract audit
P2C-M1  protocol 1.2 + BuildProfile / Build contracts
P2C-M2  fixed-profile Local Host build runner
P2C-M3  build event stream + bounded log handling
P2C-M4  Build UI + failure/output states
P2C-M5  Build Manifest + binary fingerprint
P2C-M6  selected-source / tracked-input freshness
P2C-M7  source read-only view + project provenance
P2C-M8  security / concurrency / failure recovery
P2C-M9  regression + finite build UAT
P2C-M10 completion report + checkpoint
```

每个 Milestone：

1. 完成；
2. tests；
3. 更新 `studio/STATUS.md`；
4. Scope Review；
5. 再继续。

如果 M0 发现真实 ARCH Build Contract 与本需求假设冲突：

> 停止，在写代码前报告。

---

# 37. Regression requirements

必须保留 Phase 2B 所有已工作内容。

## Local Host

- loopback-only；
- protocol negotiation；
- Project Session；
- path confinement；
- fingerprint；
- external change refresh；
- no arbitrary read/write；
- no generic command endpoint。

## `.par`

- Open Project Config；
- Working Copy；
- exact round-trip；
- Dirty / Invalid；
- Save；
- Save As；
- Revert；
- Reload；
- external conflict；
- atomic write。

## Studio

- Mock Demo；
- Preview retention；
- Preview centering；
- Update Preview；
- parameter editor；
- Undo / Redo；
- numeric scrub；
- contextual Inspector；
- Raw `.par`；
- dark plot。

## Plotfile

- real 1D HDF5；
- field enumeration；
- LineVis；
- sample Inspector；
- invalid-file recovery。

---

# 38. Build integration UAT

自动测试通过后，做**有限开发验收**。

使用：

- 一个现有、已配置、轻量的 ARCH Build Profile；
- 优先 CPU / incremental Build；
- 不运行 simulation。

至少验证：

### Successful Build

```text
Studio connects to Host
→ Build Profile Ready
→ Build
→ real stdout visible
→ success
→ expected binary exists
→ binary fingerprint recorded
→ Build Manifest recorded
```

### Source change

```text
successful build
→ modify/copy-safe test change to selected source in disposable UAT workspace
→ Refresh
→ Source changed since build
→ Binary needs build / freshness changes
```

不要在主 accepted source 上做破坏性编辑。

### Failed Build

使用安全、可恢复的方法触发失败，例如：

- disposable UAT workspace；
- invalid test profile/target；
- 已有专用 negative fixture。

验证：

```text
Build failed
→ output retained
→ previous successful manifest retained
→ .par Working Copy retained
```

不要为了制造 compiler error 污染正式 source。

### Concurrent Build

```text
Build active
→ second Build request
→ rejected as busy
```

---

# 39. Automated tests

完成后执行：

```bash
cd studio
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

新增 Build tests 至少覆盖：

```text
profile validation
command injection rejection
spawn success
spawn failure
nonzero exit
stdout/stderr sequence
build busy
manifest creation
binary missing after exit 0
source changed after build
profile changed after build
last successful manifest retained after failure
```

不得削弱现有测试。

---

# 40. Phase 2C acceptance

Phase 2C 只有全部满足才通过：

1. Protocol 1.2 正确协商；
2. `build=true` 只在真实 Build Profile 可用时暴露；
3. Browser 无法提交 arbitrary command / argv / env / cwd；
4. Build 使用 fixed Host-owned profile；
5. 使用 `shell:false` 或等价安全 argv spawn；
6. 一个 Project 同时只有一个 active Build；
7. stdout / stderr 可以实时查看；
8. log buffer 有界；
9. non-zero exit 正确进入 failed；
10. exit 0 但 binary 不存在仍视为 failed；
11. successful Build 生成 Build Manifest；
12. manifest 对应最终 binary fingerprint；
13. last successful manifest 在之后 failed Build 中保留；
14. selected source / explicit tracked input 变化能改变 freshness；
15. 未知 dependency freshness 不被伪装成 current；
16. `.par` 修改不会错误触发 needs-build；
17. Build failure 不损坏 Working Copy / config / Preview；
18. Read-only source view 可用，若本阶段实现；
19. Existing Phase 2B file lifecycle 不退化；
20. Mock / Plotfile / Inspector 不退化；
21. 不运行 simulation；
22. 不实现 Real IC Preview；
23. 不修改 ARCH scientific Core；
24. tests / host tests / lint / typecheck / build / diff check 通过。

---

# 41. Completion report

生成：

```text
studio/PHASE2C_BUILD_INTEGRATION_REPORT.md
```

至少包含：

```text
Baseline
Real ARCH build contract audit
Protocol 1.2
Build Profile
Process execution model
Security boundary
Build event transport
Build UI
Build Manifest
Binary provenance
Freshness semantics
Successful-build UAT
Failed-build UAT
Regression
Automated checks
Deferred
Remaining issues
```

必须明确：

```text
哪些 freshness 是 authoritative
哪些只是 configured/tracked
哪些仍 unknown
```

不要模糊。

---

# 42. Commit / tag

建议 commit：

```text
feat(studio): integrate controlled local ARCH builds
```

建议 tag：

```text
studio-phase2c-v0.7.0
```

不要自动 push。

完成后报告：

- commit hash；
- tag；
- worktree status；
- test summary；
- Build UAT profile；
- remaining issues。

随后：

> **STOP。**

不要自动进入 Phase 2D。

---

# 43. 明确 Deferred 到 Phase 2D

Phase 2C 完成后，以下仍然未实现：

```text
unsaved Working Copy → ARCH
Setup()
Init()
real rho / T / P / Xi fields
uniform preview sampling
Preview request cancellation
Preview config revision
Build ID provenance in preview
real IC Line / Heatmap
real point Inspector
```

这些全部属于：

```text
Phase 2D — Real Initial Condition Preview
```

---

# 44. 明确 Deferred 到 Phase 2E

```text
ParameterMetadata backend
default/source/unit/description/check results
Sod x_pos graphical line
drag marker → parameter
graphical binding schema
Cellular 2D
radius / interface handles
AMR overlay
```

除非 Phase 2D 为真实 Preview 最低需求需要一个极小 metadata subset，否则不要提前实现。

---

# 45. Final rules

```text
Fixed Build Profile
>
Browser command execution
```

```text
Build manifest
>
mtime guess
```

```text
Truthful freshness unknown
>
Fake “up to date”
```

```text
Last successful build provenance
>
Latest attempt only
```

```text
Build state separate from Config state
>
One global status
```

```text
Existing configured build tree
>
Reconfigure the whole project
```

```text
Finish Phase 2C
>
“顺手跑 Setup()+Init()”
```

## User-authorized M0 resolution
Studio development worktree is separate from managed source root /home/arch/projects/ARCH-linux. Use existing build-cuda, target ARCH, output build-cuda/bin/ARCH. Standard cmake --build may internally regenerate by existing rules; no standalone configure, cache edits, tree migration or new tree. Unified binary plus explicitly configured registered case ID; mapping never verified without Core authority. Record source Git HEAD and repository dirty separately from tracked-input freshness. Record pre/post binary and tracked-input fingerprints. No simulation or real Preview.
