# ARCH Studio — Phase 3 Target
## Full Core Local Workflow / Linux-WSL Workstation Integration

> **Source of truth**
>
> This target is derived from:
>
> ```text
> ARCH_STUDIO_FULL_CORE_LOCAL_WORKFLOW_REQUIREMENTS.zh-CN.md
> ```
>
> Current requirement baseline:
>
> ```text
> origin/main:
> 25adec4224497981a0c124a3485f786194975be4
>
> reviewed Studio checkpoint:
> studio-phase2h-v0.19.0
> 840ab538f676f9b898a4680acaabc80201b13647
> ```
>
> **Do not treat these SHAs as permanent.**
> At every stage boundary, fetch `origin/main`, record the new SHA, and re-query the actual Core binary.

---

# 0. Phase 3 purpose

ARCH Studio is now moving from a feature-complete Preview workstation prototype to a **local full-Core workflow frontend**.

The target user workflow is:

```text
Linux / WSL terminal
    ↓
arch-studio
    ↓
independent Studio window
    ↓
select / inspect / edit case source
    ↓
load and edit .par Working Copy
    ↓
inspect all current Core standard parameters
    ↓
Configure / Build
    ↓
real Initial Preview / Initial AMR
    ↓
Run in an independent visible terminal
    ↓
Restart from an explicit checkpoint
    ↓
read-only Plotfile inspection
```

The final product must track the **actual selected Core binary**, not a hard-coded list of example cases or historical parameter counts.

---

# 1. Scope boundary

This phase targets:

```text
Linux / WSL
personal PC / workstation
local Core
local files
local Build
local foreground Run / Restart
local Plotfile inspection
```

Explicitly out of scope for this phase:

```text
Windows-native Core execution
Windows-first launcher qualification
SSH
cluster scheduler
server background jobs
remote worker
runtime in-situ field streaming
MPI deployment planning UI
```

A Windows GUI wrapper may remain in history, but this phase must not rely on it to claim Linux/WSL workflow completion.

---

# 2. Critical current facts

The current mainline Core at the requirement baseline reports:

```text
95 standard parameters
14 registered cases
```

Current full Initial Field Preview and Initial AMR remain:

```text
Sod 1D
CellularDet 2D
```

All registered models can be discovered and inspected, but:

```text
registered
≠ full field Preview
≠ Initial AMR support
```

Current main does **not** track `studio/`.

Therefore the new integration line must be:

```text
latest origin/main
+
reviewed Phase 2H studio/ subtree
+
new compatibility changes
```

It must **not** be:

```text
old Studio branch
+
replace its Core tree again
```

---

# 3. Phase structure

Use the requirement document's A–E delivery structure.

```text
Phase 3A
Latest Main Alignment

Phase 3B
Parameters + Workspace + Workflow Bar

Phase 3C
Linux/WSL Launcher + Local Run / Restart

Phase 3D
Full Registered-Model Initial-State Coverage

Phase 3E
Large / AMR Plotfile Read-only Workflow
```

Strict execution rule:

```text
3A → checkpoint / STOP
3B → checkpoint / STOP
3C → checkpoint / STOP
3D → checkpoint / STOP
3E → final checkpoint / STOP
```

**Only Phase 3A is initially authorized.**

Do not automatically start B–E.

---

# 4. Phase 3A — Latest Main Alignment
## Main-based Studio integration + current Core compatibility

This is the immediate next task.

---

# 5. A0 — fetch and freeze the real baseline

Before changing anything:

```bash
git fetch origin main --tags
git rev-parse origin/main
git rev-parse studio-phase2h-v0.19.0^{commit}
git status
```

Record:

```text
latest origin/main SHA
Studio source checkpoint SHA
date/time
current worktrees
```

If `origin/main` moved beyond the requirement SHA:

> Use the new SHA as the actual baseline and re-audit capabilities.

Never force the branch back to `25adec42` merely to match this document.

---

# 6. A1 — create a main-based integration branch

The new implementation branch must be created from:

```text
latest origin/main
```

Suggested name:

```text
studio/full-core-local-workflow
```

or:

```text
studio/phase3a-main-integration
```

The branch must inherit Core history from main.

Do **not** create Phase 3A by continuing from v0.19 and replacing Core again.

---

# 7. A2 — import only reviewed Studio content

Current main does not track `studio/`.

Bring the reviewed Studio source from:

```text
studio-phase2h-v0.19.0
```

into the new main-based branch.

Preferred conceptual result:

```text
non-studio tree
= latest origin/main exactly

studio/
= reviewed v0.19 Studio
+ Phase 3A compatibility changes
```

Do not import old Phase 2H Core/scientific files over new main.

---

# 8. A3 — tree ownership verification

Before implementation changes, verify:

```text
all non-studio tracked paths match latest origin/main
```

Compare:

```text
path
file mode
object/blob identity where practical
```

Upstream deletions and renames must remain authoritative.

Generate:

```text
studio/PHASE3A_MAIN_BASED_INTEGRATION_AUDIT.md
```

The audit must state:

```text
main SHA
Studio source checkpoint
import method
non-studio tree equality
any exceptions
```

If the non-studio tree cannot be made authoritative without manually rewriting scientific Core:

> STOP and report.

---

# 9. A4 — trusted Build Profile migration

The new Studio must not keep trusting the old Phase 2H workspace.

Create / migrate a Build Profile whose:

```text
project root
source root
build directory
binary path
tracked inputs
manifest
```

all belong to the new main-based integration workspace.

Changing only:

```text
arch-studio --project <new path>
```

is **not** sufficient.

The trusted Build identity must point at this new tree.

---

# 10. A5 — fresh CPU configure/build

Use an independent CPU build tree.

Recommended:

```text
Debug
CUDA OFF
KLU according to current local approved profile
OpenMP ON
BUILD_TESTING ON
```

Do not reuse the old Phase 2H binary as compatibility proof.

No automatic long CUDA build.

---

# 11. A6 — runtime capability snapshot

From the **new binary**, query and archive raw responses:

```text
ARCH --config-schema
ARCH --list-cases
ARCH --preview-capabilities
```

Also inspect at least:

```text
Sod
CellularDet
one inspection-only model
```

Save raw evidence under ignored:

```text
studio/.local/phase3a/
```

Do not hard-code historical counts in production code.

The current baseline evidence is expected to be:

```text
95 standard parameters
14 registered models
```

but runtime output is authoritative.

---

# 12. A7 — 95-key schema compatibility

The current Core catalog adds:

```text
eos_coulomb_mult
hll_wave_speed
dt_max
```

Studio must discover these through schema automatically.

Verify:

```text
search
group
default
type
option
unit
applicability
description/presentation
default→explicit insertion
Save/Reopen
```

Do not write a second frontend copy of their defaults.

---

# 13. A8 — exp(number) expression compatibility

Current Core expression parsing accepts:

```text
pi
-pi
pi*number
pi/number
number*pi
exp(number)
```

Studio local validation must not reject a valid expression accepted by current Core.

At minimum test:

```text
exp(0)
exp(1)
exp(-1)
exp(1e-3)
```

and invalid cases:

```text
exp()
exp(foo)
exp(1)junk
exp(NaN)
exp(1000000)   # must fail if result is non-finite
nested unsupported expressions
```

Do not expand this into an arbitrary math-expression engine.

Local validation should mirror the Core-supported contract, not invent extra syntax.

---

# 14. A9 — 14-model compatibility

Verify the new Studio does not contain an old:

```text
11-model whitelist
14-model static source of truth
```

The UI list must come from:

```text
--list-cases
```

Current known registered models are acceptance evidence only.

For every returned model verify that Studio can show:

```text
registered identity
source/build association if known
inspection capability
field Preview capability
AMR capability
unsupported reason where applicable
```

Do not show Full Preview/AMR just because a model is registered.

---

# 15. A10 — existing Preview/AMR compatibility

Re-run:

```text
Sod 1D Real Initial Preview
Sod graphical binding
Sod Initial AMR

CellularDet 2D Real Initial Preview
CellularDet Initial AMR
Preview Session warm update
Cancel / stale result protection
```

New mainline physics changes must not silently expand Studio support.

---

# 16. A11 — Build input/path migration

Compare the current Build Profile's tracked inputs against new main.

Every tracked path must be classified:

```text
exists and retained
renamed
deleted / no longer relevant
new required input
```

No silent skipping.

Update the Profile and generate a new Manifest from the new workspace.

Continue to report:

```text
dependenciesComplete=false
```

unless an authoritative full dependency mechanism now exists.

---

# 17. A12 — Core / Studio / Host regression

Run the current relevant Core API/scoped tests from the new main-based binary.

Do not preserve an old expected test count if the current registry changed.

Also run:

```bash
cd studio
npm ci
npm test
npm run test:host
npm run lint
npm run typecheck
npm run build
git diff --check
```

No weakening or skipping tests simply to match older Phase 2H numbers.

---

# 18. A13 — Phase 3A UAT

At minimum:

## Parameter compatibility

```text
95-key current catalog
eos_coulomb_mult
hll_wave_speed
dt_max
exp(number)
retired-key behavior from Phase 2H
```

## Model compatibility

```text
actual current registry
Sod Preview
CellularDet Preview
one inspection-only model
```

## AMR

```text
Sod
CellularDet
limited semantics
field/AMR identity
```

## Save lifecycle

```text
default→explicit
Save
Save As
Reopen
Undo
```

---

# 19. A14 — Phase 3A completion report

Generate:

```text
studio/PHASE3A_MAIN_ALIGNMENT_REPORT.md
```

Must include:

```text
actual main SHA
Studio source checkpoint
integration method
non-studio tree verification
new CPU binary fingerprint
actual standard parameter count
actual registered case count
expression compatibility
Build Profile / Manifest
Core test results
Studio test results
Host test results
UAT
remaining gaps
```

Suggested checkpoint:

```text
tag: studio-phase3a-v0.20.0
```

Then:

> **STOP. Do not start Phase 3B without user authorization.**

---

# 20. Phase 3B — Parameters, workspace, and fixed workflow bar
## Deferred until explicitly authorized

Phase 3B implements the workspace and parameter interaction requirements that are not fully covered by Phase 2H.

Major scope:

```text
schema-driven grouping/presentation
Runtime user-oriented ordering
Gravity conditional UX
Diffusion conditional UX
Network/Burn organization
AMR placement
bottom fixed workflow bar
Configure/Build terminal drawer
```

---

# 21. B1 — parameter coverage rule

All current Core standard keys must remain reachable.

UI completeness is measured by comparing:

```text
reachable editable standard keys
vs
runtime --config-schema keys
```

No current Core key may disappear merely because Studio lacks a special hand-authored panel.

---

# 22. B2 — presentation authority

Use Core:

```text
group
presentation
options
applicability
diagnostics
units
toggle
enabledBy
```

Studio chooses layout/order, not scientific meaning.

Raw key remains searchable.

---

# 23. B3 — Runtime ordering

Runtime should prioritize:

```text
compute backend
time integrator
flux solver
reconstruction / limiter
CFL
termination conditions
output
checkpoint / restart
advanced repair/device/time controls
```

Do not simply mirror Core declaration order.

---

# 24. B4 — EOS paths

Keep separate:

```text
eos_table_path
eos_helm_table_path
```

Do not merge into one input.

Applicability comes from current Core.

---

# 25. B5 — Gravity UX

Start from:

```text
gravity_type:
none
external
self
```

`none`:

```text
collapse dependent controls
```

`external`:

```text
show active-axis acceleration inputs
```

`self`:

```text
gravity boundary
gravity_G
Advanced:
gravity_rtol
gravity_atol
gravity_max_cycles
```

Configuration support must not imply Gravity Initial Preview fields.

---

# 26. B6 — Diffusion UX

Use:

```text
use_diffusion
```

as master switch.

Then expose:

```text
diff_integrator
use_thermal_diff
use_viscous_diff
use_species_diff
```

Constant coefficients appear only when Core says they are allowed/applicable.

If Core reports forbidden explicit keys:

> offer explicit Undo-able removal.

Do not hide an invalid key and pretend the error is gone.

---

# 27. B7 — fixed bottom workflow bar

Real Config workspace must have a persistent compact action bar including at least:

```text
Configure (when needed)
Build
Update Preview
Run
Restart from checkpoint
status summary
```

Do not lose workflow actions when switching from Mock to Real.

Build / Configure output goes into an expandable bounded terminal drawer.

Closing the drawer must not cancel the process.

---

# 28. B checkpoint

Generate:

```text
studio/PHASE3B_PARAMETER_WORKSPACE_REPORT.md
```

Suggested tag:

```text
studio-phase3b-v0.21.0
```

STOP.

---

# 29. Phase 3C — Linux/WSL launcher + Run / Restart
## Deferred until explicitly authorized

This is not the old Windows wrapper workflow.

Target default entry:

```bash
arch-studio
```

running inside Linux / WSL.

---

# 30. C1 — launcher

Support:

```bash
arch-studio
arch-studio --case Sod --config ...
arch-studio --project ... --case ... --config ...
arch-studio --project ... --source ... --config ...
arch-studio --binary ...
```

From current directory, discover project upward.

If ambiguous:

```text
Project Picker
```

No npm/Vite/browser/port copy-paste required.

---

# 31. C2 — independent local window

Launch an independent Studio application window.

The packaging architecture must not impose Electron/Node dependencies on ordinary headless Core builds.

Optional CMake Studio build/install target may package matching:

```text
Core
Host
frontend assets
Linux launcher
```

but normal Core build remains GUI-independent.

---

# 32. C3 — Build

Configure/Build remain controlled local operations.

Do not duplicate Make as a second Build action when:

```text
cmake --build
```

already uses the selected generator.

No automatic long GPU rebuild on application startup.

---

# 33. C4 — Run

Run only when:

```text
.par is explicitly saved
case is registered
binary is executable
build identity is accepted
output target is checked
backend selection is clear
```

Core command remains authoritative:

```text
ARCH <ProblemType> <ParFile>
```

Do not invent a new Run CLI.

Run opens in an independent visible foreground terminal with its own:

```text
task identity
process group
stdout/stderr
exit code
```

Preview/Build cancellation must not kill Run.

Closing Studio must not silently terminate an active Run terminal.

---

# 34. C5 — Restart

Restart means:

```text
continue simulation from checkpoint
```

not “rerun failed process”.

User explicitly selects checkpoint.

Use existing Core config contract:

```text
restart
restart_file
```

Do not invent:

```text
ARCH --restart <file>
```

unless Core later provides it.

No silent “latest checkpoint” selection.

---

# 35. C checkpoint

Generate:

```text
studio/PHASE3C_LOCAL_EXECUTION_REPORT.md
```

Suggested tag:

```text
studio-phase3c-v0.22.0
```

STOP.

---

# 36. Phase 3D — full registered-model initial-state coverage
## Core capability Stop Gate

Final requirement:

> Every registered model must have a truthful initial-state presentation appropriate to its real dimensional/physical form.

Current Core does not yet provide complete field/AMR Preview for all registered models.

Therefore first perform a capability matrix audit:

```text
registered model
inspect-case
field Preview
AMR Preview
dimension
geometry
field/state representation
```

Generate:

```text
studio/PHASE3D_CORE_INITIAL_VIEW_GAP.md
```

if Core support is insufficient.

Do not use:

```text
Mock
frontend formulas
fake 2D heatmap
old Plotfile
```

to claim full model support.

For non-spatial models, use a real state presentation only if the Core interface provides the necessary authoritative data.

STOP when Core contract is missing.

---

# 37. Phase 3E — large / AMR Plotfile workflow
## Core/IO Stop Gate

Final requirement includes read-only inspection of complete Core Plotfiles.

Current small 1D viewer does not satisfy this target.

Before implementation, require Core/IO/Host contract for:

```text
HDF5 format version
step/time
geometry
field names
units
centering
shape/order
block ID
AMR level
bbox
leaf status
row/data relationship
atomic completion/publication
```

Host then needs a bounded local query worker for:

```text
viewport query
native-cell lookup
cancellation
cache limit
I/O limit
```

If these interfaces/examples are not available:

generate:

```text
studio/PHASE3E_PLOTFILE_CONTRACT_GAP.md
```

and STOP.

Do not load giant AMR HDF5 files wholesale into the GUI and call it finished.

---

# 38. Plotfile display principles

When Phase 3E eventually proceeds:

```text
main HDF5 = scientific authority
.archidx.h5 = rebuildable query index only
.xmf = third-party bridge only
```

Studio may display LOD/downsampled overview, but Inspector must be able to query the authoritative native leaf/cell value.

Displayed LOD must not be mislabeled as native AMR data.

---

# 39. Global state / identity rules

Across all Phase 3 stages keep separate:

```text
Project
Source
Working Copy
Saved config
Build
Binary
Preview
AMR
Run task
Restart task
Plotfile
```

A success in one state must not imply another state is current.

Examples:

```text
Config Dirty + Preview Current
can be valid

Config Saved + Build Stale
can be valid

Run active + Preview Stale
can be valid
```

---

# 40. Global scientific boundary

Studio may:

```text
display
edit config
validate using Core contracts
launch controlled Core commands
render Core-returned data
```

Studio may not:

```text
reimplement physics
infer arbitrary model formulas
invent unsupported fields
call registered == preview supported
call resource estimate == OOM prediction
call config valid == run scientifically ready
```

---

# 41. Global test boundary

At every checkpoint run the applicable current suites:

```text
Core scoped API tests
Studio tests
Host tests
lint
typecheck
production build
diff check
```

Do not preserve obsolete expected counts.

Report:

```text
actual test registry
actual pass count
actual skipped count
```

No full CUDA/GPU qualification unless separately authorized.

---

# 42. Final Phase 3 acceptance rule

Do not declare:

```text
"Full Core Studio support complete"
```

until A–E are actually closed against the **then-current** Core schema and model registry.

At final acceptance compare:

```text
runtime Core schema
runtime registered model list
Core capability list
Studio coverage matrix
```

and explicitly list any unsupported:

```text
parameter
case
dimension
preview type
AMR type
Run/Restart capability
Plotfile capability
```

Historical success on 95 keys / 14 cases is not permanent proof after Core changes.

---

# 43. Immediate execution instruction

**Current authorization is Phase 3A only.**

Do:

```text
latest-main branch reconstruction
Studio subtree import
trusted Build Profile migration
fresh CPU build
runtime capability snapshot
95-key compatibility
exp(number) compatibility
14-model discovery regression
Sod/Cellular Preview/AMR regression
tests + report
```

Do not:

```text
implement Run/Restart yet
start full-model fake Preview work
build large Plotfile viewer
start SSH/cluster work
```

After `studio-phase3a-v0.20.0`:

> STOP and await user authorization.
