# ARCH Studio progress

## Current Phase
Phase 0

## Current Milestone
M6 — Phase 0 QA — COMPLETE

## Completed
- Created studio/phase0-demo from clean main at 7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866; HEAD remains at that baseline commit. Studio files are uncommitted.
- Installed isolated user-level Node.js 24.21.0 / npm 11.19.0 in WSL; all frontend dependencies live under studio/.
- Created React / TypeScript / Vite shell with Parameters, Preview, Inspector and disabled actions.
- Custom Params is prominent, expanded by default, and collapsible by mouse or keyboard.
- Added responsive layouts, frontend validation scripts and startup documentation.
- Re-read TARGET.md milestone/scope restrictions at completion. M1 now complete; scope checked before starting M2.

## Changed Files
- studio/.gitignore
- studio/DEPENDENCIES.md
- studio/QA-M0.md
- studio/README.md
- studio/TARGET.md
- studio/eslint.config.js
- studio/index.html
- studio/package-lock.json
- studio/package.json
- studio/src/App.tsx
- studio/src/components/Icon.tsx
- studio/src/components/Inspector/Inspector.tsx
- studio/src/components/ParameterPanel/ParameterPanel.tsx
- studio/src/components/Preview/Preview.tsx
- studio/src/components/StatusBar/StatusBar.tsx
- studio/src/main.tsx
- studio/src/styles.css
- studio/tsconfig.json
- studio/vite.config.ts
- studio/STATUS.md

## Dependencies Added
See DEPENDENCIES.md for exact direct dependency versions and licenses. React/ReactDOM plus Vite, TypeScript, types and lint tooling only. No H5Web renderer or backend installed. User-level Node runtime is outside the repository.

## ARCH Core Files Touched
None

## Verification
See QA-M0.md. Frontend lint/typecheck/build passed; browser shell, accordion, disabled controls and responsive layouts checked. No baseline suites or ARCH build were executed.

## Baseline Protection
The actual baseline record is E:\.Codex\.ShiroAkane\wsl-setup\STATUS.md; no root STATUS.md was present. It is retained unchanged (SHA256 CCE6860A4D0E64619E905DC180826BE28CCCE1F3100104771F4C07E560D53379).

## Known Issues
- Phase 0 acceptance complete; limitations are documented in PHASE0_COMPLETION_REPORT.md.
- Upstream THREE.Clock deprecation warning and >500 kB bundle advisory remain.
- Demo Save is memory-only and does not persist across page reloads; no real .par writes.
- Cellular sample/test artifacts are preserved, excluded from M3–M5 work. Mock is the default workspace again.
- Studio changes remain uncommitted.

## Scope Check
On target. Changes confined to studio/ in the repository. No ARCH Core, real .par, real HDF5, CUDA/CMake or Phase 1 work.

## Next Action
STOP. Phase 0 complete. See PHASE0_COMPLETION_REPORT.md; await a new target before Phase 1.

## M1 checkpoint
- Central reducer: saved/dirty/invalid, current/stale/generating/failed, run idle.
- Editable mock parameters with field errors; no real file access.
- 3 state tests, lint and production build passed; browser confirmed dirty/invalid/recovery.
- No new dependencies; scope remains studio only.

## M2 checkpoint
- Independent PreviewData and MockPreviewProvider; all three dimensionless mock fields generated, only Density rendered in this milestone.
- 500 ms asynchronous generation with cancellation and revision guard; invalid inputs block Preview, prior data stays labelled stale.
- Public @h5web/lib HeatmapVis adapter with Viridis, native color bar/interaction capabilities, and Fit reset. Browser rendering verified after explicit user approval.
- npm test: 6/6 passed; npm run lint, typecheck, build passed.
- Rechecked TARGET.md milestones and scope; no M3+ work, no core changes or baseline reruns.
- Additional files: src/state/studioState.ts, src/state/useStudio.ts, src/data/PreviewData.ts, src/data/MockPreviewProvider.ts, src/components/Preview/Renderer.tsx, tests/state.test.ts, tests/mock.test.ts.

## M2 final acceptance — 2026-09-13
- User explicitly approved browser acceptance after the automatic review interruption.
- Browser: generating observed, current reached, editing shows dirty/stale; invalid radius disables Preview.
- Density hotspot moved from (0.5, 0.5) to (0.75, 0.25); increasing radius widened the hotspot and amplitude 2 changed the maximum to about 3.
- Heatmap, Viridis color bar, wheel zoom, drag pan and Fit reset verified.
- Fixed short desktop viewport clipping with an absolutely fitted renderer container; checked 1280x720 and 390x844 without horizontal overflow.
- Browser error-level logs empty; upstream THREE.Clock warning retained.
- Scope rechecked: M3 field switching, M4 point selection, M5 Save/Revert remain unimplemented. No ARCH rebuild/baseline tests or protected file edits.

## Explicit user extension — Cellular result sample (2026-09-13)
User requested running Cellular with the existing executable, then importing those results into the current test program. This narrowly overrides the Phase 0 no-real-HDF5 rule for this static sample only; TARGET.md itself is preserved. No general Phase 1 viewer or automatic M3 continuation.

- Imported initial/final plotfiles from output/cellular_run_20260913_011131; original outputs unchanged.
- Offline script scripts/import_cellular.py uses an isolated studio/.local/import-venv with h5py 3.16.0 and NumPy 2.5.3. This is not a runtime backend.
- src/samples/cellular.json retains Float64 values, actual sorted x coordinates, AMR levels, time and SHA256 provenance.
- Verified 46 blocks x 16 cells = 736 cells per snapshot; cell centers sorted; block edges tile [0,128] without gaps/overlaps; all density/temperature/pressure values finite.
- Read-only CellularSample view shows all three 1D curves and initial/final selector. Top-level sample selector returns to the existing Mock workspace.
- Clearly states 200-step termination before requested tmax and zero transverse perturbation.
- Browser verified all curves, snapshot switching and return to Mock. A historical Vite HMR error during file creation preceded successful reload/build; no application exception observed in final view.
- New files: scripts/import_cellular.py, src/samples/cellular.json, src/components/CellularSample.tsx. Updated App.tsx and styles.css.
- No ARCH Core or baseline STATUS edits, no recompilation, no full baseline reruns. M3–M6 remain pending.

## M3 checkpoint
- Mock default restored; Cellular artifacts preserved but excluded from this milestone.
- Central active field selection displays precomputed Density/Temperature/Pressure instantly, no config/revision changes.
- 7 tests, lint/typecheck/build passed; browser verified temperature hotspot+gradient and pressure step, saved state unchanged.
- Scope rechecked; proceeding sequentially to authorized M4.

## M4 checkpoint
- Data-coordinate click selection maps to containing cell center; all three Mock values shown with a small annotation marker.
- Drag threshold prevents pan from selecting; stale/generating preview cannot produce readings; edits clear selection.
- 9 tests and lint/typecheck/build passed; browser verified center coordinates/values, marker, drag nonselection, field-switch persistence and edit clearing.
- Scope reviewed; proceed only to authorized M5 Demo Save/Revert.

## M5 checkpoint — 2026-09-13
- Central config/save snapshots working parameters and marks saved; invalid input cannot overwrite the snapshot.
- config/revert restores the latest saved copy, cancels pending preview requests, clears point selection and errors. Existing preview becomes current only when its parameter snapshot matches the restored config; otherwise stale.
- Browser verified save x=0.7, edit to 0.9, revert to 0.7; invalid radius disables Save; Revert restores valid values. Matching generated preview returns to current on Revert.
- 12 state/provider/selection tests, lint, typecheck and production build pass.
- TARGET.md M3/M4/M5 and scope restrictions rechecked at each checkpoint. No new dependencies; ARCH Core and baseline untouched. M6 not started.

## M3–M5 changed files
- src/state/studioState.ts: field selection, point selection, Save/Revert and preview parameter snapshot.
- src/data/selection.ts: independent cell lookup and all-field readout.
- src/components/Preview/Renderer.tsx: H5Web data-coordinate selection and annotation.
- src/components/Preview/Preview.tsx: field selector and interaction wiring.
- src/components/Inspector/Inspector.tsx: Mock coordinate/value readouts.
- src/components/StatusBar/StatusBar.tsx: Demo Save/Revert actions and M5 label.
- src/App.tsx: centralized dispatch wiring and Mock default.
- src/styles.css: field selector and selected-cell marker.
- tests/state.test.ts, tests/selection.test.ts; README.md; STATUS.md.

## M6 final checkpoint
- Rechecked all 22 TARGET.md acceptance criteria; detailed evidence in PHASE0_COMPLETION_REPORT.md.
- 12/12 tests, lint/typecheck/build, production dependency audit pass. Dev and production browser workflows verified, including failure recovery and responsive layouts.
- Generated DEPENDENCY-LICENSES.json and PHASE0_COMPLETION_REPORT.md.
- Core unchanged; baseline and target hashes verified. No automatic Phase 1.

## Phase 0 checkpoint submission
- User accepted the Phase 0 report and requested a checkpoint on studio/phase0-demo.
- Audited Cellular experiment/fixture: no dependency edges to/from MockPreviewProvider and its local dependency closure. Only App composes the two views; fixture remains in the same bundle.
- README now explicitly labels experiment/fixture and optional offline tooling; no code adjustment or directory refactor was needed.
- Commit includes studio source, tests, configuration, lockfile, fixture and documentation. Ignored node_modules, dist, caches and local Python environment are excluded.
- Pre-commit tracked diff and staged path audit must contain no ARCH Core files. Historical uncommitted references above describe earlier milestone snapshots.
- Stop after committing; no Phase 1, real HDF5/.par or ARCH interface work.

## Release checkpoint — studio-phase0-v0.1.0
- Phase 0 formally accepted by the user. Release checkpoint only; Phase 1 remains out of scope.
- Repository-local Git author: _Ding <d979212462@gmail.com>; global Git configuration unchanged.
- Existing Phase 0 checkpoint: 57836fc75d7582c74b160a2f423bac133fea4068.
- Release commit message: feat(studio): complete Phase 0 frontend prototype.
- Tag: studio-phase0-v0.1.0. Resolve this tag for the release commit hash.
- Release gate: npm test, npm run lint, npm run typecheck, npm run build.
- Production dist and release documentation are packaged separately; ignored node_modules, .local, caches and dist are not committed.
- No feature or directory refactor, no ARCH Core changes, no further HDF5/.par/Build/Run integration.

## Phase 1A / P1A-M0 complete
- Started clean from tag studio-phase0-v0.1.0 at 286b476c3c7ca97fa102a865c18e0af3acd5cc7a on studio/phase1a-plotfile.
- User scope: stop after P1A-M5 and produce an interim report; no M6/M7 checkpoint yet.
- Scope reviewed against PHASE1A_TARGET.md; only studio changes allowed, no ARCH rerun/rebuild or AMR reconstruction.

## P1A-M1 complete
- Browser opened existing SodBeginner_HLLC_plt_0003.h5 read-only through h5wasm 0.10.3.
- File handles and virtual files are released after reading; 16 MiB local-file limit.
- Scope reviewed: no disk scanning, backend, ARCH run or rebuild.

## P1A-M2 complete
- Browser confirms Sod metadata time=0.15, dim=1, geometry=cartesian and actual Data fields DENS, ENER, PRES, VELX.
- Metadata/discovery isolated in PlotfilePreviewProvider; typecheck passes.
- Scope reviewed: no guessed fields, no hierarchy parsing.

## P1A-M3 complete
- Sod DENS browser adaptation: 64 samples, min=0.125, max=1.
- Dedicated LinePreviewData leaves Phase 0 heatmap contract unchanged. Coordinate/value sorting preserves pairing; unique uniform coordinates required.
- Adapter tests pass for unknown names, range, nearest sample and invalid arrays.
- Scope reviewed: only Grid/x; no level/morton, ghosts or AMR reconstruction.

## P1A-M4 complete
- H5Web LineVis renders real Sod DENS curve in browser; screenshot inspected.
- Renderer consumes only LinePreviewData; existing Heatmap Renderer unchanged.
- Field selector uses discovered raw names. Typecheck passes.
- Scope reviewed: 1D only, no source-specific renderer branch.

## P1A-M5 complete — stop at user boundary
- Real Inspector displays file, time, dimension, geometry, selected raw field, min/max and selected x/value.
- Click selection verified on Sod PRES: sample 33, x=0.5078125, value=0.3054751636143017; independently matches h5py read of existing file.
- Browser verified Mock switch and 512x512 generation, non-HDF5 error with cleared old data, reopening valid data and sample-number selection.
- Final gate: 14/14 tests (12 original unchanged + 2 adapter tests), lint, typecheck, build and git diff --check pass.
- Build warning: main bundle 6.12 MB / gzip 1.39 MB, includes HDF5 WASM. No performance optimization scope added.
- Scope review: M0-M5 delivered; M6 complete error matrix and M7 checkpoint are not claimed. No ARCH rebuild/run/baseline retest. All changes remain in studio/; no new commit/tag.
- Interim report: PHASE1A_M5_REPORT.md. Stop here as requested.

## M6 follow-up work — final browser gate pending
- Added reproducible unchanged real Sod fixture and h5wasm integration tests for metadata, actual fields, unknown name, min/max and exact sample value.
- Added invalid HDF5/non-ARCH/missing Data/missing attributes/unsupported type/empty/NaN/Inf/read-failure coverage.
- Extracted the existing latest-request guard for direct regression testing; old success/failure cannot replace a newer result, current errors recover, empty chooser selection is a no-op.
- 22/22 tests, lint, typecheck, build and diff whitespace checks pass on the current source. Phase 0 tests retained.
- Refreshed dependency license inventory (294 records); full h5wasm license retained.
- Final browser verification was rejected twice by automatic approval review, which treats the earlier M5 stop as still binding and does not accept the goal-continuation message as new authorization.
- M6/M7 completion is not claimed. Await explicit user confirmation to continue browser verification and checkpoint submission. No commit/tag was created.

## P1A-M6 complete
- User explicitly confirmed continuation after the earlier M5 stop; prior pending-approval entries are historical.
- Final browser regression: unknown custom_species field enumerates, renders and reports x=0.0078125/value=1; Mock switching generates a current 512x512 preview with MOCK / DEMO label.
- Cancelled selection is covered at the application's empty-selection boundary by unit test; native dialog cancellation is not automated by the browser tool.
- All nine error categories in the target have application-level test coverage; actual invalid-file/recovery UI verified during M5. Request success/failure race protection is directly tested.
- Final source checks remain 22/22 tests, lint, typecheck, build, diff check passing. No source changes since those successful checks.
- Scope reviewed: no ARCH Core, baseline, AMR or later-phase expansion.

## P1A-M7 release checkpoint
- Completion evidence and limitations: PHASE1A_COMPLETION_REPORT.md.
- Dependency inventory refreshed; h5wasm license copied verbatim. Only studio/ source, tests, small real fixture and documentation belong in the checkpoint.
- Commit message: feat(studio): integrate real 1D ARCH plotfiles.
- Tag: studio-phase1a-v0.2.0; resolve the tag for the final commit hash.
- No automatic push or Phase 1B. Stop after local commit/tag verification.

- Staged whitespace audit: original target/license whitespace preserved as documented exceptions; remaining staged paths pass. Fixture uses repository Git LFS rules.
