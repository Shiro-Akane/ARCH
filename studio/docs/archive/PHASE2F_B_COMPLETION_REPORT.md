# Phase 2F-B Completion Report

Date: 2026-09-21

## Checkpoint and scope

- Baseline: `studio-phase2f-a-v0.11.0` / `7b62bbe758bfee2d0dcebe4ba9fbbe2dbc01df59` (unchanged).
- Branch: `studio/phase2f-parameter-editor`.
- B checkpoint: `studio-phase2f-b-v0.12.0` (resolve its peeled commit with `git rev-parse studio-phase2f-b-v0.12.0^{commit}`).
- Target: `PHASE2F_TARGET.md`, section 8, B1–B12; supporting requirements 2–11 and regression boundary 25.
- Scope: Complete Parameter Editor, Core coordinates/units, Host path preflight. All delivery changes are under `studio/`.
- No scientific Core, root `STATUS.md`, CMake, CPU binary, A checkpoint, or Phase 2F-C changes. No simulation, CUDA baseline, or new ARCH build.
- This records agent-executed production browser UAT; it does not claim an additional user acceptance decision.

## Requirement closure

| ID | Result | Implementation evidence |
|---|---|---|
| UI-01 Network ODE / Advanced | Implemented | `StandardCatalog.tsx` includes all schema Network parameters and the 12 requested ODE/linear-solver keys, including omitted defaults. |
| UI-02 90-key catalog | Implemented | `parameterCatalog.ts`: 90 keys represented by 89 editable controls; alias and canonical share one destination. Global search covers all groups and inactive axes. |
| UI-03 Gravity / Diffusion | Implemented | Dedicated schema-driven groups, Core unavailable-module warning, applicability, Core per-field errors retained with user input. |
| UI-04 Coordinates | Implemented | Core `coordinateSystems` selects all 9 geometry/dimension conventions; original x1/x2/x3 keys retained. No independent coordinate-name mapping. |
| UI-05 Units | Implemented | Schema/inspection unit status and current Core axis units; custom units remain unknown without metadata. Sod x_pos inherits matched Preview binding-axis units. |
| UI-12 Stable axes | Implemented | Three peer sections in fixed order; blocks always exposed. Dependent fields hidden while inactive, without deleting text. Invalid block tokens retain the last valid layout. |
| UI-13 Validation + paths | Implemented for B | Shared schema-aware validation retained for insertion and existing edits; Core field errors prevent validated Save/Preview. Host-only metadata preflight, version/identity checks, offline Not checked. |

## Data semantics and document safety

`Schema Default` is never labeled as `Inspection Parsed Value` or `Model effective value`. The Inspector only receives model-read metadata when the current Preview identity and Working Copy match. Inspection remains explicitly before Setup and does not establish simulation readiness.

Omitted standard values are virtual. A user edit alone inserts the selected key through the existing serializer/history path. Existing aliases are edited in place; canonical takes precedence if both already exist. No parser or serializer changes were needed. Existing comments, unknown keys, order and newline conventions retain regression coverage.

Applicability false is advisory: it remains searchable/editable, is labeled with Core applicability text, and does not delete existing input. Self gravity is displayed as unavailable; no simulation support is implied. Standard options/types come from the Core schema. Existing raw enum spelling is retained.

## Host path preflight

`host/pathPreflight.ts` checks only schema-declared Local Host paths. The browser still sends only project, approved case, text and revision; it cannot choose command, cwd, environment or path role.

- Relative paths resolve against the same Host-managed Core process cwd used for inspection.
- Input: resolved path, existence, regular-file status and read permission.
- Output: existing directory or nearest existing parent, writable/searchable feasibility; unresolved output symlinks are rejected.
- No directory creation, content reads, config writes, Preview or simulation occurs during preflight.
- Results are attached to the inspection envelope and its request/config/project/model/build identity. Edited or disconnected state cannot display old checks as current.
- `ok` is a metadata/permission observation, not validation of an EOS file's contents or a guarantee that a later run can create/write output. Filesystem state can change after preflight.

The schema is cached only for the exact build ID and binary fingerprint. Inspection and checks still verify current build readiness before/after the operation.

## Automated validation

Environment: ARCH-Ubuntu-24.04, Node 24.21.0 / npm 11.19.0.

| Check | Result |
|---|---|
| Core scoped tests | 8/8 PASS; 210.66 s; `ARCH_PREVIEW_SIMULATION_ORACLE=0` |
| `npm test` | 121/121 PASS |
| `npm run test:host` | 46/46 PASS (subset also included in npm test) |
| `npm run lint` | PASS |
| `npm run typecheck` | PASS |
| `npm run build` | PASS |
| `git diff --check` | PASS |

Core groups: configuration_api_contract, mainline_authority, preview_initial_conversion, preview_api_contract, preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits, preview_cellular_2d.

Reused unchanged A CPU executable SHA-256: `8fc913ee9d588d71de8b8b6b52c1039f56e0abb0f1579a72d64e87e61c2c362e`.

New regression coverage: all catalog keys/alias destinations, insertion byte preservation, nine coordinate conventions, transient-invalid topology, authoritative coordinate units, schema-only path checks (readable/missing/directory/unreadable input, new output without mkdir, invalid/dangling output target), stale path-response rejection and invalid unit metadata. Existing lifecycle, build security, Sod binding, Cellular 2D, Mock, Plotfile, undo/scrub, cancellation/race/retention tests remain passing.

An existing large JavaScript chunk warning remains (~6.22 MB uncompressed); it is not a failed build and was not expanded into bundling work.

## Production browser UAT

Dedicated production preview `127.0.0.1:4188` and Host `127.0.0.1:4180`; no existing user tab/server was reused. Test files were restricted to ignored `studio/.local/uat/`.

| Scenario | Observed result |
|---|---|
| All 90 standard keys searched individually | Every query found exactly one corresponding visible/enabled editor; alias query targets the same canonical control. |
| Network defaults / ODE Advanced | All requested keys visible, schema-default/not-written labels; mixed-state ode_atol explanation retained. |
| Omitted ode_max_substeps | Changed to 42, successful real Sod Preview, Save and Reload retained value; no other defaults inserted. |
| Omitted ode_rtol | Changed to 0.0002, Save and Reload retained value; disk comparison proved the only new assignment was ode_rtol. |
| Alias source | Loaded timeintegrator; edited shared control to rk3; Inspector reported alias / timeintegrator; disk retained alias only, with no competing canonical assignment. |
| Applicability false | ODE keys remained visible and explicitly edited values were retained with Core reason. |
| Gravity | self retained and Core UNAVAILABLE_MODULE warning displayed. |
| Helmholtz diffusion | Explicit nu_visc=0.1 retained; Core INAPPLICABLE_PARAMETER surfaced and Save/Preview blocked. |
| Axis transitions | 1D→2D→3D→2D→1D; blocks 0/1/2; blank, -, 1., 1.5, negative and x2=0/x3=1 preserved last valid layout and exposed error. |
| Geometry labels | Cartesian x/y/z; cylindrical 2D r/phi; spherical 2D r/phi and 3D r/theta/phi. All nine combinations additionally covered by regression. |
| Units | Ideal code_length, CGS cm, angular rad; current Sod x_pos Inspector code_length; custom_unknown remains untyped with unknown unit. |
| Path states | Existing relative file resolved from project Core cwd; missing input, directory-as-file, unset input, new output ancestor checked; no output directory created. |
| Host unavailable | Dedicated Host stopped; UI shows Not checked instead of retaining prior pass/fail result. |
| Desktop layout | 1280×720 and 1920×1080 inspected visually; 800×720 narrow window checked for access/overflow. Document scroll width equals viewport in all three sizes. |
| Side effects | No auto Save or auto Preview from search/edit/navigation; only explicit actions generated Sod previews and wrote disposable UAT config. |

## Remaining boundaries / stop

B is complete. No Phase 2F-C plot interaction, log/range/clipping/colormap work or README overhaul was started. Full Phase 2F closure reports remain for C. No Phase 3, new scientific model, 3D Preview, real AMR hierarchy, Cellular editable marker, or native Windows Host qualification is claimed.

The local B checkpoint is created without push. Dedicated test services are stopped after UAT. Stop here and await explicit authorization for C.
