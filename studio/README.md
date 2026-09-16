# Phase 2B — Local Config Lifecycle

Phase 1C2 Manual UAT was confirmed passed by the user. Phase 2B extends the sealed Local Host foundation with explicit config load, safe Save, Save As, Revert and external-change conflict handling. See PHASE2B_TARGET.md and PHASE2B_CONFIG_LIFECYCLE_REPORT.md. Build and real IC preview remain unavailable.

Use the documented WSL Node environment (Node 24.21.0 / npm 11.19.0 tested). In a fresh checkout run `npm ci` in `studio/`.

Terminal 1, from `studio/`:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run local-host -- --project /home/arch/projects/ARCH-phase2b-config-lifecycle --case simulation/Sod/Sod.cpp --config simulation/Sod/Sod_beginner.par --binary build/not-configured/ARCH
```

The binary path above intentionally demonstrates `missing`; replace it with an explicitly chosen project-relative executable path, or omit `--binary` for unknown. No executable is launched or claimed compatible. Initial source/config/binary selection is configured at service startup; successful Save As updates the current config in the running session. Selection is not inferred from filenames. The root must exist. All selected paths must be plain relative paths; symlink components are refused.

Terminal 2, from `studio/`:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run dev
```

Open `http://127.0.0.1:5173/`, expand **Project / Local Host**, then **Connect Local Host**. For a production frontend instead, run `npm run build` then `npm run preview -- --port 4177`, and launch the host with `--origin http://127.0.0.1:4177`. Both servers bind only 127.0.0.1. Stop each using Ctrl+C. The UI connects to host port 4180. Exact origin matching is intentional: localhost and 127.0.0.1 are different origins. The host CLI supports an alternate port for programmatic clients; the current UI uses 4180.

**Refresh Project State** inspects only the configured files, bounded to 64 MiB each, and compares fingerprints with the session baseline (updated for config after a successful Save/Save As). No recursive scan/watcher. Errors retain previous known identity and mark it unknown; UI request failure retains the last session with a warning. Restarting the host creates a new snapshot/session. Reconnect does not reset a running host's baseline.

Project identity never auto-opens or replaces the editor. Select **Real Config → Open Project Config** to load the selected `.par` explicitly. Save requires a compatible connected host with write capability and matching config association. The existing serializer supplies exact UTF-8 text; comments, unknown keys, BOM and line endings are preserved.

**Save** compares the last saved fingerprint, writes and syncs a same-directory temporary file, checks the fingerprint again, then atomically replaces the original. Write/rename failures preserve the original. **Save Working Copy As…** takes a plain project-relative `.par` destination in an existing directory and refuses overwrite; success makes the new file current. **Revert** restores the latest loaded/saved in-memory snapshot. **Reload disk version** reads disk explicitly, with an unsaved-change guard. Refresh reports external changes without replacing edits. Conflicts keep the Working Copy and offer Reload, Save As or Cancel. No force overwrite or merge is implemented. **Download Copy…** remains the browser export fallback.

Protocol **1.1** endpoints: GET /api/health, /api/host, /api/project, /api/project/files, /api/config; POST /api/project/refresh, /api/config/save, /api/config/save-as. All requests require exact authorized Origin/Host, `X-ARCH-Studio: 1` and `X-ARCH-Protocol: 1.1`. Only the two config writes accept bodies: strict JSON fields, at most 1 MiB including JSON overhead. Config reads/text are also limited to 1 MiB; JSON escaping can make a near-limit config too large to save. No generic filesystem or command API exists. Build/Preview/binding contracts remain declarations only. Browser-origin controls are not authentication against programs already running as the same OS user.

Writes are supported only by the WSL/Linux service, using directory descriptors and same-directory atomic publication. Native Windows host writes remain disabled. Save As uses atomic no-clobber linking; no directory creation is performed. This is optimistic concurrency, not an OS transaction with unrelated external editors: an external writer can race the final check/rename interval. Avoid simultaneous external editing during Save. Post-publication changes are detected by final readback where observable. Saves do not generate or clear stale Preview state.

Run `npm run test:host` for host-specific checks and `npm test`, `npm run lint`, `npm run typecheck`, `npm run build` for all gates. The supported/verified service environment is WSL Linux; native Windows filesystem race behavior has not been qualified. See PHASE2B_CONFIG_LIFECYCLE_REPORT.md.

---
Historical frontend documentation follows.

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

Select **Real Config → Open Config** and choose a local UTF-8 `.par` (up to 1 MiB). Use the persistent Grid / EOS / Network / Runtime navigator to select one Core editor. Advanced and Custom parameters are collapsed by default and searchable. Known axis fields follow the explicit dimensional topology; absent keys are never inserted. Source information remains in tooltips and the read-only loaded Raw .par view. Custom keys remain text unless explicit metadata is available. `refine_threshold` has a source-backed [0,1] slider plus precise numeric input. Edit in memory, Revert to the loaded snapshot, or Save As a new `_modified.par` download. The source file is never overwritten; downloaded exports do not claim in-place saves. Mock preview remains illustrative and disconnected from ARCH initialization.

Desktop/workstation is the target platform, particularly 1280×720 and 1920×1080. Narrow windows receive basic overflow/accessibility checks only. See PHASE1B_COMPLETION_REPORT.md for the checkpoint and validation boundaries.
