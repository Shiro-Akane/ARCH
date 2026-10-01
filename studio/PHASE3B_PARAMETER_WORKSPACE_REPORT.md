# Phase 3B — Parameters, Workspace & Workflow Bar

Date: 2026-09-29 (Asia/Tokyo)
Result: **PASS WITH NON-BLOCKING VERIFICATION LIMITATION**
Branch: studio/phase3b-parameter-workspace
Checkpoint: studio-phase3b-v0.21.0 (resolve the annotated tag for the final report commit).

## Baseline and scope

- Authorized baseline: studio-phase3a-v0.20.0 → 03a6f13ba9d5caba1823aae3c6709ea2ae8315d2.
- 3A tag object retained: 80bf3b8889c6e50594720889ab9ae1314e4d1aaf.
- Stage-start/end fetch: origin/main=25adec4224497981a0c124a3485f786194975be4.
- No main merge, reconstruction, Core integration or scientific implementation change. Changes confined to studio/; root STATUS.md untouched.
- Target sections 20–28 and the user's Phase 3B authorization checked at implementation/completion boundaries.
- No Run/Restart execution, full-model Preview expansion, Plotfile implementation, Phase 3C launcher work, or push.

## Delivered requirements

| Area | Implementation and evidence |
| --- | --- |
| Schema coverage | Current --config-schema reports 95 keys; all 95 have controls. Core determines membership/group/type/default/options. Unknown future groups also render. Count is evidence, not a production constant. Search covers keys, aliases, display names and descriptions. |
| Presentation | Core names, descriptions, option labels/accepted spellings, units, applicability, toggle and enabledBy are consumed. Raw keys and provenance remain available. Not-applicable units do not emit a missing-unit warning. |
| Three semantic layers | Defaults stay virtual until edited. Inspector distinguishes Working Copy, Inspection Parsed Value before Setup, and matching Preview model-read metadata. No second default catalog. |
| Runtime | Backend → time integrator → flux/HLL solver → reconstruction/limiter → CFL → termination → output → checkpoint/restart → advanced repair/device/time. Toggle off values come from Core; enabling requires a positive user value, not an invented default. |
| EOS | eos_table_path and eos_helm_table_path remain independent controls and Host path checks. |
| Gravity | none collapses dependents; external shows active-axis acceleration; self shows boundary/G and advanced tolerance/cycles. Waiting for inspection does not flash known irrelevant fields. Error-bearing fields remain reachable. Configuration support does not imply gravity field Preview. |
| Diffusion | Master, independent channels, integrator and Advanced. Coefficient editing requires matching Core permission. Forbidden explicit text/error remains until explicit removal: one Undo, no Save. Later permitted re-edit restores the destination correctly. |
| Network | Common enable/network/NSE/ODE/linear solver/tolerances; other schema controls under Network ODE / Advanced. |
| Grid/AMR | Stable peer x1/x2/x3 blocks. Fixed Grid AMR subsection with regrid interval in AMR Advanced. Transient invalid axis input retains the last valid layout. |
| Workflow | Persistent Configure, Build, Update Preview, Run, Restart from checkpoint, status and terminal toggle. Run/Restart disabled with Phase 3C reasons; no execution handler. |
| Terminal | Persistent shared Build provider owns polling independently of drawer visibility. Bounded stdout/stderr/state events, scrolling and Clear view. Hiding does not cancel/restart a task. |

### Configure boundary

Current Host exposes fixed-profile builds of an existing build tree, not a standalone Configure endpoint. Configure reports that limitation and configured-tree state. The drawer does not fabricate Configure output. No arbitrary program/args/cwd/env/shell interface was added.

### Forbidden-key authority

Core may stop at the first forbidden coefficient before returning the successful diffusion extension. Studio consumes structured INAPPLICABLE_PARAMETER error keys and diffusion.forbiddenExplicitKeys, never keys guessed from message text. Removal triggers a new matching inspection, exposing the next error if present. Collapsing a module does not clear local validation errors.

## Core / Build identity

- Managed project: /home/arch/projects/ARCH-mainline
- Existing build: build-phase3a-cpu (Debug, CUDA OFF, OpenMP ON).
- Binary SHA-256, unchanged before/after UAT Build:
  fb4f9de20f6ca152bf016d56182b15a6760a83224fbec6ab8643a4f0c2b5d9a4
- Actual schema: 95 parameters; registry: 14 models. Existing field/AMR support remains Sod/CellularDet.
- Profile: arch-phase3a-cpu-integration; 94 explicit inputs; dependenciesComplete=false.
- Workflow-bar Build succeeded with "ninja: no work to do"; no standalone configure/reconstruction.
- New successful Manifest: 1acd56b8-f6f3-4db5-add5-c96a371fccd6.
- Manifest source HEAD: 03a6f13ba9d5caba1823aae3c6709ea2ae8315d2; repository dirty at execution from Studio-only edits. Tracked-input freshness is separate; complete dependency freshness is not claimed.

## Automated validation

| Check | Result |
| --- | --- |
| Current Core scoped suite | 11/11 PASS, 190.32 seconds |
| npm test | 177/177 PASS |
| npm run test:host | 70/70 PASS |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS; production dist not committed |
| git diff --check | PASS |

Core tests: preview_initial_conversion, preview_api_contract, configuration_api_contract, preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits, case_inspection_contract, preview_session_contract, preview_verified_resources, preview_exact_sample_cache, preview_cellular_2d. No simulation oracle enabled.

New regressions cover full key reachability, future groups, Runtime order, separate EOS paths, conditional/error precedence, coefficient authority, malformed metadata rejection, not-applicable units, comment-preserving removal/one Undo/re-edit and disabled execution actions.

Full Studio/Host gates ran after the Gravity UAT correction. A final CSS-only font declaration correction was checked with a fresh production build; no logic/Core input changed. Core tests were not repeated after frontend-only edits.

## Production UAT

Node 24.21.0 / npm 11.19.0. Production preview 127.0.0.1:4193, controlled Host 127.0.0.1:4180. Only ignored studio/.local/phase3b/ parameter copies were edited.

| Scenario | Result |
| --- | --- |
| Mock → Real | Workflow actions persist; no simulation action enabled. |
| Runtime, Network, Grid/AMR | Sections/search reachable; 95 unique rendered controls. |
| Gravity | Correct immediate none/external/self layout; only active x1 acceleration for 1D external, self Advanced separated. |
| EOS | Distinct path controls and path-preflight statuses. |
| Forbidden coefficients | Core error shown and Save disabled. Remove alpha_therm, one Undo restores original 1; remove again, then remove nu_visc from the next Core diagnostic. |
| Allowed re-edit | Return to Ideal, set removed alpha_therm to 2. Saved file retains inline comment and omits nu_visc. |
| Save As/Reopen | Host writes saved-parameters.par; filesystem check plus GUI Reopen confirm 2. Original UAT source retains forbidden inputs: no auto Save. |
| Virtual default | eos_coulomb_mult initially absent. Edit does not touch disk. Ideal+0.75 rejected by Core; Save disabled. Restore allowed 1, explicit Save/Reopen succeed. |
| Build/drawer | Start real Build, immediately hide while Building, reopen after success: stdout/state retained, binary hash unchanged. |
| Update Preview | Footer action generates real Sod Preview and returns Current. Existing warm-session/race/cancel regressions pass. |
| Desktop | 1280×720 and 1920×1080 footer bottom matches viewport; no horizontal overflow. 900×720 also has no horizontal overflow and accessible actions. |
| Console | No production console errors/warnings captured. |

UAT correction: Gravity mode switching briefly showed irrelevant controls while inspection was pending. Fixed and verified. Scientific behavior unchanged.

## Limitations / deferred work

1. Browser file-chooser automation could not read either the WSL UNC sample or its Windows-local copy; the browser reported a file-read permission failure. This import route is **not claimed manually verified** here. The same sample loaded via Open Project Config; actual Host Save/Save As/Reopen passed. No permission workaround or picker-code modification.
2. Configure remains the actual Host capability boundary above. Run/Restart execution deferred to explicitly authorized Phase 3C.
3. This is production-browser/WSL UAT, not new Windows packaged-desktop qualification.
4. Complete Build dependency freshness remains unknown, inherited from 3A.

Ignored evidence: studio/.local/phase3b/ logs, final runtime responses, UAT files; Build Manifest in studio/.local/. Screenshot outside Git: E:\.Codex\.ShiroAkane\phase3b-production-uat.png.

UAT services were stopped after verification; no managed Host/Preview worker remained.

## Checkpoint / stop

Annotated checkpoint studio-phase3b-v0.21.0. Resolve its peeled commit for delivery hash. Preserve 3A. No node_modules, dist, .local, screenshots or temporary files in the commit.

**Phase 3B complete. STOP before Phase 3C; no automatic push.**
