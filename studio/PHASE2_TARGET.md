# Phase 2 naming clarification

User confirmed Phase 1C2 Manual UAT passed and is sealed. This stage is Phase 2; the attached Phase 3A scope below is adopted unchanged, with P2-M0 through P2-M9, branch studio/phase2-local-host, report PHASE2_LOCAL_HOST_REPORT.md and tag studio-phase2-v0.5.0. Later-stage labels in the original are historical roadmap references, not authorization.

# ARCH Studio — Phase 3A Local Host Foundation & Contract Freeze

> **Active stage only.**
>
> This task is the first implementation slice of Phase 3.
> It is intentionally narrower than the full “本地文件接入与初始预览需求”.
>
> Phase 3A does **not** implement Build, real `Setup()+Init()` preview, 2D Cellular interaction, SSH, scheduler, or remote execution.
>
> The purpose of this stage is to freeze the Local Host boundary and make ARCH Studio able to identify and inspect one local project safely, so later Build / Preview work has a stable contract instead of temporary glue.

---

# 0. Why Phase 3 is split

The full Phase 3 requirement covers several independent high-risk systems:

```text
Project / filesystem
        ↓
Source/config/binary identity
        ↓
Save + external change detection
        ↓
Build
        ↓
Real Setup()+Init() preview
        ↓
Graphical parameter binding
```

Implementing all of them in one Codex task would mix frontend state, filesystem permissions, process execution, C++ build identity, scientific preview provenance, conflict handling and UI / UX.

That is explicitly forbidden in this stage.

Phase 3 roadmap:

```text
Phase 3A
Local Host boundary + project session + read-only identity/fingerprints

Phase 3B
Real local file lifecycle
Save / Save As / external change conflict

Phase 3C
Build integration
source/binary freshness + compiler output

Phase 3D
Real Initial Condition Preview
working .par → compiled ARCH → Setup()+Init() → PreviewData

Phase 3E
Graphical parameter bindings + 2D expansion
Sod handle / Cellular bindings
```

Only **Phase 3A** is active now.

---

# 1. Frozen baseline

Start from the latest accepted ARCH Studio frontend checkpoint currently in Git.

Do not assume a historical hash from chat.

Before any edit, report:

```bash
git status
git branch --show-current
git rev-parse HEAD
git describe --tags --always --dirty
```

Requirements:

- working tree state must be understood before editing;
- do not reset / discard user work;
- do not modify existing release tags;
- do not touch ARCH Core;
- create a dedicated Phase 3A branch from the actual accepted Studio baseline.

Suggested branch:

```text
studio/phase3a-local-host
```

---

# 2. Phase 3A one-line goal

Implement:

```text
ARCH Studio UI
      ↓
LocalHostAdapter contract
      ↓
loopback-only local host service
      ↓
one explicitly authorized project root
      ↓
read-only project/source/config/binary inspection
```

At the end of Phase 3A, Studio should be able to answer:

```text
Which project is open?
Which case source is selected?
Which .par is selected?
Which executable is associated?
What files changed since the project session was loaded?
```

It should **not yet** compile or generate a real scientific preview.

---

# 3. Architecture decision for this stage

Preserve the current Web-native React frontend.

Add a minimal local process boundary:

```text
React / TypeScript
      ↓
LocalHostAdapter
      ↓ HTTP / structured local protocol
127.0.0.1 only
      ↓
local host service
      ↓
filesystem / file fingerprints
```

Preferred implementation for the current repository:

> a small Node-based local host service, because the Studio already uses the Node / TypeScript toolchain.

Do **not** introduce:

- Tauri;
- Electron;
- Rust;
- Python backend;
- SSH;
- Slurm;
- a new large application framework.

A future packaged launcher may replace the development startup flow without changing `LocalHostAdapter`.

---

# 4. Security rules — mandatory

The local service is a privileged boundary compared with the browser UI.

It must follow all of these.

## Network

Bind only:

```text
127.0.0.1
```

Never:

```text
0.0.0.0
```

No LAN exposure.

## Project root

The service starts with one explicit authorized project root, for example:

```bash
npm run local-host -- --project /home/user/ARCH
```

Phase 3A may use this explicit launch argument instead of implementing a native folder picker.

Do not fake a native folder picker in the browser.

## Path confinement

Every filesystem request must be resolved against the authorized project root.

Reject:

- `../` traversal;
- absolute paths outside project root;
- symlink escapes outside project root where reasonably detectable;
- arbitrary user-supplied OS paths from normal API calls.

## No arbitrary command endpoint

Forbidden:

```text
POST /shell
POST /exec
POST /run-any-command
```

No API may accept an arbitrary shell command string.

Phase 3A does not spawn CMake, Ninja, ARCH, editor, or shell.

## Read-only

Phase 3A local host performs no project-file writes.

No:
- Save;
- Save As through LocalHost;
- source modification;
- generated build metadata;
- build output;
- Preview output.

Those belong to later stages.

---

# 5. Freeze the contracts before implementation

Create a small contract layer shared conceptually by frontend and local host.

Suggested location:

```text
studio/src/host/contracts.ts
```

or an equivalent clearly isolated path.

The exact naming may follow the repository style.

At minimum define these contracts.

---

# 6. Host status contract

```ts
interface HostInfo {
  protocolVersion: string;
  hostKind: "local";
  platform: string;
  projectRoot: string;
  capabilities: HostCapabilities;
}

interface HostCapabilities {
  readProject: boolean;
  writeConfig: boolean;
  build: boolean;
  preview: boolean;
  watchFiles: boolean;
}
```

For Phase 3A:

```text
readProject = true
writeConfig = false
build = false
preview = false
watchFiles = limited / false unless explicitly implemented
```

The UI must not enable future actions based on wishful assumptions.

---

# 7. Project session contract

Define a project-session model similar to:

```ts
interface ProjectSession {
  projectId: string;
  displayName: string;
  projectRoot: string;

  caseSource?: ProjectFileRef;
  parameterFile?: ProjectFileRef;
  executable?: ProjectFileRef;

  sourceState: SourceState;
  configFileState: ConfigFileState;
  binaryState: BinaryState;

  openedAt: string;
}
```

The exact fields may differ, but these concepts must remain separate.

Do not collapse everything into:

```text
projectStatus = "ok"
```

---

# 8. File identity contract

Use a stable read-only fingerprint:

```ts
interface ProjectFileRef {
  relativePath: string;
  kind: "case-source" | "parameter" | "executable" | "other";
  exists: boolean;
  size?: number;
  modifiedTime?: string;
  sha256?: string;
}
```

Requirements:

- paths sent to the UI should be project-relative when possible;
- full path can exist in expandable project details;
- hash calculation must be bounded and deliberate;
- do not hash huge unrelated output trees automatically.

For Phase 3A, hash only the explicitly selected / configured project files.

---

# 9. Source / Config / Binary states

Create distinct state enums.

Suggested:

```ts
type SourceState =
  | "missing"
  | "available"
  | "changed"
  | "unknown";

type ConfigFileState =
  | "missing"
  | "available"
  | "changed-externally"
  | "unknown";

type BinaryState =
  | "missing"
  | "available"
  | "stale"
  | "unknown";
```

Important:

> Phase 3A is not allowed to claim `binary = stale` unless the evidence required to prove staleness actually exists.

If build provenance is not available yet:

```text
BinaryState = available / missing / unknown
```

is safer than inventing freshness.

Do not use `mtime(source) > mtime(binary)` as a complete build-freshness solution.

---

# 10. Future contracts must be declared, not implemented

Phase 3A should define minimal future-facing types so later phases do not invent incompatible APIs.

These contracts may be incomplete and versioned.

## Build

```ts
interface BuildRequest {
  projectId: string;
  caseId: string;
}

interface BuildEvent {
  requestId: string;
  state: "queued" | "running" | "succeeded" | "failed" | "cancelled";
  stream?: "stdout" | "stderr";
  text?: string;
}
```

Do not implement build execution in Phase 3A.

## Preview

```ts
interface PreviewRequest {
  projectId: string;
  caseId: string;
  configText: string;
  configRevision: string;
  requestedFields?: string[];
}

interface PreviewEnvelope {
  requestId: string;
  configRevision: string;
  buildId?: string;
  data?: unknown;
}
```

Do not implement `Setup()+Init()` in Phase 3A.

## Parameter metadata

Define a conservative schema such as:

```ts
interface ParameterMetadata {
  key: string;
  valueType?: "int" | "float" | "bool" | "enum" | "string";
  defaultValue?: unknown;
  source?: "explicit" | "default" | "unknown";
  unit?: string | null;
  description?: string | null;
  min?: number;
  max?: number;
  enumValues?: string[];
}
```

Missing metadata must remain missing.

Do not derive description / unit / range by guessing parameter names.

## Graphical binding

Define only a placeholder contract, for example:

```ts
interface ParameterBinding {
  parameterKey: string;
  kind: "axis-position" | "radius" | "unknown";
  axis?: "x" | "y" | "z";
  min?: number;
  max?: number;
}
```

Do not auto-generate bindings from parameter names.

---

# 11. Local host service endpoints

Keep the service minimal.

Suggested endpoints:

```text
GET  /api/health
GET  /api/host
GET  /api/project
GET  /api/project/files
POST /api/project/refresh
```

No generic filesystem browsing API.

No arbitrary path API.

No process execution API.

Responses must be typed and validated.

---

# 12. Project bootstrap for Phase 3A

Phase 3A does not need a polished native project picker.

The local host may be launched with explicit project settings:

```bash
npm run local-host --   --project /home/arch/projects/ARCH-linux   --case relative/path/to/case.cpp   --config relative/path/to/case.par   --binary relative/path/to/ARCH
```

or an equivalent small development config.

The browser UI then connects to the host and creates a Project Session.

This is deliberately temporary UX.

Do not spend Phase 3A building an OS-native project launcher.

---

# 13. Frontend project area

Add a compact Project / Local Host area using the existing Studio visual language.

At minimum show:

```text
Project
ARCH-linux

Case source
cases/.../Sod.cpp

Config
cases/.../sod.par

Executable
build/.../ARCH

Host
Local · Connected
```

Each object should have its own truthful state.

Examples:

```text
Source: available
Config: available
Binary: available
```

or:

```text
Binary: status unknown
```

Do not show Build/Preview readiness if not proven.

Full paths may be expandable.

---

# 14. Do not replace existing `.par` editor yet

The current `.par` working-copy implementation is already stable.

Phase 3A should integrate project identity around it, not rewrite it.

Allowed:

```text
Project Session identifies the selected .par
↓
existing ParDocument / Working Copy opens it
```

Not allowed:

- new parser;
- new serializer;
- changing round-trip behavior;
- local-host Save implementation;
- replacing existing Save As semantics.

Real Save-in-place comes in Phase 3B.

---

# 15. Refresh / fingerprint behavior

Provide a deliberate:

```text
Refresh Project State
```

operation.

It should re-read fingerprints for selected:

- case source;
- parameter file;
- executable.

If a selected file changed since the project session snapshot:

show truthful status such as:

```text
Source changed externally
Config changed externally
```

Do not automatically reload the current `.par` working copy in Phase 3A.

Do not silently overwrite user edits.

If a conflict is detected:

> report it only.

Conflict resolution belongs to Phase 3B.

---

# 16. File watching

Continuous file watching is optional in Phase 3A.

If implemented:

- only watch explicitly selected files;
- debounce events;
- do not recurse through the entire repository;
- do not automatically reload content;
- only update external-change state.

If file watching increases platform complexity:

> defer it and use manual Refresh Project State.

Do not let file watching block Phase 3A completion.

---

# 17. Case ↔ binary mapping

The full requirement says the core/local side will verify the selected case against the executable.

Phase 3A must **not invent this mapping**.

Allowed:

- display a configured case ID if explicitly supplied by the local host;
- display `mapping: unknown` if no authoritative interface exists.

Forbidden:

- infer binary compatibility from filenames;
- parse arbitrary C++ to guess registration;
- assume one executable contains the selected case;
- claim the binary is scientifically valid.

Later core integration must provide authoritative mapping.

---

# 18. No C++ static analysis

Do not parse arbitrary `case.cpp` to automatically discover:

- all `config.Get()` calls;
- parameter defaults;
- units;
- descriptions;
- physical meaning;
- parameter bindings.

Simple source viewing/search can be implemented later, but Phase 3A is not a C++ parser project.

If the metadata contract is not supplied by ARCH Core:

show:

```text
metadata unavailable
```

---

# 19. No build implementation

The following remain disabled / unavailable:

```text
Build
Build & Preview
Start
Stop
Monitor
```

UI may show:

```text
Build integration not connected
```

or capability-based disabled actions.

Do not call:

- CMake;
- Ninja;
- Make;
- compiler;
- ARCH executable.

Do not create a shell endpoint “for later”.

---

# 20. No real Preview implementation

Current Mock and real Plotfile viewing remain unchanged.

Do not implement:

- `ARCH --preview`;
- `Setup()+Init()`;
- PreviewExtractor;
- uniform sampling;
- real IC fields;
- AMR reconstruction;
- preview process cancellation.

Only define the Preview contract for future use.

---

# 21. Error handling

At minimum cover:

```text
Local host not running
Protocol version mismatch
Project root missing
Selected source missing
Selected .par missing
Selected executable missing
Path outside project root
Permission denied
File changes during refresh
Malformed host response
```

Requirements:

- Studio must not crash;
- existing Mock / Plotfile modes must remain usable where appropriate;
- error messages should identify the object that failed;
- failed refresh must not destroy the previous known project session.

---

# 22. Security tests

Add explicit tests for:

```text
../ path traversal
absolute outside-root path
encoded traversal
symlink escape where supported
arbitrary command field rejection
unknown endpoint behavior
```

The local host must not become a generic local remote-control server.

---

# 23. Protocol versioning

Define:

```text
protocolVersion
```

from the first Local Host version.

Frontend must detect incompatible versions and show:

```text
Local Host version is incompatible with this Studio build.
```

Do not silently continue with mismatched contracts.

---

# 24. Development startup

Provide one documented development workflow.

Example:

```bash
# terminal 1
cd studio
npm run local-host -- --project ...

# terminal 2
cd studio
npm run dev
```

or an equivalent combined script:

```bash
npm run dev:local
```

The local host must bind loopback only.

Do not require ARCH rebuild for Phase 3A startup.

---

# 25. Phase 3A milestones

Execute strictly in order:

```text
P3A-M0  baseline / branch / architecture audit
P3A-M1  shared LocalHost contracts + protocol version
P3A-M2  loopback-only read-only local host skeleton
P3A-M3  project root confinement + security tests
P3A-M4  project / source / config / executable identity
P3A-M5  frontend LocalHostAdapter + connection states
P3A-M6  Project Session UI
P3A-M7  refresh / fingerprint / external-change reporting
P3A-M8  error recovery + regression
P3A-M9  docs / completion report / checkpoint
```

Every milestone:

1. complete;
2. test;
3. update `studio/STATUS.md`;
4. scope review;
5. continue only if still inside Phase 3A.

---

# 26. Regression requirements

Must preserve:

## Existing frontend
- mode selector;
- Mock Demo;
- Preview state;
- Inspector;
- Hotspot sliders;
- Undo / Redo;
- numeric scrub;
- parameter panel;
- Raw `.par`.

## Real `.par`
- working copy;
- round-trip preservation;
- Custom Params;
- Save As;
- Revert;
- Invalid state.

## Real Plotfile
- real 1D `.h5`;
- field enumeration;
- LineVis;
- point/sample Inspector;
- invalid file recovery.

## UAT fixes
- dark plot presentation;
- contextual Inspector;
- Preview retention;
- stale provenance;
- Preview canvas centering;
- current Update Preview interaction.

---

# 27. Automated checks

At completion run:

```bash
cd studio
npm test
npm run lint
npm run typecheck
npm run build
```

Also run local-host-specific tests.

No existing test may be weakened merely to pass.

Do not rerun ARCH CPU/CUDA scientific baseline unless separately requested.

---

# 28. Phase 3A acceptance

Phase 3A passes when:

1. Studio can connect to a loopback-only Local Host.
2. Protocol version is visible and checked.
3. One explicitly configured project root is opened.
4. Studio shows the selected source `.cpp`.
5. Studio shows the selected `.par`.
6. Studio shows the selected executable.
7. Missing files are represented truthfully.
8. Selected-file fingerprints can be refreshed.
9. External selected-file changes are detectable via refresh or optional watcher.
10. Existing `.par` Working Copy still functions.
11. External changes do not silently overwrite Working Copy.
12. No arbitrary filesystem path outside project root can be read.
13. No arbitrary command execution endpoint exists.
14. Mock / Plotfile functionality still works.
15. Build remains unimplemented.
16. Real IC Preview remains unimplemented.
17. ARCH Core is untouched.
18. tests / lint / typecheck / build pass.

---

# 29. Completion report

Create:

```text
studio/PHASE3A_LOCAL_HOST_REPORT.md
```

Required sections:

```text
Baseline
Architecture
Protocol version
LocalHost capabilities
Security boundary
Project session example
Files / fingerprints
Errors tested
Regression
Automated checks
Deferred to Phase 3B / 3C / 3D / 3E
Remaining issues
```

Do not claim Build or Preview support.

---

# 30. Commit / tag / stop

Suggested commit:

```text
feat(studio): add local host project session foundation
```

Suggested tag:

```text
studio-phase3a-v0.5.0
```

Do not push automatically unless explicitly authorized.

After reporting the commit hash:

> **STOP.**

Do not automatically start Phase 3B.

---

# 31. Explicitly deferred

## Phase 3B — local file lifecycle
- Save in place
- Save As through LocalHost
- atomic write
- external-edit conflict resolution
- recent projects / project switching polish

## Phase 3C — Build
- build manifest
- authoritative source/binary freshness
- case ↔ binary verification
- compiler stdout/stderr
- Build / Build & Preview state

## Phase 3D — Real Initial Preview
- unsaved Working Copy → ARCH process
- Setup()+Init()
- real preview sampling
- fields / units / provenance
- request ID / config revision / build ID
- cancellation / stale response prevention
- 1D Sod acceptance

## Phase 3E — graphical binding + 2D
- authoritative binding metadata
- Sod `x_pos` marker
- drag → parameter → one Undo
- 2D preview
- Cellular binding
- no UI-side physics inference

---

# 32. Final rules

```text
Stable contract
>
Temporary glue
```

```text
Loopback-only narrow API
>
Generic local control server
```

```text
Explicit project root
>
Arbitrary filesystem access
```

```text
Truthful unknown state
>
Guessed build validity
```

```text
Define Build / Preview contracts
>
Implement them prematurely
```

```text
Preserve current Studio
>
Rewrite frontend architecture
```

```text
Finish Phase 3A
>
“顺手做 Phase 3”
```
