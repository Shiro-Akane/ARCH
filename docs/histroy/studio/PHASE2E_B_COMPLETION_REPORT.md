# Phase 2E-B Completion Report

## Baseline and integration

A baseline: `018f69b82b10eb71c0f927faddd2a2b34f84c513` / `studio-phase2e-a-v0.9.0`.
A branch `studio/phase2e-metadata-binding-2d` and annotated tag were pushed atomically to Shiro-Akane/ARCH; remote branch and peeled tag both verified at the above commit.

B branch: `studio/phase2e-b-cellular-2d`.
Independent worktree: `/home/arch/projects/ARCH-phase2e-cellular-2d`.
Only Core B `91a46f8f5498fa207c5210e3a12f366fa270df80` was applied with `cherry-pick --no-commit`; no conflicts. Parent is Core A `47517d1ce0ab33b761fcf3dd1a4241d68b7041fd`. No repeated A cherry-pick, main merge, reset or stash.
Core patch-id `48de39918ef69bf57c72fedd6d137a5ead72eb62` matches upstream exactly. All additional implementation changes are in studio/. Root STATUS.md is untouched.

## B1: authoritative contract verification

Read Core B handoff, README, Preview/Sampling/Response/CLI implementation, shared Grid/ProblemHelper changes and scoped tests. Rebuilt CPU Debug with CUDA/KLU OFF and BUILD_TESTING ON in independent build-preview-audit; reused existing HighFive headers read-only. Original managed CUDA tree and A checkout unchanged.

Six `ctest -R '^preview_'` groups PASS in 215.36 seconds: initial conversion, original API contract, parameter reads, parameter metadata, sampling limits, Cellular 2D. Simulation oracle explicitly disabled. Cellular scoped tests compare 5x3 samples to direct authoritative Init/EOS in both directions; verify default/max limits, EOS errors, actual response size overflow, unsaved stdin identity, process termination and no output files.

Actual capabilities match Core documentation: CellularDet, Cartesian x1/x2, shock_dir 0/1, x3=0; default 128x128, axes 2..256, total <=65536, <=8 MiB response; DENS/PRES/TEMP/VELX/ENER/EINT/VELY, units null. Core schema remains 1.0. B gate cleared before Studio implementation.

## B2–B6 implementation

- Host-owned Sod and Cellular profiles; browser supplies profileId/config/revision and bounded sampling, never program/argv/cwd/env. Model-specific capability validation before spawning approved CLI arguments.
- Unified ARCH binary, configured registered cases Sod/CellularDet. B project selects Cellular.cpp. Build input fingerprints now include Cellular.cpp, Sampling.h, Response.h, Grid.h and ProblemHelper.h alongside existing inputs. Full dependency authority remains unknown, not falsely verified.
- Strict grid response validation: dimension/kind, [Ny,Nx], x1-fastest, sample product, ordered axes, x3=0, finite arrays/extrema, request identity, CPU/no-timestep/no-output declarations and capability field/byte limits.
- Real Float64 grid provider preserves Core arrays. Heatmap uses x1 right/x2 up; h5web expects pixel edges, so the adapter converts authoritative uniform bin centers to N+1 display edges. Inspector still reads original centers/doubles at j*Nx+i. Regression covers half-bin alignment.
- Fields, colorbar/min/max, zoom/pan/Fit and sample Inspector. Constant fields explicitly disclose display-only scale padding. No Cellular marker or inferred physical units.
- Same request lifecycle as Sod: stdin Working Copy, explicit Update, cancellation/process termination, sampling/model/config revision rejection, previous successful image retention, provenance and synchronized state summary. Failed request state is separate from old success state. No auto Save/Preview.

## B7 regression

- npm test: **105/105 PASS**.
- npm run test:host: **42/42 PASS** (subset of npm test, not additive).
- npm run lint / typecheck / build: **PASS**.
- git diff --check and cached diff --check: **PASS**.
- Existing production bundle size warning remains; no packaging refactor introduced.

Tests retain config round-trip/insertion, disk lifecycle, Build/security, Mock/Plotfile, Undo and prior preview behavior. Added actual Core B fixture validation in both directions, shape/axis/order/fixed-coordinate corruption, capability bounds, structured errors, 2D stdin provenance, cancellation and failure retention.

## Desktop UAT observations

Production UI: http://127.0.0.1:4187/; Host loopback :4180.

- Opened actual CellularPreview2D.par via Host; controlled Build produced manifest `08a46d80-162a-456b-a525-12eecce6324a`.
- 5x3 real response displayed Current. Example request `e9a06317-7e98-408e-9930-4d423ad44324`, original config revision `c33965b71bc301ca7ae155f123039857b9db59493feebc2577a5512a4dac940f`.
- shock_dir=0 showed vertical separation; unsaved shock_dir=1 immediately marked old preview stale, explicit Update returned Current with horizontal separation. At i=1,j=0/index1: x1=7.68, x2=2.1333333, VELY=1.011e9. No synthetic field evaluation in Studio.
- Selected index11 and clicked heatmap: i=1,j=2, x1=7.68,x2=10.666667, matching Core. Switched Density/VELY; field selector contains all seven Core fields. Zoom, pan and Fit exercised; click selection distinguished from drag.
- shock_dir=2 retained input and old image, showing the actual Core unsupported-direction message and separate failure state.
- Cancelled a real 256x256 request; old preview retained. Started another 256x256 request then changed requested dimensions to5x3; successful late response was discarded by UI.
- Same B binary generated Sod512 Current with metadata and x_pos marker. Textbox0.35 moved pending marker and left curve stale; one Undo restored0.5 and Current.
- 1920x1080,1280x720 and800x720 layouts inspected; measured document width equaled viewport at1280/800. No mobile feature work.
- Final production default128x128 response reached Current with Helmholtz ready,19 species and AMR configuration0–2.
- Config files remained unchanged; no Preview-created Plotfile/checkpoint/scientific files. CMake creates an empty output/ directory during configuration; it remained empty throughout Preview UAT.

Binary SHA256: `d6c20a15791336a724ea223cc0086f7af633c3cfe117d5a3d9fd6aa6dc74850f`.
Build manifest truthfully records the pre-checkpoint dirty integration state and source HEAD at build time; committing docs/Studio changes does not rewrite that history or imply full dependency freshness.

## Boundaries and checkpoint

No independent scientific Core edits, simulation, CUDA baseline rerun, Cellular editable marker, actual AMR hierarchy, SSH or Phase3. User Manual UAT remains a separate acceptance authority; observations above are agent desktop UAT, not claimed user sign-off.

Local checkpoint: `studio-phase2e-b-v0.10.0`. Resolve exact commit with `git rev-parse studio-phase2e-b-v0.10.0^{commit}`. B is not pushed automatically. A remains unchanged and remotely accessible. See PHASE2E_COMPLETION_REPORT.md and ORIGINAL_REQUIREMENT_CLOSURE_REPORT.md for full closure and deferred items.
