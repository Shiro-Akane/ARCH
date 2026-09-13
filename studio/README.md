# Current Phase 1A checkpoint

Real local 1D ARCH Plotfiles are now supported alongside the preserved Phase 0 Mock workspace. Select **Real Plotfile → Open Plotfile**, open `tests/fixtures/sod-1d.h5`, select an actual field, then click the plot or enter a sample number to inspect x/value. Files are read locally and read-only; maximum file size is 16 MiB. Only simple uniform 1D data is supported. Build/Start/Monitor remain disabled.

Run `npm test`, `npm run lint`, `npm run typecheck`, and `npm run build` with the existing Node environment. See `PHASE1A_COMPLETION_REPORT.md` for verification, limitations and the checkpoint tag. The sections below retain the Phase 0 workflow and historical context; their exclusions describe that earlier checkpoint.

# ARCH Studio — Phase 0 / Complete

M0–M6 are complete; see PHASE0_COMPLETION_REPORT.md for acceptance evidence and limits. The authoritative scope is [TARGET.md](TARGET.md); current progress is [STATUS.md](STATUS.md).

## Start in WSL

```powershell
wsl -d ARCH-Ubuntu-24.04 -u arch --cd /home/arch/projects/ARCH-linux/studio
```

Then in that Linux terminal:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm ci
npm run dev
```

Open http://127.0.0.1:5173/ . Stop the server with Ctrl+C. If the port is occupied by the already-running demo, reuse it instead of starting another server.

## Frontend checks

```bash
npm run lint
npm run typecheck
npm run build
```

Build output is studio/dist. These commands do not build or run ARCH.

## Current interaction

Mock is the default workspace. Edit parameters to mark Config dirty (or invalid) and Preview stale. Preview generates dimensionless Mock fields; switch Density/Temperature/Pressure instantly. Click the current heatmap to read the containing cell center and all three values in Inspector. Dragging pans without selecting. Edits clear stale selections.

Save stores the working copy in demo memory. Revert restores the latest Save and cancels pending generation. Invalid configurations cannot be saved. Nothing is written to real .par files; reloading resets demo memory. Build/Start/Monitor remain disabled.

Run `npm test`, `npm run lint`, `npm run typecheck`, and `npm run build` for frontend checks. Phase 0 is complete; see STATUS.md.

## Cellular experiment / fixture (not the formal preview provider)

This is a preserved **experiment/fixture**, not a Phase 1 implementation or part of the formal MockPreviewProvider dependency graph.

- `CellularSample.tsx` imports only React and `src/samples/cellular.json`; it neither imports nor calls MockPreviewProvider, studioState, or useStudio.
- MockPreviewProvider and its transitive local dependencies do not import the Cellular component, fixture JSON, or offline script.
- `App.tsx` offers both as sibling views. They share the application bundle and UI entry point, but not provider data or state. This is dependency/data-flow isolation, not separate deployments or bundles.
- `scripts/import_cellular.py` is a manual offline experiment utility; it is not invoked by npm scripts or the frontend. Its optional Python environment is ignored by Git and is not needed to run the committed fixture.

The top sample selector can open the previously imported Cellular run. Its files are preserved separately from the Mock milestone work. Initial/final snapshots contain native 1D AMR cell-center density, temperature and pressure; connecting lines are not resampled data. This fixed sample importer does not accept arbitrary HDF5 or reconstruct multidimensional AMR.

Reproduce this sample from the preserved source files, in WSL at the repository root:

```bash
studio/.local/import-venv/bin/python studio/scripts/import_cellular.py
```

Offline import dependencies: h5py 3.16.0 (BSD-3-Clause), NumPy 2.5.3 (BSD-3-Clause; bundled third-party notices in its distribution). They live only under ignored studio/.local; no Python server is used.

## Phase 1B real config working copy

Select **Real Config → Open Config** and choose a local UTF-8 `.par` (up to 1 MiB). Core sections remain open and scroll inside the parameter panel. Custom keys remain text unless explicit metadata is available. `refine_threshold` has a source-backed [0,1] slider plus precise numeric input. Edit in memory, Revert to the loaded snapshot, or Save As a new `_modified.par` download. The source file is never overwritten; downloaded exports do not claim in-place saves. Mock preview remains illustrative and disconnected from ARCH initialization.

Desktop/workstation is the target platform, particularly 1280×720 and 1920×1080. Narrow windows receive basic overflow/accessibility checks only. See PHASE1B_COMPLETION_REPORT.md for the checkpoint and validation boundaries.
