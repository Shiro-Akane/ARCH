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
