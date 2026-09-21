# ARCH Studio — Phase 2F · desktop frontend checkpoint

Studio supports **Sod 1D Real IC**, authoritative Sod `x_pos` binding, and **CellularDet Cartesian 2D Real IC** (`shock_dir=0/1`, x3=0). Preview calls Core initialization only: no simulation timesteps, scientific output or AMR hierarchy. Mock and existing Plotfile viewing remain available.

## Current workflow

- Connect the local Host and open a project `.par`, or import a Working Copy. Persistent Model / Parameter File / source identity is shown. Filename pairing warnings are advisory; overwrite approval is separate.
- `config-schema` exposes 90 standard keys (89 controls because aliases share an edit destination); omitted defaults stay absent until edited. `inspect-config` parses the unsaved Working Copy before Setup. **Schema Default ≠ Inspection Parsed Value ≠ Preview Effective / Model-read Value**. Effective metadata requires matching project/model/build/revision.
- Explicit **Build** uses a Host-owned fixed profile and the existing build tree. Repository dirty is distinct from tracked-input freshness; `dependenciesComplete=false` remains disclosed. The browser cannot supply commands, arguments or environment.
- Explicit **Generate / Update Preview** sends unsaved text via stdin. CPU Preview timeout is **120 seconds** in Host (`previewRunner.ts`); UI completion polling deadline is 135 seconds, each HTTP request 15 seconds. Cancel terminates only the owned process group (SIGTERM, then SIGKILL after 1 second). Last success is retained on cancellation/error and marked previous when stale.
- Sod marker: drag shows a candidate; release commits one Working Copy edit and one Undo. It does not Save or Preview. A new explicit Preview resolves the pending marker. Cellular has no editable marker.
- Real plots: scroll zoom, drag pan, click sample, Fit. Data, axes, selection and Sod marker use the same physical projection. Coordinate Axes and Field Values have independent Linear/Log, Auto/Manual ranges and Reset. Lower/upper clipping and Viridis/Hot affect only display; Inspector remains raw. Log rejects nonpositive values/ranges explicitly. For a domain starting at zero, use an explicit positive Manual display range or return to Linear. Fit restores full coordinate domain and Linear spatial axes; intentional field clipping remains until Reset Field Values.
- **Save / Save As / Revert / Download Copy** are explicit. Preview and display controls never save. External disk changes require conflict resolution. Units, coordinate labels, applicability and path fields come from Core; Host checks schema-declared paths relative to its actual process working directory.

## Current local startup

Tested environment: **Node 24.21.0 / npm 11.19.0**. In a fresh checkout run `npm ci` in `studio/`. Current fixed Preview profile `arch-preview-cpu-integration` is bound to:

| Item | Value |
|---|---|
| Managed source root | `/home/arch/projects/ARCH-phase2f-ui-contract-integration` |
| Existing CPU Debug build | `build-preview-audit` |
| Build target / executable | `ARCH` / `build-preview-audit/bin/ARCH` |
| Models | Sod / CellularDet |
| Tracked inputs | 43 explicit inputs; incomplete full dependency graph |

From that checkout's `studio/`, in two terminals:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run local-host -- --project /home/arch/projects/ARCH-phase2f-ui-contract-integration --build-profile arch-preview-cpu-integration --config simulation/Sod/Sod.par --origin http://127.0.0.1:4188
```

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run build
npm run preview -- --port 4188
```

Open `http://127.0.0.1:4188/`; choose Real Config, Connect Local Host, Open Project Config, select the matching model, then explicitly Preview. Host is at `127.0.0.1:4180`. Both services bind loopback only; the exact frontend origin must match. Stop each terminal with Ctrl+C after active work completes. These commands use the existing fixed CPU tree; a different deployment requires an intentional Host profile, not browser reconfiguration. Original `/home/arch/projects/ARCH-linux/build-cuda` is separate and unchanged.

Cellular requires the Core reference 2D configuration and Helmholtz EOS table; see [Core API README](../src/api/README.md). Default sampling is 128×128, with Core capability limits; non-square Nx/Ny are supported. Protocol remains 1.3 and Core Preview schema 1.0 with independently versioned extensions.

Checks: `npm test`, `npm run test:host`, `npm run lint`, `npm run typecheck`, `npm run build`, `git diff --check`. See [Phase 2F completion](PHASE2F_COMPLETION_REPORT.md) and [UI review closure](PHASE2F_UI_REVIEW_CLOSURE_REPORT.md). Desktop/workstation is the target; narrow-window checks only prevent broken layout and inaccessible controls. Phase 3 is not included.

---

# Historical phase notes — superseded by the current instructions above

The following documents earlier checkpoints; old roots, timeouts, protocols and limitations are historical, not current startup guidance.

# Phase 2D — Real Sod initial-condition preview

Protocol **1.3**. Core Preview JSON schema is independently **1.0**. Real Config now uses the exact serialized unsaved Working Copy via stdin; Preview does not Save. Only registered Sod 1D Cartesian initialization is supported. There is no simulation, Plotfile generation, graphical parameter binding or AMR reconstruction.

The original `arch-existing-cuda-release` profile is unchanged and does not advertise real Preview. The independent integration profile `arch-preview-cpu-integration` is fixed to:

- source root: `/home/arch/projects/ARCH-phase2d-api-integration`
- existing build directory: `build-preview-audit`
- target: `ARCH`
- executable: `build-preview-audit/bin/ARCH`
- Preview profile: `sod-initial-cpu`, CPU, 512 samples, max4096

This is an explicit local integration deployment, not a portable auto-configurer. A different checkout needs an intentionally configured Host profile; the browser cannot rebind it. The CPU Debug build was prepared according to `src/api/README.md`, CUDA/KLU OFF. Host only performs standard builds of that existing tree. It never configures a tree through HTTP. Original `/home/arch/projects/ARCH-linux` and its CUDA tree are untouched.

Use Node24.21.0/npm11.19.0. In `studio/`, run `npm ci` for a fresh checkout, then:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run local-host -- --project /home/arch/projects/ARCH-phase2d-api-integration --build-profile arch-preview-cpu-integration --config simulation/Sod/Sod.par --origin http://127.0.0.1:4185
```

In another terminal run `npm run build` and `npm run preview -- --port 4185`. Both bind127.0.0.1. Connect Local Host; select Real Config; Open Project Config. If no matching successful Manifest exists, explicitly Build. Generate Real Preview, edit parameters without saving, then Update Preview. Fields come from Core. Click the curve or choose sample index to inspect real values. Parameter inspection and sample inspection share the contextual Inspector.

Last success remains visible during edits, errors and cancellation. Current requires the same Working Copy, project, request and build/binary identity. Cancel affects only the Host-owned process group (SIGTERM then SIGKILL after1s); timeout30s. Build and Preview cannot run concurrently. Host shutdown terminates active Preview. Unknown full dependency freshness is disclosed; matching25 tracked inputs and binary/manifest are required. Case mapping remains configured, not a general registry-verification claim. Unit/parameter metadata absence is explicit.

Host endpoints: POST `/api/preview` accepts only projectId/profileId/configText/configRevision/optional requestedSampleCount; GET `/api/preview/status`; POST `/api/preview/:requestId/cancel` with no body. Exact Origin/Host, Studio/protocol headers and bounded payloads apply. No shell/program/argv/cwd/env/PID from the browser. Core diagnostics are not automatic repairs. Existing config writes remain separate explicit actions. JSON request including overhead is limited to1MiB, so a near-limit raw config can be too large to submit.

See `PHASE2D_TARGET.md`, `PHASE2D_UPSTREAM_PREVIEW_API_AUDIT.md`, `STATUS.md`. Previous phase instructions below are historical; protocol1.3 and the above Preview behavior supersede them.

---

# Core initial-preview interface

ARCH now provides a CPU-only initial-preview command for local Host integration. See [the interface README](../src/api/README.md) for build steps, stdin/JSON calls, EOS/grid/AMR state, limits and response examples. This delivers the Core entry point; wiring it into Studio remains a separate frontend/Host change.

# Phase 2C — Controlled Build integration

Studio development and the managed ARCH project are separate. The fixed Host profile `arch-existing-cuda-release` builds `/home/arch/projects/ARCH-linux/build-cuda`, target `ARCH`, expected executable `/home/arch/projects/ARCH-linux/build-cuda/bin/ARCH`. It never treats the Studio checkout as the binary's source root. This is a unified ARCH executable with explicitly configured registered case ID `Sod`; mapping is **configured**, not independently verified by Core.

Use Node 24.21.0 / npm 11.19.0 in the Phase 2C `studio/` directory; run `npm ci` for a fresh checkout. Example production launch in two terminals:

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run local-host -- --project /home/arch/projects/ARCH-linux --build-profile arch-existing-cuda-release --config simulation/Sod/Sod_beginner.par --origin http://127.0.0.1:4179
```

```bash
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm run build
npm run preview -- --port 4179
```

Open `http://127.0.0.1:4179/`, expand Project, connect, inspect Build Profile, then explicitly choose Build. Both services bind only 127.0.0.1. Without `--build-profile`, Build stays Not configured. Source/binary selections cannot contradict the fixed profile. The profile source root must match the managed project and the existing CMake cache bindings. Config operations remain explicit and independent.

The Host invokes only `/usr/bin/cmake --build <fixed-directory> --target ARCH --parallel 4` with shell disabled and an allowlisted PATH/HOME/LANG environment. Existing CMake/Ninja internal regeneration is allowed; Studio never invokes standalone configure, edits CMakeCache, migrates trees or runs the binary. A trusted existing build tree may itself execute its configured build rules; this service is not a sandbox for untrusted CMake projects. No browser-provided program, argv, cwd, environment or binary path is accepted.

Protocol **1.2** adds GET `/api/build/profile`, `/api/build/status`, `/api/build/<buildId>/events`, `/api/source` and POST `/api/build` (exactly projectId/profileId). Existing config and project routes also require 1.2. Build output uses bounded one-second polling with project/build IDs and ordered sequences; retained logs are limited to 1024 events/256 Ki characters, 4096 characters per event. Clear view changes only the UI. Completed output is session-local, not a persistent compiler log. Reconnect is blocked during an active Build. Cancellation is deferred; the CLI refuses normal shutdown while its Build is active, so wait for completion before Ctrl+C.

Successful builds require exit 0 plus a confined regular expected executable and a valid final fingerprint. Manifests persist atomically under the **managed project's** ignored `studio/.local/build-*.json`, including source root/Git HEAD/repository dirty, profile hash, pre/post tracked inputs and binary fingerprints, output path and timestamps. Repository dirty is separate from tracked-input freshness. Failed attempts keep the last successful manifest. Executable hashing streams up to 512 MiB; other project identity files remain capped at 64 MiB, config reads/writes at 1 MiB.

Refresh Project State updates provenance without altering the Working Copy or Mock Preview. Changed explicit inputs produce needs-build; matching inputs with incomplete dependency coverage remain freshness-unknown. The fixed profile does not claim complete C++ dependencies and does not parse include graphs. Runtime `.par` edits and unrelated Studio changes do not imply a rebuild. The selected source viewer is read-only, limited to 256 KiB, with search only in the loaded text. Open in external editor remains deferred.

Run `npm test`, `npm run test:host`, `npm run lint`, `npm run typecheck`, `npm run build`, and `git diff --check`. See PHASE2C_TARGET.md, STATUS.md and PHASE2C_BUILD_INTEGRATION_REPORT.md. Build does not save config, run simulation or generate real Initial Preview. No Phase 2D features are included.

---
Historical Phase 2B and earlier usage follows; protocol 1.2 above supersedes the older version numbers.

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


## Phase 2E-B checkpoint

Current fixed CPU integration profile manages `/home/arch/projects/ARCH-phase2e-cellular-2d`, build tree `build-preview-audit`, target `ARCH`, executable `build-preview-audit/bin/ARCH`. Selected source is `simulation/Cellular/Cellular.cpp`; the unified binary has configured Sod and CellularDet cases. Full dependency freshness remains unknown. Host never configures a tree from browser input.

```sh
cd studio
npm ci
npm run build
npm run local-host -- --project /home/arch/projects/ARCH-phase2e-cellular-2d --build-profile arch-preview-cpu-integration --config simulation/Cellular/CellularPreview2D.par --origin http://127.0.0.1:4187
# separate terminal
npm run preview -- --port 4187
```

Connect Local Host, choose Real Config, open Project Config and select CellularDet2D. Run controlled Build if no matching manifest exists, then explicitly Generate Preview. Reference config uses Helmholtz/aprox19 and the repository EOS table; do not replace EOS with ideal. Default128x128, each axis2..256,8MiB bound. A high-resolution response may fail the byte limit; reduce sampling and retry. Display sampling is not actual AMR hierarchy. Units not supplied by Core remain unknown.

See PHASE2E_B_COMPLETION_REPORT.md and ORIGINAL_REQUIREMENT_CLOSURE_REPORT.md. No Cellular marker or Phase3 functionality is included.
