# Phase 2 Local Host Foundation report

## Baseline
- User confirmed Phase 1C2 independent Manual UAT passed. Immutable accepted baseline: studio-phase1c2-v0.4.2 / defc0ff7ccbf5304b63e90cd4792575f49a74006.
- Baseline review worktree clean. Dedicated worktree /home/arch/projects/ARCH-phase2-local-host; branch studio/phase2-local-host. Original dirty UAT worktree untouched.
- User renamed original Phase 3A scope to Phase 2; milestones P2-M0 through M9 followed in order. PHASE2_TARGET.md preserves the original target with authoritative naming clarification.

## Architecture
React ProjectPanel -> typed HttpLocalHostAdapter -> narrow Node HTTP service -> explicitly selected files under one launch-authorized root. No new framework/dependency. Existing editor and viewers remain independent; no automatic file-content load or editor mutation.

## Protocol version
1.0, visible in UI and checked before accepting responses. Adapter validates capabilities, session, paths and fingerprints. Timeout 10 seconds; response bounded to 64 KiB. Browser-native fetch receiver regression covered after integration testing found it.

## LocalHost capabilities
readProject=true; writeConfig/build/preview/watchFiles=false. Future Build/Preview/ParameterMetadata/ParameterBinding types declared only. Mapping unknown, metadata unavailable; no C++ parsing or inferred binary freshness.

## Security boundary
- Bound only to 127.0.0.1. Exact authorized UI Origin, exact loopback Host and X-ARCH-Studio header required; no wildcard CORS.
- Fixed endpoints, no query/body input, unknown endpoint 404; command/path fields rejected. No process spawning or project-file writes.
- Explicit canonical root; traversal, encoded/absolute paths and symlink components rejected. Regular files only; no-follow/nonblocking descriptor open. Linux descriptor target checked before reads; inode/stat consistency checked after reads.
- Only three configured identities inspected, 64 MiB maximum each, streamed in 64 KiB chunks. No output-tree scan.
- Supported verification platform: WSL Linux. No claim of protection against a privileged local attacker; Origin checks protect the browser boundary, not same-user native programs.

## Project session example
Browser connected to /home/arch/projects/ARCH-phase2-local-host through protocol 1.0. simulation/Sod/Sod.cpp and simulation/Sod/Sod_beginner.par show available; build/not-configured/ARCH intentionally shows missing. Project root/session/timestamps and per-file hashes are expandable. No compatibility or Build readiness inferred.

## Files / fingerprints
SHA-256 + byte size + modified time; separate source/config/binary states. Refresh compares with the service-open snapshot. Source changed, config changed-externally, binary available with fingerprint-changed flag (never stale). Missing and read errors are distinct. Failed object inspection preserves previous known fingerprint with error; failed project refresh preserves prior session. No working-copy reload.

## Errors tested
Traversal, absolute/encoded paths, symlink escape, permission denied, missing root/selected files, oversized file, file changing during hash, offline service, protocol mismatch, malformed/oversized response, unknown endpoint, arbitrary command body, disallowed Origin, failed refresh and recovery. Tests create temporary non-scientific files outside the repo and remove them afterwards.

## Regression
Original 49 tests retained, including real Sod HDF5/field/sample and invalid recovery, .par exact round-trip/export/ranges/invalid/revert, Mock state/point selection/fields/retention, history/scrub/controls/Inspector. No parser, scientific fixture, Preview renderer or Plotfile component changes.
Browser integration: Edge and Codex in-app browser connect successfully; selected paths/states and protocol displayed. Mock generate works; Grid edit to 256 remains after host refresh with old heatmap stale. Switch Real Config retains prior-preview provenance. Actual sod.par opens; nblockx1 edit 4->8 remains Dirty and 8 after project refresh; Revert restores 4. This is developer regression evidence, not a new human UAT claim.

## Automated checks
npm test: 59/59 PASS (original 49 + 9 host tests + 1 Project UI render test). npm run test:host: 9/9 PASS. npm run lint, npm run typecheck and npm run build: PASS. git diff --check: PASS. Existing HDF5 negative-test diagnostics and Vite chunk-size advisory are expected; no test weakening or bundling refactor.

## Deferred
Local-host Save/Save As, conflict resolution, Build/manifest/freshness, ARCH execution, Setup()+Init() preview, AMR/2D graphical binding, SSH and remote execution. Manual refresh used instead of a watcher. Existing browser Save As semantics preserved.

## Remaining issues
No known blocking issue in the implemented scope. Native Windows host filesystem behavior is not qualified; use the documented WSL Node environment. Files over 64 MiB report unknown with a size-limit explanation. Local-host identity is separate from the manually opened editor file; no automatic association or reload is claimed.

## Checkpoint
Local checkpoint tag studio-phase2-v0.5.0 identifies the commit feat(studio): add local host project session foundation. Resolve the immutable commit with git rev-parse studio-phase2-v0.5.0^{commit}. No automatic push or next-stage work.
