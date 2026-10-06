# ARCH Studio — Phase 2F Requirements
## UI Contract Integration & Usability Closure

**Studio baseline**

```text
studio/phase2e-b-cellular-2d
studio-phase2e-b-v0.10.0
fb22178fe578b17120597633f427e38d2a2be582
```

**Core UI contract**

```text
codex/studio-core-ui-contracts
5e96d4f004c9bd320fb232853d59b90006cdb0f2
```

This document is the requirement source for Phase 2F. It preserves the 2026-09-20 UI review framing and the Core handoff boundaries.

It does **not** expand scientific scope beyond the already supported Sod 1D and CellularDet 2D Initial Preview.

---

# 1. Requirement map

| ID | Requirement | Priority | Main owner |
|---|---|---:|---|
| UI-01 | Network Advanced / ODE coverage | P2 | Studio |
| UI-02 | Complete standard parameter catalog/default entry | P2 | Core contract + Studio |
| UI-03 | Gravity / Diffusion groups | P2 | Studio |
| UI-04 | Geometry-aware coordinate labels | P2 | Core contract + Studio |
| UI-05 | Units | P2 | Core contract + Studio |
| UI-06 | Zoom/data/axis mismatch | P1 | Studio |
| UI-07 | Plot scale/color/range/clipping controls | P2 | Studio |
| UI-08 | Persistent model/config identity + pairing warning | P1 | Host + Studio |
| UI-09 | Old-model metadata isolation | P1 | Studio |
| UI-10 | Source / Preview Model association | P1 | Host + Studio |
| UI-11 | Current README/startup docs | P2 | Studio |
| UI-12 | Stable Grid axis blocks | P2 | Studio |
| UI-13 | Unified type validation + path preflight | P1 | Core contract + Host + Studio |

P1 issues take precedence over cosmetic or convenience work.

---

# 2. Standard parameter catalog

Studio shall consume:

```text
ARCH --config-schema
```

Current contract publishes 90 standard RuntimeParams keys.

Requirements:

- Standard parameters omitted from `.par` are still searchable and editable.
- Do not infer arbitrary model custom parameters.
- Omitted defaults remain absent from `.par` until user edits them.
- Preserve comments, unknown keys, ordering and newline conventions.
- Do not expose reports, caches, derived values or compile-time constants as editable input.

Acceptance:

```text
config does not contain ode_rtol
→ ode_rtol still appears in catalog
→ Core default visible
→ user edit inserts only ode_rtol
→ save/reopen retains value
```

---

# 3. Parameter groups

Required top-level groups:

```text
Grid
EOS
Network
Gravity
Diffusion
Runtime
Custom Parameters
```

Network Advanced must include:

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

Gravity:

```text
gravity_type
gravity_g_x
gravity_g_y
gravity_g_z
gravity_G
```

Diffusion:

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

---

# 4. Three-layer value semantics

UI must distinguish:

```text
Schema Default
Inspection Parsed Value
Preview Effective / Model-read Value
```

Definitions:

- **Schema Default**: standard parsing default from Core catalog.
- **Inspection Parsed Value**: typed current config input before Setup/policy resolution.
- **Preview Effective / Model-read**: actual model Setup read result, only where Preview metadata provides it.

Do not label Inspection parsed value as model-effective value.

If Preview metadata does not report a model-read value:

```text
Model effective value: unavailable
```

---

# 5. Applicability

Inspection may report:

```text
applicable = false
```

Behavior:

- parameter remains visible/searchable;
- existing explicit value remains in Working Copy;
- show not-applicable status/reason;
- do not auto-delete;
- do not auto-insert default;
- do not claim full simulation validation.

---

# 6. Alias handling

Aliases such as:

```text
timeintegrator -> time_integrator
```

must not be presented as two independent controls.

If input uses an alias:

- preserve source semantics;
- expose alias/source information;
- avoid producing both alias and canonical assignments accidentally.

---

# 7. Stable Grid axis layout

Grid always contains three stable peer sections:

```text
x1 / X
x2 / Y
x3 / Z
```

`blocks` remains visible and never moves into Advanced.

Activation rules:

```text
x1: positive integer
x2: 0 off, >=1 on
x3: 0 off, >=1 on
x3 active requires x2 active
```

When inactive:

- keep blocks control visible;
- hide dependent min/max/BC;
- preserve hidden values in Working Copy.

Intermediate/invalid text must not change dimension/layout.

---

# 8. Geometry-aware coordinate labels

Original keys remain:

```text
x1 x2 x3
```

Display labels use Core contract:

| geometry | 1D | 2D | 3D |
|---|---|---|---|
| cartesian | x | x, y | x, y, z |
| cylindrical | r | r, φ | r, z, φ |
| spherical | r | r, φ | r, θ, φ |

Important:

```text
2D cylindrical = r–φ
2D spherical = r–φ
```

Do not substitute other common coordinate conventions.

---

# 9. Units

Use Core-provided unit status.

CGS fields:

```text
DENS  g/cm^3
TEMP  K
PRES  erg/cm^3
VEL*  cm/s
ENER  erg/cm^3
EINT  erg/g
length cm
time s
angle rad
```

IdealGas uses model-defined `code_*` units where Core reports them.

Distinguish:

```text
known
dimensionless
unknown
not-applicable
coordinate-dependent
model-dependent
mixed-state
```

Never infer unit from numerical magnitude or field name alone.

---

# 10. Unified standard input validation

All standard edit routes use the same schema-aware validation:

```text
loaded existing field
new default insertion
paste
numeric scrub
graphical binding
programmatic edit
```

Standard int rejects:

```text
1.5
1.0
1e2
12abc
overflow
```

No truncation or rounding.

Standard float:

- accepts supported finite decimal/scientific notation;
- rejects invalid suffix, NaN, Infinity and overflow.

Expression fields use Core-approved grammar, including documented forms such as `pi` and `2*pi`.

Invalid text remains visible for correction but cannot become validated Save/Preview input.

---

# 11. Host path preflight

Only fields declared as paths by Core receive filesystem checks.

Input file checks:

```text
resolved path
exists
is regular file
readable
```

Output directory/new target checks:

```text
target may not exist
parent exists/is usable
creation/write feasibility
no filesystem side effect during preflight
```

Relative path base:

```text
Core process working directory
```

not browser location or `.par` directory unless Core contract says so.

Host unavailable:

```text
Not checked / Unable to check
```

Old path-check responses must be rejected when input changes.

---

# 12. Persistent current identity

Workspace must always show:

```text
Current Model
Parameter File
```

Secondary details:

```text
Source
Parameter path
```

Requirements:

- use registered case/model identity;
- show full parameter filename;
- allow full path inspection/copy;
- if browser-imported file has no trusted Host path, say so.

---

# 13. Pairing suspicion warning

Pairing checks are advisory, not scientific truth.

Example:

```text
Current Model: CellularDet
Parameter File: Sod.par
```

may show:

```text
Filename suggests this may be intended for Sod. Verify pairing.
```

Do not infer:

```text
same filename prefix = verified
same directory = verified
different directory = wrong
```

Generic names such as `1.par` / `test.par` use neutral “association unconfirmed”.

---

# 14. Preview confirmation vs overwrite confirmation

Pairing warning before first Preview may offer:

```text
Continue Preview
Switch Config
Cancel
```

That confirmation does not authorize file overwrite.

Save with pairing suspicion must show exact overwrite target and separate actions:

```text
Save As
Overwrite <filename>
Cancel
```

External-change conflict protection remains independent.

---

# 15. Metadata isolation

Metadata/bindings are valid only for matching:

```text
project
model/case
build/binary
```

Effective read results additionally require matching:

```text
configRevision
```

Old success may remain visible as history but cannot:

- insert defaults for another model;
- expose another model's editable binding;
- drive current Inspector meaning;
- overwrite current async state.

---

# 16. Source / Preview Model association

Current supported mapping:

```text
Sod -> simulation/Sod/Sod.cpp
CellularDet -> simulation/Cellular/Cellular.cpp
```

Switching model updates source association.

If Source panel is project-level rather than model-level, label it explicitly.

Do not imply:

```text
source selected = compiled
source selected = preview-capable
```

---

# 17. Zoom correctness

Zoom/pan must transform consistently:

```text
data geometry
axes
Sod marker
selected sample marker
hit testing
Inspector mapping
```

Test with:

- Sod discontinuity;
- CellularDet structured field;
- non-square 2D grid.

A fixed physical point retains the same raw Inspector value before/after zoom.

Fit/Reset restores full domain.

Zoom never modifies scientific config or triggers Preview.

---

# 18. Plot settings separation

Two separate setting groups:

```text
Coordinate Axes
Field Values
```

1D:

```text
Spatial X: Linear / Log
Field Y: Linear / Log
```

2D:

```text
Spatial X: Linear / Log
Spatial Y: Linear / Log
Color value: Linear / Log
```

No ambiguous global Log toggle.

---

# 19. Plot advanced ranges

Coordinate axis:

```text
Auto
Manual min/max
Reset
```

Field values:

```text
Auto
Manual min/max
Lower clipping
Upper clipping
Reset
```

Users enter original physical values even under log display.

Invalid ranges are rejected locally.

---

# 20. Log invalid values

Zero/negative values cannot silently enter log rendering.

Never:

```text
abs(value)
hidden epsilon
silent drop
```

Show explicit problem and allow Linear fallback.

Inspector remains raw.

---

# 21. Colormap

At minimum:

```text
Viridis
Hot
```

Additional maps only when actually supported by visualization library.

Selection is display-only.

---

# 22. Clipping

Lower and upper clipping can be enabled independently.

Display must indicate:

```text
Clipped
threshold
```

2D may saturate at colorbar endpoint.

1D may clip visible portions with edge indication.

Never mutate Core response arrays or Inspector raw values.

---

# 23. Display settings isolation

The following are presentation-only:

```text
zoom
pan
linear/log
display min/max
clipping
colormap
```

They must not:

```text
set Config Dirty
write .par
change configRevision
trigger Core Preview
change Inspector raw value
```

---

# 24. README / startup

Top of `studio/README.md` must describe current reality:

```text
Sod 1D Real IC
Sod x_pos binding
CellularDet 2D Real IC
config-schema / inspect-config
Build
Save lifecycle
current Preview timeout
```

Historical Phase 2D-only limitations move out of the primary startup instructions.

---

# 25. Regression boundary

Must retain:

```text
Phase 2B config lifecycle
Phase 2C Build
Phase 2D Sod Real Preview
Phase 2E-A metadata/binding
Phase 2E-B Cellular 2D
Mock
Plotfile
Undo/Redo
numeric scrub
cancel/timeout/race handling
security boundaries
```

No full simulation or CUDA run is required for Phase 2F unless separately authorized.

---

# 26. Explicit deferred scope

Not blockers:

```text
Cellular editable marker
actual AMR hierarchy
3D Preview
new scientific models
generic arbitrary C++ auto-UI
full dependency graph authority
SSH / cluster / scheduler
simulation monitoring / in-situ
native Windows Host qualification
generic external editor launcher
```

---

# 27. Completion definition

Phase 2F is complete only when:

1. Core commit `5e96d4f0` is integrated without main merge or duplicate A/B.
2. Core 8 scoped test groups pass.
3. UI-01..UI-13 each has explicit closure status.
4. P1 items UI-06/08/09/10/13 are resolved or explicitly blocked with evidence.
5. All 90 standard parameters are searchable without writing all defaults.
6. Geometry and units come from Core contract.
7. Model/source/config identity remains truthful across switches and stale results.
8. Zoom/pan preserve coordinate/data/Inspector consistency.
9. Plot display settings remain independent from scientific state.
10. Studio/Host regressions and final desktop UAT pass.
11. `PHASE2F_COMPLETION_REPORT.md` and `PHASE2F_UI_REVIEW_CLOSURE_REPORT.md` are generated.
12. Development stops before Phase 3.
