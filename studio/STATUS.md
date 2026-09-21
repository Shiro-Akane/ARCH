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

## P1B-M0 complete
- Clean baseline studio-phase1a-v0.2.0 resolves to 3692d5b64ec75733846e705990f6707d5444ebb7; created studio/phase1b-config.
- Phase 0 Mock and Phase 1A Plotfile browser regression passed immediately before branch creation; no baseline rerun.
- Observed .par grammar: ConfigParser.h strips text after first #, splits at first =, trims ASCII spaces/tabs/CR/LF. No INI sections, quoting or escape processing. Lines without = are ignored by ARCH but retained as raw by Studio.
- Known value forms: GetBool accepts case-insensitive true/false only; GetInt/GetDouble use stoi/stod (prefix acceptance possible); GetString returns trimmed literal. RuntimeParams supports scientific notation and limited pi expressions for coordinate/gravity fields. Studio will preserve unfamiliar literals and reject ambiguous edited numeric tokens rather than emulate unsafe prefix conversion.
- Duplicate-key behavior: exact case-sensitive key, last occurrence wins (map assignment). UI edits only last effective occurrence and identifies its line.
- Unknown-key behavior: all keys retained in parser map and copied to custom numeric/string maps by RuntimeParams. No inferred units/ranges.
- Representative fixtures: simulation/Sod/Sod_beginner.par, simulation/Cellular/Cellular.par, simulation/GaussianPulse/Gaussian.par (2*pi expression).
- Range evidence: RuntimeParams.h validates refine_threshold in [0,1], derefine_threshold >=0 and < refine_threshold. Configuration editing only, no AMR implementation. cfl and custom values get no guessed slider.
- Scope checked against PHASE1B_TARGET.md. No Core/Setup/Init/Build/Run changes.

## P1B-M1 complete
- ParDocument preserves original text and value offsets; effective entries use confirmed last-wins semantics.
- Real Sod/Cellular/Gaussian no-edit round trips pass; single edit changes only selected value, comments/raw lines/CRLF/BOM preserved. Unsafe value injection rejected.
- 3 parser tests and typecheck pass. Scope reviewed: no runtime/solver invocation.

## P1B-M2 complete
- Browser loads sod.par read-only, filename and Config saved visible; all effective keys rendered.
- Working copy state tests verify saved/dirty/invalid/revert, original-text preservation and Mock stale invalidation. Latest request gate reused.
- Typecheck passes; scope reviewed, no initializer or file mutation.

## P1B-M3 complete
- Core Grid/EOS/Network/Runtime now persistent sections in real and Mock panels. Custom remains collapsible. Existing fixed-width panel and parameter-scroll retained.
- Browser confirms real Sod Core contents present without disclosure clicks; typecheck passes. Scope reviewed: no layout/theme/renderer redesign.

## P1B-M4 complete
- refine_threshold uses explicit RuntimeParams [0,1] evidence; no slider guessed for cfl/tmax/custom keys.
- Browser verified exact value 0.7654321, keyboard slider update to 0.766, and invalid 1.2 retained without clamp.
- Slider coarse step 0.001 is UI-only; precise input has no quantization. Range tests/typecheck pass. Scope reviewed.

## P1B-M5 complete
- Custom fields retain raw names/text with no inferred unit/range; known bool/use_nse enum/numeric controls follow verified accessors.
- Validation covers finite numeric tokens, 32-bit integers, bool/enum, explicit ranges, threshold ordering and required restart_file. Duplicate editor identifies last effective occurrence.
- 9 config tests and typecheck pass. Raw ignored lines retained; syntax not recognized by schema is preserved, not guessed. Scope reviewed.

## P1B-M6 complete
- Revert restores loaded text/state; Save As exports a new _modified.par and does not claim in-place access or completed disk Save.
- Browser downloaded sod_modified.par; Windows readback exactly equals original with only tmax 0.15 -> 0.25. Revert restored 0.15.
- Export/round-trip tests and typecheck pass. Config remains mounted across view switches. Scope reviewed; original files unchanged.

## P1B-M7 complete
- Final 34/34 tests, lint, typecheck and build pass; Phase 0/1A tests retained. No dependency or lockfile change.
- Browser: real config load/second file, saved/dirty/invalid, Revert, actual Save As download, Custom text controls, slider keyboard/numeric sync, Mock current generation, Plotfile metadata/fields/LineVis/Inspector and error recovery verified.
- Desktop-first QA per latest user instruction: 1280x720 and 1920x1080 screenshots inspected; no horizontal overflow, Core sections persistent, controls reachable through panel scrolling. 1440x900 panel has overflow:auto (732px viewport / 3280px content).
- Narrow-window smoke check only: no horizontal overflow or inaccessible controls. Mobile is not a target; no dedicated mobile UX/features added.
- Parser handles exact ASCII trim and preserves BOM as part of the key, matching ConfigParser (no silent key normalization). Prototype-like keys use plain dictionaries. Finite restricted pi expressions validated.
- Real fixture copies byte-identical to repository sources. Scope reviewed: no ARCH Core/rebuild/Setup/Init/Build/Run/SSH/AMR work.

## P1B-M8 checkpoint
- Completion report: PHASE1B_COMPLETION_REPORT.md, including limitations and acceptance evidence.
- Commit: feat(studio): integrate real ARCH parameter working copy.
- Tag: studio-phase1b-v0.3.0 (resolve tag for commit hash).
- Only studio/ code, tests and documentation; no node_modules/.local/dist. Original imported target/fixture whitespace retained as documented exceptions.
- Stop after local commit/tag verification; no automatic push or next stage.

## P1C-M0 complete
- Clean baseline studio-phase1b-v0.3.0 = 9609c9d4e5f8ea289b19119a503c223f7d88d8a9; created studio/phase1c-parameter-ui.
- Existing Phase 0/1A/1B gates and browser flows verified in preceding checkpoint; source unchanged at branch start.
- Before screenshots inspected at 1280x720 and 1920x1080 using sod.par. Before panel scrollHeight=3280; preview region 788x656 and 1384x1016 respectively. Left panel width at 1280 is 268px.
- Full PHASE1C_TARGET.md read. Only presentation changes; parser/serialization/parameter schema semantics frozen. No extra references or dependencies needed.

## P1C-M1 complete
- Four persistent Core Block buttons with selected state and actual-value summaries; browser confirms Grid cartesian/1D/4 blocks X, EOS ideal, Network disabled, Runtime tmax 0.15. Typecheck passes. Scope reviewed.

## P1C-M2 complete
- Only selected Core block renders details, in both Real Config and Mock panels. Browser Runtime selection hides geometry, shows tmax, and leaves Config saved. Typecheck passes. Scope reviewed.

## P1C-M3 complete
- Explicit nblockx2/nblockx3 topology drives 1D/2D/3D presentation; absent/invalid topology conservatively shows all axes. Inactive fields remain in Advanced.
- Dimension and summary tests/typecheck pass, no document mutation or schema change. Scope reviewed.

## P1C-M4 complete
- Known domain min/max, boundary left/right, refinement levels and internal-energy bounds render in pairs. Missing keys display Not specified without insertion. Browser confirms X domain pair and inactive X2 hidden. Typecheck passes. Scope reviewed.

## P1C-M5 complete
- Default source line text removed; tooltip retains raw key, source line, type and duplicate semantics. Explicit limited labels used, unknown names unchanged. Browser confirms Geometry info and Left/Right boundary. Typecheck passes. Scope reviewed.

## P1C-M6 complete
- Current-block search includes Advanced keys; Custom search matches raw key/known label/value and opens filtered All Custom Parameters without scientific grouping.
- Browser finds inactive x2_min and rho_right while Config remains saved. Presentation tests/typecheck pass. Scope reviewed.

## P1C-M7 complete
- Dependency-free read-only loaded Raw source with line numbers; browser confirms exact original comment/key lines. No alternate editable form or serializer changes.
- Core navigation sticky within single scroll region; Mock Custom also searchable/collapsed. Typecheck passes. Scope reviewed.

## P1C-M8 validation record
- Target rechecked. Final test 37/37, lint, typecheck and build pass; no application edits after these gates.
- Before/after screenshots inspected at 1280x720 and 1920x1080. Default Sod scroll content 3280 -> 1063 px, sidebar 268 px unchanged; Preview 788x656 / 1384x1016 unchanged. 2560x1440 and 900px window no horizontal overflow.
- Browser: Mock Preview/Inspector; real Plotfile PRES/LineVis/Inspector; Real Config block/search/source, dimension visibility, numeric/slider, Invalid/Revert verified.
- Save As export tests and unchanged handler pass, but this session's in-app browser download did not yield a confirmed file. Disk delivery remains an explicit validation limitation, not a claimed pass. Historical Phase 1B download evidence retained without rerun claims.
- Core, parser/serialization/schema/state, ConfigControl, Plotfile and dependencies unchanged. No baseline rerun.

## P1C-M9 checkpoint
- Target rechecked; PHASE1C_UI_COMPLETION_REPORT.md records changes, measurements and validation limits.
- Commit: refactor(studio): simplify parameter panel information architecture.
- Tag: studio-phase1c-v0.4.0 (resolve tag for commit hash).
- Local checkpoint only; excluded node_modules/.local/dist. Imported target whitespace retained verbatim.
- Stop at Phase 1C; no automatic push or next stage. Browser download-to-disk confirmation remains outstanding.

## P1C1-M0 complete
- Clean baseline d621ad9ae7c69e2dc565b1c84e712bbf0cbdf319; branch studio/phase1c1-control-polish created from studio-phase1c-v0.4.0. Target fully read. Prior gates retained; download handoff limitation retained.
- Audited ConfigPanel, ConfigControl, RuntimeParams, ConfigParser and PolicyDescriptor. UI metadata will not change parser/schema validation. Scope reviewed.

## P1C1-M1 complete
- Custom English Open Config button invokes hidden native picker; filename/no-file state visible. Typecheck passes; target scope reviewed.

## P1C1-M2 complete
- Compact filename/state, transient operation feedback, two actions; preserved lines moved to Raw, implementation footer removed. Typecheck passes; target scope reviewed.

## P1C1-M3 complete
- Native numeric spinners hidden in Parameter Panel. Real numeric arrows disabled absent explicit UI stepping contract; direct typing retained. Typecheck passes; scope reviewed.

## P1C1-M4 complete
- PolicyDescriptor-backed dropdowns include aliases and raw fallback. EOS input tabular distinguished from internal identifiers; network registry remains text. Dedicated raw-preservation test and typecheck pass. No schema/validation edits; scope reviewed.

## P1C1-M5 complete
- Existing verified bool checkboxes retained with contract tooltip and invalid accessibility state. Non-bool tokens retain raw editor; use_nse remains tri-state. par-validation/par-state tests and typecheck pass. Scope reviewed.

## P1C1-M6 complete
- Explicit refine_threshold slider plus exact numeric preserved; coarse-step explanation moved to tooltip. Numeric inputs retain scientific notation and negative sentinels, no guessed min/max/step. Custom/expression remain text. Range/export tests and typecheck pass; scope reviewed.

## P1C1-M7 complete
- Source/type/allowed values/range contract info retained in tooltips; invalid controls show borders and short errors, full issue list collapsed. Typecheck passes. Scope reviewed.

## P1C1-M8 complete with recorded browser limitation
- Target reviewed. Final 38/38 tests, lint, typecheck and build pass. Existing Phase 0/1A/1B/1C regressions retained.
- Desktop screenshots inspected at 1280x720 / 1920x1080; Preview 788x656 / 1384x1016 unchanged. 2560 and 900 px checks no horizontal overflow.
- Browser confirms custom picker, loaded enum spelling, precise scientific notation with no arrow stepping, Invalid gating and Revert. Save As request feedback works; actual disk delivery remains unconfirmed as at Phase 1C.

## P1C1-M9 checkpoint
- Target scope reviewed; PHASE1C1_CONTROL_POLISH_REPORT.md includes A-D before/after, evidence, fallback rationale and limitations.
- Commit: refactor(studio): polish config controls and parameter feedback.
- Tag: studio-phase1c1-v0.4.1 (resolve for hash). Local only; no push or next stage.

## UATF-M0 complete
- Clean baseline studio-phase1c1-v0.4.1 = 5dd195138afca5372d79ab9c9aab7689854d6b8a; branch studio/phase1c2-uat-fixes. Target fully read; UAT-01 through UAT-20 inventory in UAT_FIX_TARGET.md. Prior independent clean-install gates passed 38/38.
- Root cause identified: RenderBoundary key includes edit revision and preview state, remounting renderer during edits. Undo/scrub absent. No ARCH baseline rerun. Final human UAT/Windows download proof required before checkpoint.

## UATF-M1 complete
- Four concise mode/input descriptions; centered empty Preview with Generate action. Presentation test/typecheck pass. Target scope reviewed.

## UATF-M2 complete
- Plotfile custom picker and dark select use existing UI styles; metadata/field reader unchanged. Request/cancel tests and typecheck pass; scope reviewed.

## UATF-M3 complete
- Real Config focus/click selects current parameter details with verified metadata; Mock removes Composition/AMR placeholders. Existing Plotfile Inspector remains actual-data based. Selection tests/typecheck pass; scope reviewed.

## UATF-M4 complete
- Verified H5Web theme CSS variables set dark canvas/readable axes without data/colormap changes. Minimum typography raised locally, no global scaling. Typecheck passes; visual QA scheduled M11. Paper theme deferred as optional. Scope reviewed.

## UATF-M5 complete
- Mock Temperature uniform ambient + radial hotspot; pressure step explicitly labelled; intensity friendly label keeps raw key. Mock X/Y explicit [0,1] slider + exact text entry, invalid values not clamped. Mock tests 4/4 and typecheck pass; Real schema unchanged. Scope reviewed.

## UATF-M6 complete
- Removed edit/state-derived renderer remount key. Stale retains prior data; explicit Generate/Update/Retry only, duplicate generation guarded. Real Config no longer presents a Mock point viewport. State tests 7/7 and typecheck pass. Crossfade optional deferred. Scope reviewed.

## UATF-M7 complete
- Separate Mock/Real edit histories; Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z; native range gesture transaction merges edits. Revert resets history to loaded/saved snapshot. History/state tests 9/9 and typecheck pass. Scope reviewed.

## UATF-M8 complete
- Shared numeric input supports click/type, 4px horizontal drag, magnitude-relative sensitivity, Shift fine, pointer capture, Esc restore. Known integer/range honored, unknown text unchanged. Gesture history tracks latest value independently of render timing. Scrub/history tests and typecheck pass; scope reviewed.

## UATF-M9 complete
- Core navigation becomes compact four-column sticky row after scrolling. Logo is non-clickable brand. Browser context menu untouched. History/scrub tests and typecheck pass; scope reviewed.

## UATF-M10 implementation
- Real Plotfile/Cellular explain 1D Profile in one sentence. Cellular processing details moved into existing collapsed provenance. Warnings about incomplete archived run retained. Config shortcuts cover Inspector focus. Scope reviewed.

## UATF-M10 complete
- Provenance/1D presentation checks included in final regression; no source data changes. Scope reviewed.

## UATF-M11 in progress: human acceptance required
- Final automated gates: 47/47 tests, lint, typecheck and build pass. Existing HDF5 negative-test diagnostic and Vite size warning unchanged.
- Browser QA: real numeric scrub 4 -> 8 -> one Undo -> 4; Mock numeric Undo/Redo, explicit Update action and stale prior image, parameter Inspector, source-backed enum fallback, real PRES LineVis and metadata verified.
- Dark H5Web uses outer background and transparent overlay to preserve visible data. 1280x720 and 1920x1080 screenshots inspected; 2560x1440 layout no overflow. New mode guidance occupies existing header height.
- Frozen production candidate copied to /home/arch/uat-phase1c2-candidate/dist; source/dist SHA-256 manifest beside it. No final commit/tag before human UAT and Windows Save As confirmation.
- UATF-M12 pending; do not claim full acceptance or automatically enter next phase.

## Targeted fix regression complete (v2 target)
- The active v2 targeted-fix scope supersedes the previous open-ended UAT continuation for this round only. Existing broad UAT work is preserved in ee63416e; original dirty workspace remains untouched.
- Retained Mock preview across config/source changes, canvas-relative status centering, and nearby Hotspot Update Preview implemented.
- Production browser reproduction and existing Sod Config/Plotfile regression passed; final 49/49 tests, lint, typecheck, build and diff checks passed. See TARGETED_FIX_REGRESSION_REPORT.md for evidence and limitations.
- Focused commit only; no release tag/push or Phase 2. Historical human UAT / Windows Save As acceptance is not claimed.

## Final targeted-fix checkpoint
- Focused implementation: 5ce8a07027d6fef9062e3641d7f00746544daaac; branch studio/preview-targeted-fixes was clean before this documentation update.
- User explicitly confirmed Windows Save As disk persistence passed manual acceptance, superseding that specific pending item above. No broader UAT result is inferred.
- Final local checkpoint: studio-phase1c2-v0.4.2, on the subsequent documentation-only acceptance commit. Existing 49/49 tests, lint, typecheck and build remain applicable; implementation unchanged.
- No feature changes, push, or Phase 2 work.

## Phase 1C2 final human acceptance
User explicitly confirmed the independent Manual UAT passed. v0.4.2 remains immutable.

## P2-M0 complete
Baseline defc0ff7ccbf5304b63e90cd4792575f49a74006 / studio-phase1c2-v0.4.2 verified clean before creating independent studio/phase2-local-host worktree. npm ci passed. Architecture audit: isolated Node service, shared versioned contracts, selected-file read-only identity, explicit UI connection. Existing editor/viewers remain independent. Original target fully read; scope unchanged except phase naming authorized by user.

## P2-M1 complete
Shared version 1.0 contracts define separate file states, project identity and disabled capabilities. Build/Preview/metadata/binding are declarations only. Typecheck passed. Scope reviewed against PHASE2_TARGET.md.

## P2-M2 complete
Loopback-only service skeleton with exact Host/Origin validation, dedicated request header, fixed routes and no request bodies. Socket/API test passed. No filesystem or process endpoint. Scope reviewed.

## P2-M3 complete
Plain relative paths only; symlink components refused, regular files only, no-follow/nonblocking opens, Linux descriptor target recheck before reading. Selected files bounded at 64 MiB. Traversal/encoded paths/absolute paths/symlink escapes/permission denied/missing root and API tests passed (3). Scope reviewed.

## P2-M4 complete
CLI requires an explicit root; only selected files receive bounded SHA-256 inspection. Session UUID, timestamps, paths, separate states and explicit unknown mapping/unavailable metadata implemented. Capabilities deny write/build/preview/watch. Four host tests and typecheck passed. Scope reviewed.

## P2-M5 complete
Loopback-only HTTP adapter validates protocol, capabilities, session and selected fingerprints with response size/time limits. Offline, mismatch, malformed and oversized response checks passed; typecheck passed. No automatic connection/reload. Scope reviewed.

## P2-M6 complete
Compact collapsible Project area shows protocol, connection, separate identities/states, expandable fingerprints and project details. No connection on mount; no link to working-copy mutations. Render tests 3/3, lint and typecheck passed. Scope reviewed.

## P2-M7 complete
Refresh compares against immutable session-open fingerprints, detects external changes/removal, leaves binary mapping unknown, and retains previous known data on refresh errors. Concurrent refreshes share one in-flight operation. Six host tests and typecheck passed. No editor reload, writes or watcher. Scope reviewed.

## P2-M8 complete
New host tests cover path confinement, permission/missing/root errors, bounded and changing-file reads, explicit changed states, failed-refresh retention/recovery, narrow HTTP API and invalid adapter responses. Original 49 regressions preserved. Browser integration found native fetch receiver bug; corrected and protected by a regression test. Both Edge and in-app browser connected and displayed protocol/source/config/missing binary. Browser refresh retained Mock edit and stale image; real sod.par nblockx1 4->8 remained Dirty/8 after refresh, Revert returned 4. Existing parser/viewer components unchanged. Scope reviewed. Final gates recorded in report after M9 documentation.

## P2-M9 complete
PHASE2_TARGET.md preserves the authorized scope/naming; README documents WSL startup, exact origin, fixed UI host port, read-only limitations and separate editor identity. PHASE2_LOCAL_HOST_REPORT.md includes all required report sections. Final tests 59/59; host-specific 9/9; lint/typecheck/build/diff check PASS. Only studio/ changed; Core/parser/solver/fixtures unchanged. Local checkpoint studio-phase2-v0.5.0; no push, no subsequent-stage implementation. Development QA servers stopped after verification.

## P2B-M0 complete
Baseline studio-phase2-v0.5.0 / 92e260a419120679d313f23753c59061991b8371 verified clean; independent studio/phase2b-config-lifecycle branch/worktree created. npm ci and all 59 baseline tests passed. Target fully read and copied to PHASE2B_TARGET.md. No original worktree or Core changes.

## P2B-M1 complete
Protocol 1.1 and lifecycle/read/write/error contracts declared. Adapter rejects 1.0. writeConfig remains false until safe write endpoints are implemented; validation now permits explicit true or false capability. Protocol tests and typecheck passed. Scope reviewed.

## P2B-M2 complete
GET /api/config reads only the launch-selected .par, returns exact UTF-8 (including BOM/CRLF), fingerprint and project identity. Read capped at 1 MiB with existing no-follow/descriptor consistency protection. Explicit association helper feeds unchanged loadPar and keeps loaded/saved fingerprints distinct. Both server and adapter require protocol 1.1. Host suite 10/10 and typecheck passed; lifecycle round-trip test added. Scope reviewed.

## P2B-M3 complete
Save preparation validates exact text/size and expected fingerprint, rereads selected disk version, rejects conflict and checks file permission without truncation. Conflict preserves original bytes. Config/lifecycle tests 3/3 and typecheck passed. Save route remains unexposed pending atomic implementation. Scope reviewed.

## P2B-M4 complete
Same-directory exclusive temporary file, bounded exact text, mode preservation, file fsync, second fingerprint check and atomic rename implemented. Linux directory descriptor pins destination parent. Injected write/rename failure and intervening external change preserve original and clean temp. Atomic tests 2/2 and typecheck passed. Scope reviewed.

## P2B-M5 complete
Save/Save As narrow endpoints implemented with protocol/header checks, strict request fields and 1 MiB JSON body limit. Project operations serialize. Save As atomically links completed temporary file without overwrite and changes current parameter association only on success. Traversal/absolute/encoded/backslash/NUL/symlink/existing target/missing parent tests pass. Linux writeConfig now true because safe write methods exist; other platforms remain false. Host suite 14/14 and typecheck passed. Scope reviewed.

## P2B-M6 complete
Frontend explicit project load, local Save/Save As, association, separate disk state, conflict message and explicit Reload integrated around unchanged ParDocument/parState. Successful saves rebuild only the saved snapshot using existing parser and leave Preview state unchanged; failed calls keep the editor. Typecheck/lint pass. Unsaved replacement guard follows in M7 before UAT. Scope reviewed.

## P2B-M7 complete
Unsaved replacement guard covers Open Project Config, Reload and browser file open. Save/Save As/explicit discard/cancel offered; unassociated configs cannot Save in place, browser download fallback remains. Reconnect updates identity only and never replaces editor. Busy operations disable editing. Revert remains saved in-memory snapshot; Reload goes through guard. Tests 65/65, typecheck/lint pass. Scope reviewed.

## P2B-M8 complete
HTTP end-to-end tests cover exact text, strict protocol/fields, 413 limit, concurrent saves (one succeeds, one conflicts), Save As current identity and no-clobber. Atomic/write-security tests include outside/encoded/backslash/NUL/symlink/missing-parent and cleanup. Host suite 15/15; full suite previously 65/65 before newest HTTP test; lint/typecheck/build pass. Existing parser/serializer untouched. Limited UAT follows using disposable configuration copies only. Scope reviewed.

## P2B-M9 complete
Finite production-browser UAT verified explicit Load, 4->8 Save/Reload, 8->12 Revert, external-edit conflict without Refresh, Working Copy retention, rejected outside Save As, successful new current config and guarded Discard/Reload. Actual disk bytes/original preservation/temp cleanup checked on disposable copies. This is agent QA, not new human acceptance. Final tests 67/67, host subset 16/16, lint/typecheck/build/diff check PASS. README and PHASE2B_CONFIG_LIFECYCLE_REPORT.md document scope, algorithms and limitations. Target reviewed; Core/parser/root STATUS untouched. Local checkpoint studio-phase2b-v0.6.0; no push or Phase 2C.

## P2C-M0 complete
Phase 2B tag 71d97b0ff8de2d1d4924a71f5e19fa2b2d8d984e verified clean. Independent studio/phase2c-build-integration worktree created. Read-only audit confirmed original ARCH-linux source root and existing CUDA Release tree; Ninja dry-run regeneration is glob-check conservatism (all seven inventories match, no newer/missing regeneration inputs). User authorized managed project separate from Studio checkout, standard build including internal regeneration, no standalone configure. Unified ARCH binary/runtime registry; mapping configured only. Scope reviewed.

## P2C-M1 complete
Protocol 1.2 and typed Build/Profile/Event/Manifest/Provenance contracts added. Existing 67 tests and typecheck PASS. Build capability still false until runner validation. Scope reviewed against PHASE2C_TARGET.md and user M0 resolution.

## P2C-M2 complete
Host-owned existing CUDA profile, cache source/build binding validation, fixed /usr/bin/cmake argv runner, shell:false and allowlisted environment implemented. One active build per session enforced before async validation. Module-only test seams are not HTTP inputs. Profile/path/missing-CMake tests 2/2 and typecheck PASS. Scope reviewed; no ARCH build executed.

## P2C-M3 complete
Typed bounded polling endpoints expose latest build state and build-ID-scoped events. Logs strip terminal controls, split to 4096 characters/event and retain at most 256 Ki characters/1024 events with explicit truncation. Strict two-field Build request; no argv/env injection path. Focused tests 4/4 and typecheck PASS. Scope reviewed.

## P2C-M4 complete
Independent Build panel displays managed source root, profile paths/target, configured case identity, build result, bounded plain-text output and clear-view. Polling validates project/build IDs and sequences; reconnect disabled during active Build. No editor/Preview callbacks. Adapter/UI tests 4/4; lint/typecheck PASS. Scope reviewed.

## P2C-M5 complete
Success now requires exit 0, expected regular confined binary fingerprint and successful atomic manifest publication under ignored managed-project studio/.local. Manifest records pre/post inputs/binary, managed root, Git HEAD/repository dirty, profile hash, target/directory and timestamps. Failed attempts retain last successful manifest, including across service initialization. Process/manifest tests 3/3 and typecheck PASS. Scope reviewed.

## P2C-M6 complete
Freshness compares profile hash, saved binary and explicit tracked-input fingerprints. Changed tracked inputs (or unstable during-build snapshot) require build; incomplete dependency coverage and changed profile/binary stay unknown. Repository dirt and .par edits do not cause needs-build. Refresh updates only provenance. Focused tests 4/4 and typecheck PASS. Scope reviewed.

## P2C-M7 complete
Fixed selected-source endpoint uses confined no-follow bounded reads; UI provides plain read-only source and in-text search, no editor launch/file browser. Managed source/build/output paths and configured registry case identity shown separately from Studio checkout. Source/UI tests 4/4, lint/typecheck PASS. Scope reviewed.

## P2C-M8 complete
Security/concurrency tests reject command/args/cwd/env/shell/binary injection, unknown profile and stale event IDs; exercise spawn/nonzero/missing-output failure, bounded logs, symlink/cache binding and input changes during build. Fixed finish-before-freshness race; active reservation remains until provenance finalization. 77/77 tests, host subset 25/25, lint/typecheck/build PASS. Scope reviewed. Real Build not yet executed.

## P2C-M9 complete
Production UI initiated authorized real ARCH existing CUDA Release target ARCH with parallelism 4. Actual output: glob recheck, ninja no work to do; succeeded, binary and all 11 tracked inputs unchanged, manifest on disk. Repository dirty true correctly independent of freshness unknown. Dirty nblockx1=8 and old Mock Preview retained. Source viewer located real Sod registration. Isolated non-ARCH process fixture verified copied-source change -> needs-build, failed output/last success/editor/Preview preservation; concurrent requests returned 202/409. No simulation, standalone configure or Core edits. Scope reviewed.

## P2C-M10 complete
PHASE2C_BUILD_INTEGRATION_REPORT.md and README document managed/development checkout separation, real build/no-op evidence, manifest, truthful freshness and deferred limits. Final 78/78 tests, host subset 26/26, lint/typecheck/build/diff check PASS. Core/parser and original managed worktree status unchanged. Delivery limited to studio/; no runtime artifacts. Target and M0 resolution reviewed. Local checkpoint studio-phase2c-v0.7.0; no push, no Phase 2D.

## P2D-M0 STOP GATE
Base studio-phase2c-v0.7.0 / 332af5675768123cf23fbdf5e3dc3cb6f44a327d verified clean. Read-only audit found simulation CLI and internal Setup/InitializeData/Init, but no callable approved init-only Preview contract. Current path creates output/log/sidecar and enters driver; no structured sample/provenance/cancellation interface. PHASE2D_CORE_PREVIEW_CONTRACT_GAP.md records evidence and a proposal requiring Core approval. M1-M10 not started; protocol 1.2/preview=false unchanged; no binary execution, build, simulation or Core modifications. Documentation only; no success tag.

## P2D-M0 v2 revalidation — STOP unchanged
Fully read v2 target and replaced PHASE2D_TARGET.md with the exact attachment. Only Diagnostics/Metadata wording differs; scope and Stop Gate unchanged. Git checkpoint reverified; sealed Phase 2C remains clean; no new Core/helper changes or contract approval. Existing call-path audit remains valid. Gap report updated; no M1-M10, protocol change, baseline rerun, binary execution or implementation.

## P2D-M0 upstream integration verification
Target v2 reviewed. API commit 4c0fd5c integrated without Core conflict in isolated worktree; documentation conflict resolved preserving Studio. Core Preview-only tests: 10 PASS, 1 simulation oracle skipped; initial conversion PASS. Stop Gate YES. No simulation or managed-root edits. See PHASE2D_UPSTREAM_PREVIEW_API_AUDIT.md.

## P2D-M1
Target sections5-20 rechecked. Protocol1.3, separate Core schema1.0, fixed Sod Preview Profile and separate CPU integration Build Profile declared. Existing CUDA profile/root unchanged; explicit tracked inputs expanded for API provenance.

## P2D-M2
Target stdin/security scope rechecked. Fixed Sod command, exact UTF-8 stdin, one active request, last successful manifest/input/binary readiness checks implemented. Original project .par is never written by Preview.

## P2D-M3
Target schema/identity/bounds rechecked. Core and Host envelopes validated independently: exact identity, 1D finite monotonic coordinates, bounded fields, lengths/extrema/duplicate keys, execution flags. Host provenance snapshots include build ID and executable SHA.

## P2D-M4
Target cancellation/race scope rechecked. Owned process group SIGTERM then bounded SIGKILL, 30s timeout, one active Preview, strict request cancel route, Build/Preview mutual exclusion, post-run input/binary recheck and retained last success implemented. Frontend revision guard follows in M5.

## P2D-M5
Target Working Copy/retention boundaries rechecked. RealInitPreviewProvider uses exact existing serializer output, SHA-256, fixed profile requests, validated identities. ConfigPanel exposes opaque Working Copy without parser or round-trip changes. Revision acceptance compares exact content and project.

## P2D-M6
Target1D visualization rechecked. Real Config uses authoritative fields and existing LineRenderer, sample selection/index inspector and provenance; Mock/Plotfile providers remain separate. No graphical parameter marker or binding.

## P2D-M7
Target diagnostics/retention rechecked. Previous successful curves retained across edit/generation/failure/cancel; exact content/project guard rejects obsolete response. Current additionally requires matching ready build/binary. Dirty config may have current Preview; Save is independent. Core diagnostics and unavailable units/metadata shown truthfully.

## P2D-M8
Target negative/security/race list rechecked. Focused13 tests PASS: fixed input/provenance, manifest/binary/source readiness, injected command/args/env/cwd, protocol1.2 rejection, bounds, process-group cancel/timeout, malformed/oversized/nonzero output, schema/identity/array validation, changed-source and revision races. Initial regression exposed4 hardcoded protocol1.2 fixtures; migrated those positive fixtures to shared protocol constant, preserving negative-version assertions.

## P2D-M9 in progress
Target finite Sod UAT reviewed. Real Host/API checks passed: baseline512 samples, six real fields, changed unsaved x_pos changes returned DENS, correct SHA revisions, invalid x_pos returns Core error and retains last success, original .par and output inventory unchanged. UI UAT pending: browser auto-review rejected opening local production URL by referring to the earlier Manual UAT prohibition. No browser operation occurred; authorization question sent. No completion tag yet.

### M9 pending UI authorization — automatic regression collected
92/92 npm test PASS;38/38 test:host PASS;lint/typecheck/production build PASS;unstaged and staged diff checks PASS. Source/Core/CMake/simulation/tests/api contents exactly match approved upstream4c0fd5c, no additional scientific changes. UI not operated: automatic approval review rejected browser navigation using earlier Manual UAT instruction. M9 desktop interaction checks and M10 final checkpoint remain pending. No commit/tag/push. Production preview127.0.0.1:4185 (WSL PID508), Host127.0.0.1:4180 (PID478), reserved for this integration.

## P2D-M9 completed
Target sections37-43 checked. User explicitly authorized opening/operating this production UAT. Verified real Sod baseline, DENS/PRES switching, curve click and index Inspector, exact provenance, unsaved x_pos stale→Current with Dirty Config, invalid x_pos Core error with old graph retained. Real Sod+Helmholtz test-only input exercised UI Cancel during actual loading: Host cancelled request5b330357-7959-42b2-949b-063cea29ba86, retaineda6d650a0-7212-4779-8cfd-bfc3cdf4dc2e; no ARCH preview processes remained. Editing x_pos0.35→0.45 during real generation discarded old completion and retained prior graph; next update succeeded. No Save action or simulation.
UAT fixes: protocol adapter accepts Preview boolean, stale Phase2C caption removed, controls inherit readable dark styling, unrelated old Host diagnostics hidden, shared LineVis receives authoritative min/max (including constant/large values). PRESSURE1e24 confirmed on correct ordinate domain and Inspector; no physics changes.1280×720 and1920×1080 layouts inspected;800×720 no horizontal overflow and controls/Inspector available. No mobile scope added.

## P2D-M10 in progress
Target completion/checkpoint sections44-50 rechecked. Final full checks follow UAT fixes. No Phase2E or scientific modifications beyond exact approved API commit.

## P2D-M10 completed
Final94/94 tests and39/39 Host tests PASS;lint/typecheck/production build/diff checks PASS after UAT fixes. Completion report written. Local checkpoint tag studio-phase2d-v0.8.0; no push. API/Core exactly upstream4c0fd5c, root STATUS untouched, delivery excludes ignored artifacts. Phase2D scope complete; STOP before Phase2E.

## P2E-M0 — STOP A / STOP B
Fresh remote audit: review/studio-v0.4.2=4c0fd5c1; studio/phase2d-api-integration=43b381c3. Clean baseline/tag43b381c3 verified, independent Phase2E branch/worktree created. Complete active target copied to PHASE2E_TARGET.md. Core still exposes Sod1D only, no runtime parameter metadata/binding or CellularDet2D contract. See PHASE2E_CORE_CONTRACT_GAP.md. No code/build/Preview/simulation/baseline rerun. New target requires drag without automatic Preview. Waiting for Core A delivery; A/B remain separately gated. No success checkpoint/push.

## P2E-M0 Core A integration verification
Only47517d1c no-commit cherry-picked; no conflicts. B91a46f8f parent verified=A, not integrated. Independent CPU Debug build succeeded; four handoff scoped CTest groups PASS, simulation oracle disabled. Actual capabilities and default/explicit/fallback/outside/nonfinite CLI responses matched README identity and extension behavior. A Stop Gate cleared; B deferred by stage authorization. A1-A7 next, no automatic Preview after drag.

## P2E-A1 / A2 in progress
Target metadata authority rechecked. Added versioned optional Core extension validation and updated fixed CPU project tracked inputs. No B code. Preparing UI transport and capability negotiation.

## P2E-A1–A6 integration
Target metadata/binding/drag sections reviewed. Versioned Core extensions validated and capability-scoped; fixed CPU profile tracks A new headers/sources. Inspector separates Working/Saved/Core values and source. Missing parameter serialized by safe append preserving original bytes and Undo snapshot. Marker uses actual axis, Core bounds, transient drag candidates, mouseup one edit/Undo, no auto Save/Preview. State summary uses successful response identity. UAT in progress; no B.

## P2E-A7 completed — stop before B
Target A acceptance reviewed. Core4 scoped groups PASS; Studio97/97 and Host39/39 PASS, lint/typecheck/build/diff checks PASS. Production UAT verified metadata, textbox/marker, one-drag one-Undo, default insertion, error retention/recovery and desktop layouts. PHASE2E_A_COMPLETION_REPORT.md records evidence and limits. Local studio-phase2e-a-v0.9.0 checkpoint; no push, no B integration.


## Phase 2E-B integration — 2026-09-20

- Active scope: PHASE2E_TARGET.md B1–B7/MF, with the user's narrower no-Cellular-marker rule. A is sealed; no Phase 3.
- A branch/tag pushed and remotely verified at 018f69b82b10eb71c0f927faddd2a2b34f84c513.
- Independent worktree: /home/arch/projects/ARCH-phase2e-cellular-2d; branch studio/phase2e-b-cellular-2d.
- Core B 91a46f8f5498fa207c5210e3a12f366fa270df80, parent Core A 47517d1ce0ab33b761fcf3dd1a4241d68b7041fd, applied with --no-commit without conflicts. A was not reapplied; main was not merged.
- CPU Debug/CUDA OFF/KLU OFF build and five requested targets succeeded in independent build-preview-audit. No original build tree changed.
- B1: actual capabilities match handoff (Cartesian CellularDet, shock_dir 0/1, default 128x128, [Ny,Nx], x1-fastest, 8 MiB). Six scoped tests running; Studio implementation remains gated until completion.

B1 PASS: six scoped groups, 215.36 seconds; real capabilities verified. Target rechecked before B2. B gate cleared. B2 schema/profile integration in progress.

B2–B5 implementation: Host-owned profiles, model capability checks, strict shape/axis validation, Float64 provider, 2D heatmap/fields/colorbar/fit and index Inspector. Target boundaries rechecked: no Cellular marker, no AMR hierarchy. Verification in progress.

B2–B6 targeted checks PASS: 19 tests covering actual non-square Core fixtures in both shock directions, transpose/shape/axis rejection, limits, stdin identity, cancellation and failure retention, plus prior Sod security lifecycle. Production UAT underway. Target checked at each B milestone; no new scientific controls introduced.

B7 PASS: six Core groups; npm105/105,Host42/42,lint,typecheck/build PASS. Agent desktop UAT includes both shock directions,5x3 indices,half-bin adapter fix,zoom/pan/Fit,cancel,late sampling rejection,invalid direction retention and Sod metadata/textbox/Undo compatibility. MF closure reports generated; target rechecked. Local B checkpoint: studio-phase2e-b-v0.10.0; no B push or Phase3. Final default128x128 production preview Current; same binary Sod compatibility smoke passed.

## Phase 2F Core verification gate — PASS
Baseline fb22178fe578b17120597633f427e38d2a2be582 verified clean. Independent studio/phase2f-ui-contract-integration worktree; only Core UI 5e96d4f004c9bd320fb232853d59b90006cdb0f2 applied without conflicts using no-commit cherry-pick. Main and earlier API commits were not merged/reapplied. Target and Requirements fully read. CPU Debug build succeeded; eight required scoped CTest groups PASS (213.10 seconds), simulation oracle disabled. Actual --config-schema returns 90 parameters; Sod and CellularDet inspections preserve request ID and SHA-256 config revision, report expected 1D/code and 2D/CGS coordinates and no Setup/EOS/filesystem/CUDA execution. Invalid integer decimal/exponent/overflow and float NaN/overflow/suffix return field diagnostics. No Studio UI changes preceded gate. Proceed to 2F-A only; A checkpoint requires STOP before B.

### 2F-A1 / A6 foundation in progress
Fixed Host /api/configuration/schema and /api/configuration/inspect routes added with exact field allowlist, approved case IDs, stdin-only unsaved text, revision hash, bounded execution/output and pre/post build identity checks. Added configuration identity types and shared strict standard value validation foundation. Five focused tests PASS (type tokens, expressions, identity components, browser command injection and HTTP origin/protocol boundaries); typecheck and lint PASS. UI is not yet connected; full response validation, current-model metadata isolation, overwrite/Preview confirmations and desktop UAT remain required before A checkpoint. No A checkpoint yet; no B/C implementation.

### 2F-A1–A6 integration and desktop UAT
Target A1–A7 rechecked. Schema and inspection adapters now validate actual Core envelopes and scope results to project/case/revision/build/binary/request. Late request generation is discarded. Shared standard validation covers existing and inserted values; unknown model inputs remain untyped. Inspector separates schema default, parsed input and current Preview effective read. Persistent model/config/path identity, source mapping, model-scoped defaults/bindings and separate Preview/overwrite confirmations implemented.
Production UAT on 4188: Sod current Preview; x_pos metadata cleared on Cellular switch and restored for matching Sod; missing x_pos default entry and marker absent in Cellular; x_pos safe insertion, pending marker and one-drag/one-Undo verified without auto Save/Preview; existing nblockx1=1.5 and omitted ode_max_substeps=1e2 rejected; corrected 42 saved to ignored 1.par, verified disk bytes. Generic name remains association-unconfirmed. Exact Sod.par overwrite confirmation and separate Preview pairing dialog observed. Fixed Save As stale default selection found during UAT. Actual new-Core Cellular 5x3 Preview accepted, index11=(1,2), x1=7.68/x2=10.666667 and CGS units, no Cellular binding. Updated additive x3 unit validator to accept the approved Core unit string while preserving null compatibility. Final checks and checkpoint remain pending.

## Phase 2F-A completed — checkpoint / STOP
Target A1–A7 rechecked. Core8 scoped groups PASS; final Studio116/116 and Host45/45 PASS, lint/typecheck/build/diff checks PASS. Production UAT covers model/metadata/source isolation, generic and suspicious filenames, separate Preview versus overwrite confirmation with exact target disk verification, strict existing/inserted integers, missing x_pos insertion and one-drag/one-Undo. Actual new-Core Cellular5x3 regression and desktop1280/1920 plus narrow800 layout checks passed. PHASE2F_A_COMPLETION_REPORT.md records evidence, truthful build provenance and remaining B/C scope. Local checkpoint studio-phase2f-a-v0.11.0; no push. STOP before 2F-B; full Phase2F goal remains unfinished.


## Phase 2F-B — parameter editor (in progress)
- User authorized B only after A checkpoint `7b62bbe758bfee2d0dcebe4ba9fbbe2dbc01df59` / `studio-phase2f-a-v0.11.0`.
- Independent branch `studio/phase2f-parameter-editor`; A tag unchanged. Rechecked Target section 8 (B1–B12).
- Implemented schema-backed virtual defaults / aliases / six groups, Core coordinate layout, unit metadata and metadata-only Host path preflight. No Core edits or simulation.
- Regression and production browser UAT in progress; no B checkpoint yet. C remains unauthorized.

### B verification boundary
- Re-read Target B1–B12 and Requirements 2–11 / 25. No C implementation.
- Core scoped groups: 8/8 PASS, 210.66 s, simulation oracle disabled; reused unchanged A CPU binary.
- Production browser UAT: stable axes and invalid text, cylindrical/spherical naming, ODE insertion + real Sod Preview + actual save/reopen, alias-only save, gravity unavailable warning, Helmholtz diffusion field error, code_length/cm/rad, input/output path states, Host offline, desktop 1280×720 / 1920×1080 / narrow 800×720.
- Final regression after edge-case and display refinements remains to be recorded in completion report.

### B completion / STOP
- Target section 8 rechecked after final UAT; UI-01/02/03/04/05/12/13 B scope complete.
- Final Studio tests 121/121; Host tests 46/46; lint/typecheck/production build/diff-check PASS. Core 8/8 unchanged from the B verification run.
- All 90 keys individually searched through the production UI; omitted ode_rtol saved/reopened with only one added assignment; alias-only disk output confirmed.
- Report: `PHASE2F_B_COMPLETION_REPORT.md`. Checkpoint: `studio-phase2f-b-v0.12.0` on `studio/phase2f-parameter-editor`.
- A tag unchanged. No push; no 2F-C or Phase 3. STOP pending user authorization.

## Phase 2F-C / UI-06 boundary (2026-09-21)
From clean B 6b0b10b8 on studio/phase2f-plot-presentation. A/B tags unchanged. Re-read Phase2F Target. Fixed line figure sizing, reactive marker visible-domain subscription, explicit 1D Fit, and rejected grid hits outside sample edges. 9 focused tests and production build PASS. Production UAT: Sod discontinuity; Cellular 64x32, shock_dir 0 and 1; fixed sample 500 retains coordinates (21,3) and raw values through zoom/pan/Fit, no config edit or automatic Preview. Stock HeatmapVis does not consume spatial axis scaleType; UI-07 must use a shared physical projection for nonlinear spatial rendering and hit testing rather than relabel a linear texture.

## Phase 2F-C / final closure (2026-09-21)
Rechecked Phase2F Target/Requirements at C boundary. UI-06 fixes precede UI-07. Final real-only PhysicalPlot now shares forward/inverse physical projection across drawing, axes, hit testing, selection and Sod binding; existing Plotfile/Mock renderers remain unchanged. Independent axes/field scales, manual ranges, lower/upper clipping, Viridis/Hot, explicit Log failure/recovery and README current startup are complete. Core 8/8 (oracle disabled, no rebuild), Studio 128/128, Host 46/46, lint/typecheck/build PASS. Agent desktop UAT covers Sod and Cellular shock_dir 0/1, 64x32, four 1D scale combinations, 2D independent scales, drag/one Undo, raw Inspector and saved config lifecycle; 1920x1080,1280x720 and narrow800x720. See PHASE2F_COMPLETION_REPORT.md and PHASE2F_UI_REVIEW_CLOSURE_REPORT.md. Final local tag studio-phase2f-v0.13.0 on studio/phase2f-plot-presentation; resolve tag for hash. A/B tags and root STATUS unchanged. STOP before Phase 3; no push.
