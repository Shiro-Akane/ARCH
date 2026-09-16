# Phase 2C Build Integration completion report

## Baseline
Base tag `studio-phase2b-v0.6.0`, verified commit `71d97b0ff8de2d1d4924a71f5e19fa2b2d8d984e`, clean before development. Independent branch `studio/phase2c-build-integration`; Studio checkout `/home/arch/projects/ARCH-phase2c-build-integration`. Fresh npm ci succeeded. Phase 2C target and the user's M0 resolution are preserved in PHASE2C_TARGET.md. Milestones M0 through M10 are recorded in STATUS.md.

## Real ARCH build contract audit
The managed source root is `/home/arch/projects/ARCH-linux`, not the Studio checkout. CMakeCache CMAKE_HOME_DIRECTORY points to that root; CMAKE_CACHEFILE_DIR and Ninja workdir point to its `build-cuda`. Ninja regeneration uses `/usr/bin/cmake --regenerate-during-build -S/home/arch/projects/ARCH-linux -B/home/arch/projects/ARCH-linux/build-cuda`. Dry-run listed regeneration because the CONFIGURE_DEPENDS phony force input triggers a glob verification edge with restat. All seven stored glob inventories matched, with no newer/missing regeneration inputs. This was not proof that real configuration would run.

Only this CUDA Release tree was found, consistent with the earlier baseline's single CPU/CUDA-capable executable. GCC/G++ 12, CUDA 12.8, OpenMP/KLU/cuDSS enabled; BUILD_TESTING ON. Target ARCH builds the unified executable and its dependencies, not all test targets. Sod is a registered runtime case, not a separate binary. Source/core/build files were equal to the Phase 2B checkpoint; original worktree has unrelated uncommitted studio/ changes, which were preserved.

User authorized strategy 1: use this existing original project/tree, including ordinary internal CMake regeneration during standard build. No standalone configure, cache editing, tree copying/migration or new CUDA tree was performed.

## Protocol 1.2
All endpoints require exact Origin/Host, X-ARCH-Studio: 1 and X-ARCH-Protocol: 1.2. Older protocols are rejected. Build capability requires an explicitly selected Host-owned profile that validates against the managed source root and cache bindings. Without one, Build is not-configured. Preview and watch capabilities remain false.

## Build Profile
- ID: `arch-existing-cuda-release`
- Managed source root: `/home/arch/projects/ARCH-linux`
- Build directory: `/home/arch/projects/ARCH-linux/build-cuda`
- Target: `ARCH`
- Expected executable: `/home/arch/projects/ARCH-linux/build-cuda/bin/ARCH`
- Registered case ID: explicitly configured `Sod`
- Selected case source: `simulation/Sod/Sod.cpp`
- Fixed parallelism: 4; existing configured heavy pool remains unchanged at 8, so the total job limit governs this invocation.
- Mapping: configured, never verified; there is no independent Core registry verification interface.
- Explicit tracked inputs: selected Sod source, src/main.cpp, src/core/ProblemRegistry.h, CMakeLists.txt, CMakePresets.json, cmake/Application.cmake, BuildOptions.cmake, CudaBackend.cmake, Dependencies.cmake, CustomNetworks.cmake and build-cuda/CMakeCache.txt.
- Dependency coverage: incomplete; no include graph is inferred.

## Process execution model
The Host spawns `/usr/bin/cmake` with fixed `--build`, `--target ARCH`, `--parallel 4` argv and shell:false. cwd is the validated managed source root. Environment is limited to fixed PATH, HOME and LANG; browser-supplied env/command/args/cwd/program are rejected. One active build is reserved before asynchronous validation and retained through final provenance checks. No simulation/executable launch exists. Cancellation is deferred; the CLI refuses ordinary shutdown during an active Build.

## Security boundary
Fixed profile selection only; strict two-field request (projectId/profileId), loopback-only server, protocol/origin headers and request limits. No generic exec/shell/editor API. Paths remain confined and symlinks rejected. The selected source endpoint returns only the session's source, bounded to 256 KiB; it accepts no path argument. Executables use bounded streaming fingerprints up to 512 MiB, necessary for the real 217,504,416-byte binary; non-executable project files retain the 64 MiB cap and configs the existing 1 MiB cap. Manifest state is under the managed project's ignored studio/.local with checked parents and atomic publication.

A trusted CMake tree can execute its own configured build rules. This is controlled invocation of an authorized local project, not a sandbox for untrusted build scripts. The one-build rule is per Host session, not a cross-process filesystem lock; avoid simultaneous external builds in the same tree.

## Build event transport
Bounded polling at one-second intervals; each event carries projectId, buildId, sequence and timestamp. stdout/stderr remain distinct, terminal controls are stripped and rendering is plain React text. Limit: 4096 characters/event, 1024 events, 256 Ki characters retained. Truncation is explicit. Stale IDs/order and oversized responses are rejected. Only the latest attempt's output is retained in memory; no persistent full compiler log is introduced.

## Build UI
Existing Project panel shows managed root, build directory, target, expected executable, configured case relation, state/result, binary freshness and last successful provenance. Output is collapsible; Clear view only changes the view. Reconnect is disabled while active. Build never calls editor Save/Revert or Preview generation. Selected source view supports read-only in-text search without external editor launch or arbitrary file browsing.

## Build Manifest
A successful command is insufficient by itself: expected regular confined executable must be fingerprinted and manifest saved successfully. Manifest includes managed source root, source Git HEAD, repository dirty, profile fingerprint, selected source and explicit inputs, pre/post input/binary fingerprints, build directory/target/output absolute and relative paths, SHA-256/size/mtime, build ID and start/end times. A later failed attempt does not overwrite the last successful manifest; initialization restores it. Latest attempt/output itself is session-local.

## Binary provenance and freshness semantics
- Authoritative observations: actual process exit, checked source/build path binding, exact file fingerprints and recorded Git metadata.
- Configured relationship: unified ARCH binary plus explicit Sod registry ID. No scientific or independent registry verification is claimed.
- Tracked freshness: profile/input/binary comparison against last successful manifest. Changed tracked inputs produce needs-build; input changes during build also prevent a current-input claim.
- Unknown: full dependency coverage. Matching tracked inputs stays freshness-unknown in the real profile; profile or binary mismatch also becomes unknown.
- Repository dirty is a separate historical fact. Unrelated studio/ dirt and ordinary runtime .par edits do not trigger needs-build. Refresh never replaces the Working Copy or modifies Mock Preview state.

## Successful-build UAT
Production frontend at 127.0.0.1:4179, Host at 127.0.0.1:4180. A real Build was initiated via the Studio Build button with the fixed profile above. Actual stdout:

```text
[0/2] Re-checking globbed directories...
ninja: no work to do.
```

Exit 0, final binary checked, Manifest persisted. This was an actual standard build invocation with no compile work required, not evidence of a new cold compilation. No independent configure or actual regeneration occurred in this UAT.

Before Build, explicit binary and all 11 tracked-input fingerprints were recorded. Post-build values match exactly, including CMakeCache and binary mtime. Managed Git status is byte-for-byte unchanged from before UAT. The source viewer located line 72 registering Sod. An unsaved nblockx1 edit 4 -> 8 remained Dirty/8; the previously generated Mock image remained present and stale.

Build ID: `bdc02004-3f4e-4142-97e9-b7cfb459a4d9`.

Start: `2026-09-16T08:53:39.113Z`; finish: `2026-09-16T08:53:39.760Z`.

Managed Git HEAD: `5dd195138afca5372d79ab9c9aab7689854d6b8a`; repositoryDirty: `True`.

Binary SHA-256: `8166e3da61be1f1a245300e2e55929acd695fb3a6ec2e752af39ed3cccf8a968`; size: `217504416`; mtime: `2026-09-12T15:13:16.417Z`.

Manifest: `/home/arch/projects/ARCH-linux/studio/.local/build-1c5b864bb8ed4c5d1de11546.json`.

## Failed-build and source-change UAT
A separate disposable fixture at /tmp/arch-build-RMYneq used the real Host runner/state/API/UI but an explicit module-only fake child-process seam, labeled "Isolated negative UAT fixture (not ARCH compiler)". It is not an ARCH build tree and did not invoke ARCH or CMake configure. A copied Sod source was changed only in that fixture. Refresh showed needs-build and case.cpp as the changed input. A subsequent nonzero process result displayed failed and retained stdout/stderr, last successful fixture manifest, Dirty/8 Working Copy and the old Mock Preview. Two immediate requests returned 202 then 409 build-busy. These are controlled negative-path integration checks, not claims of a real compiler-error build of ARCH.

## Regression
Phase 2B config lifecycle and exact round-trip tests pass; parser/serializer and scientific Core were not changed. Existing Mock, history/numeric controls, inspector, real 1D HDF5 discovery/LineVis/invalid recovery tests pass. Browser UAT additionally checked editor/Preview retention through Build success, project reconnection and Build failure. No new human acceptance is claimed. No CPU/CUDA scientific baseline tests, simulation or Plotfile generation were run.

## Automated checks
Final checks on Node 24.21.0 / npm 11.19.0, WSL Linux:

| Check | Result |
| --- | --- |
| npm test | PASS: 78/78 |
| npm run test:host | PASS: 26/26 (subset) |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS |
| git diff --check | PASS |

Tests cover profile/cache/path validation, protocol mismatch, injected fields, fixed argv/environment, spawn failure, nonzero exit, absent binary after exit 0, concurrency, sequenced bounded output, stale events, manifest retention, source/profile changes, during-build input changes, selected-source safety and all previous regressions. Existing executable-size test was extended to validate the new 512 MiB bound while preserving the original 64 MiB non-executable bound. No tests disabled. Negative HDF5 diagnostics and the existing Vite large-chunk advisory are expected, not failures.

## Deferred
Real IC Preview, Setup()+Init(), simulation/Run, Build & Preview chain, AMR, graphical bindings, SSH, external editor launch, cancellation, watcher, full dependency authority and profile editing from the browser remain out of scope. No Phase 2D work or push.

## Remaining issues
Profile is intentionally installation-specific and CUDA-enabled; there is no new CPU-only tree. Full dependency freshness remains unknown. Successful no-op build provenance records the checked existing artifact; it does not prove the original binary was freshly compiled by this invocation. Cancellation and complete persistent build logs are not implemented. Git metadata can be unknown if the optional bounded Git query fails. Local runtime manifests are not cryptographic attestations. Finite UAT did not exercise an actual ARCH compiler failure or a cold CUDA rebuild. No further development or optimization was undertaken for these deferred limits.

## Checkpoint
Commit message: `feat(studio): integrate controlled local ARCH builds`.
Local tag: `studio-phase2c-v0.7.0`. Resolve exact hash with `git rev-parse studio-phase2c-v0.7.0^{commit}`. Only studio/ delivery files are committed; node_modules, dist, .local, temporary UAT fixtures/logs and screenshots are excluded. QA servers are stopped at completion. Stop at Phase 2C.
