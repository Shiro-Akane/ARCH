# Phase 2G-C Desktop Architecture Audit

Date: 2026-09-22. Scope: PHASE2G_TARGET.md C1; audit before implementation.

## Decision
**PASS for implementation — Electron selected; desktop completion remains unproven until C8.**

Correction to the first audit: cua_repl exposes browser-only surfaces, but the installed computer-use skill supplies a separate node_repl + @oai/sky native Windows channel. Its documented initialization succeeded, list_windows returned real native windows, and get_window_state read the project's Explorer window after restoring it. Therefore the earlier claim that no native UI channel exists was too broad. No security setting or permission was changed.

The native-UAT blocker is cleared by actual tool evidence. The implementation obligations below remain: packaged assets, explicit Windows/WSL association, owned ports and graceful cleanup, including active Build. Electron is selected because React/Node reuse is strongest; Tauri adds a new Rust/sidecar toolchain without removing these obligations.

## Baseline evidence
- Initial working tree clean; initial branch studio/phase2g-initial-amr.
- HEAD and B peeled tag: 2cd4dbbabc6a17123950a4b2ab6d612a6c0831b2.
- A peeled tag: 13345ba4d1b7379ff5481bf9ca4f2fc9156aee7c.
- Audit branch: studio/phase2g-desktop-launcher, created from B.
- Repository: /home/arch/projects/ARCH-phase2g-continuous-local-workflow.
- WSL inventory confirms ARCH-Ubuntu-24.04 running as WSL2. Existing CPU executable is present.
- Windows Node v24.19.0 is available through the Codex runtime; Linux project Node v24.21.0 is present. Do not assume end users have either agent-specific path.
- Rust/cargo not found on PATH or the checked user cargo path. Visual Studio2022 Community exists, but C++ workload completeness was not established. No Tauri toolchain was installed.
- Existing production dist is approximately6.1MiB. This is frontend size only, not a desktop distribution estimate.
- Tracked studio files contain no Electron/Tauri/native launcher entry. Existing optional browser/Host scripts are reusable components, not an existing desktop solution.
- A/B tests are already recorded in their completion reports; they were not rerun for this documentation-only audit.

## Architecture comparison
| Concern | Electron | Tauri2 | Existing repository |
|---|---|---|---|
| React production integration | BrowserWindow can load bundled assets. Existing React stays substantially unchanged. | Bundles frontend assets into system WebView. React reuse feasible. | Vite dist exists; no desktop asset loader. |
| Node Host reuse | Main-process Node eases orchestration, but Windows Electron Node cannot execute Linux Core. Existing Host still runs under WSL Linux Node. | Existing Node Host still requires WSL runtime; Rust commands/sidecar introduce another lifecycle layer. | Host already owns config, Build, Preview/session and AMR validation. |
| Independent window | Native app/window and taskbar identity supported. | Native app/window supported. | Browser tab only; fails C3 as the default. |
| Native dialogs | Main-process dialog API for directory/open/save selection; narrow preload bridge. | Dialog plugin/capabilities plus Rust boundary. | Current import/download and Host Save As UI are not a native desktop project-picker implementation. |
| Windows–WSL bridge | Main spawns fixed wsl.exe argv, tracks child ownership and ready handshake. | Same OS boundary through Rust process/sidecar handling. | Manual WSL Host start; no launcher ownership. |
| Packaging cost | Adds Electron Chromium/Node runtime; larger distribution, but JS/TS orchestration matches repository. | Smaller shell potential; WebView2, Rust/MSVC build pipeline and WSL Node still required. | No installer/runtime package. |
| Development/packaged parity | Same main/preload and production asset path can be used in both; dev server remains optional. | Same bundled frontend possible, separate Rust development pipeline. | Current npm preview depends on a user-started Vite process. |
| Conclusion | Preferred minimum adaptation, contingent on desktop UAT access. | Feasible alternative, higher new-toolchain cost for this repository; does not remove WSL lifecycle problems. | Reuse Host/React, not the manual startup workflow as the claimed launcher. |

At the initial audit, framework size/startup/installer performance was not measured. Subsequent packaged UAT and the approximately375MiB portable artifact are recorded in PHASE2G_DESKTOP_LAUNCH_REPORT.md; no installer benchmark is claimed.

## Actual code findings
1. studio/host/cli.ts:
   - --project required; manual default port4180/origin5173.
   - Port0 is rejected, so launcher-owned atomic ephemeral binding requires an entrypoint adaptation.
   - --case currently means source-relative path, not registry case ID; public --case Sod must not be passed through unchanged.
   - SIGINT/SIGTERM refuses shutdown during Build. Existing code cannot simply be killed on window-close and called clean lifecycle.
   - No parent-stdin EOF ownership watchdog, machine-readable startup nonce, or desktop parent relationship.

2. studio/host/server.ts:
   - listenLocal binds127.0.0.1.
   - Exact Host and HTTP loopback Origin, Studio/protocol headers and route/request validation already exist and should remain.
   - file:// or an app:// origin would fail the current origin contract. Do not permit arbitrary/null Origin to work around this.

3. Endpoint configuration:
   - ConfigAdapter.ts, BuildAdapter.ts, LocalHostAdapter.ts, RealInitPreviewProvider.ts and ProjectPanel.tsx reference127.0.0.1:4180.
   - A single trusted desktop bootstrap endpoint must replace these hardcoded transport choices, while debug mode retains its explicit loopback default. A free-form browser-supplied Host/program/env is not acceptable.

4. studio/host/project.ts and files.ts:
   - Canonical Linux root, project-relative paths, traversal/symlink rejection and atomic config writes can be reused.
   - Passing an absolute --binary directly currently fails selectedPath().
   - Fixed-profile source/binary association must remain authoritative. --source is evidence/selection, not arbitrary source-to-build authorization.
   - Native Save As returns a Windows path; conversion and managed-root containment must precede existing Host write calls. A path outside the project is not silently accepted as a project save.

5. studio/host/buildProfile.ts:
   - CPU profile is bound to this exact managed source root, existing build-preview-audit and75 explicit inputs.
   - CUDA profile remains separately bound to ARCH-linux/build-cuda.
   - Project discovery must not rewrite these roots, modify CMakeCache, copy build trees or infer a trustworthy Build profile from an arbitrary binary.
   - Unknown project/binary association must show missing approved profile/provenance. Supporting project selection does not guarantee Build/Preview for every arbitrary directory.

6. studio/host/previewRunner.ts / previewSession.ts:
   - Existing owned Linux process groups, TERM/KILL/reap, session invalidation and Build retirement are reusable.
   - Desktop shutdown must await this path; terminating only wsl.exe is not evidence that detached Linux children are gone.
   - New launch must create a new Host/project/session generation, never attach to an unidentified old service.

7. studio/vite.config.ts:
   - Production build is available, but no desktop packaging configuration or alternate base exists.
   - Existing root-based assets fit a packaged loopback asset server at its root; file-loading alternatives require an intentional base/URL/CSP review.
   - Include emitted JS/CSS/WASM and samples actually used by the app, with correct MIME types. Do not rely on development checkout assets.

## Recommended design after gate clearance
### Packaged window/assets
Windows Electron main owns one isolated BrowserWindow, contextIsolation on, nodeIntegration off, sandbox on; preload exposes only fixed desktop operations. Deny arbitrary navigation/new windows. Production serves a read-only packaged dist via a Windows-owned127.0.0.1 ephemeral server; no Vite/npm is required at runtime. This retains an exact HTTP origin rather than weakening Host checks. Main owns asset root and validates request paths; renderer cannot select the filesystem root.

### Host/runtime packaging and association
Package a versioned Host payload separately from the scientific project. Start it using validated Linux Node in the selected distro with a fixed executable/argument vector and Linux cwd. Runtime prerequisites must be explicit; either bundle an appropriately licensed Linux Node runtime or require/detect a supported runtime with actionable failure. Electron's Windows Node is not a substitute. No network install or long Build occurs automatically.

Represent association as distro + Linux user + canonical Linux project root + approved profile + executable + selected registered case/config/source. Preserve project provenance when Studio install/development checkout differs from the scientific root.

### CLI/project discovery
- arch-studio: walk cwd ancestors for an unambiguous ARCH root, otherwise native project picker.
- --case/--config: case is a registry ID; config is mapped and contained.
- --project/--binary: map absolute native/WSL inputs to canonical Linux paths, compare with approved Host profile/provenance.
- --source/--config: discover root from source and use authoritative source association from binary registry; reject ambiguity instead of guessing model from filename.
- Handle drive paths and WSL UNC paths with the actual selected distro. Do not strip slashes or guess that all Windows paths belong to the default distro.
- Spaces are separate argv data; no shell-built command interpolation.
- Unrecognized/missing Build profile stays explicit and non-operational; no automatic arbitrary build configuration.

### Ports and ownership
Bind rather than scan-then-assume-free. Windows assets and WSL Host each bind loopback port0 and report their actual address with a per-launch random identity. Confirm Host protocol, nonce and project before connecting. Test Windows/WSL localhost forwarding and collision behavior in the actual packaged run; a reachable unrelated4180 server is never accepted as owned. On a conflicting/failed forwarding path, release only owned listeners and retry boundedly or report a specific error.

A supervisor control pipe/EOF watchdog and authenticated ready/shutdown channel are needed. Do not expose an unrestricted shutdown/PID/command endpoint to the renderer. Reuse HTTP business endpoints and strict validators rather than duplicating scientific logic.

### Lifecycle
- Normal close: stop accepting actions; resolve unsaved-copy UX; request Host shutdown; retire Preview/AMR session and await process reap; close listeners; then close app.
- Active Build: keep a visible closing/wait state until the owned fixed Build finishes, or implement an explicitly designed cancellable Build path. Current signal refusal cannot count as cleanup.
- Parent disappearance: EOF/watchdog initiates cleanup of owned Host/session; bounded escalation must target verified ownership and generation, never all Node/ARCH processes.
- Startup failure: unwind only this launch's processes/listeners.
- Reopen: new nonce/project/session; no stale endpoint auto-attachment.
These are required implementation and test obligations, not proven behavior of a wrapper that has not yet been built.

### Native files and missing prerequisites
Project/Open/Save dialogs run in main and return validated selections through narrow IPC. Linux Host remains authoritative for project writes and conflict checks. Windows-export behavior must be distinct from project Save As if offered.
Missing WSL/distro/Linux runtime/project/build tree/binary/profile/Host handshake each needs a specific explanation and concrete next action. Window launch never triggers configure or Build.

### Distribution
Initial deliverable can be a versioned Windows unpacked application/archive plus command shim and startup documentation. Include Electron/Node/dependency notices and production assets. Do not bundle EOS tables or scientific executables without an explicit distribution decision. Installer/signing are separate costs; do not claim a signed installer. Pin framework/tool versions when implementation begins and verify the actual package on this machine.

## Required UAT evidence before final tag
- Independent Windows window/taskbar identity, project picker, actual native Open/Save As.
- Root/nested cwd, each CLI form, paths with spaces and WSL UNC/drive mapping.
- Missing WSL/runtime/binary and startup failure UX without implicit Build.
- Collision with an unrelated listener: no attach/kill; owned alternative port.
- Actual fixed Build, Save/Save As disk result, warm field Preview, matching AMR overlay in packaged app.
- Vite/dev processes absent for packaged launch.
- Normal close, close during Preview, active Build policy, relaunch and verified no owned Node/ARCH orphans.
- npm test/test:host/lint/typecheck/build/diff-check plus new launcher boundary tests.
Already-passed A/B functional baselines are references; C regression must target changed transport/lifecycle and required final checks.

## Gate outcome / next step
Architecture preference: **Electron**.
Implementation authorization prerequisite: **CLEARED — native Windows observation channel verified through the computer-use skill**.
Proceed with C2–C8 implementation. Do not create studio-phase2g-v0.16.0 until real packaged desktop UAT and final regression pass.

Next step: implement isolated Electron launcher and managed WSL Host entrypoint, then verify native dialogs and close/relaunch using the now-verified Windows tool. No new scientific Core contract is needed.

## Primary references consulted
- [Electron BrowserWindow](https://www.electronjs.org/docs/latest/api/browser-window): independent window and local content loading.
- [Electron security](https://www.electronjs.org/docs/latest/tutorial/security): isolation, sandbox and restricted navigation/privilege boundaries.
- [Electron native dialogs](https://www.electronjs.org/docs/latest/api/dialog): main-process open/save/directory dialogs.
- [Electron packaging](https://www.electronjs.org/docs/latest/tutorial/application-distribution): application distribution/packaging.
- [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/): Rust/MSVC and WebView2 requirements.
- [Tauri sidecars](https://v2.tauri.app/develop/sidecar/): external executable packaging; does not make Windows binaries Linux runtimes.
- [Microsoft WSL filesystems](https://learn.microsoft.com/en-us/windows/wsl/filesystems): Windows/Linux filesystem boundary.
- [Microsoft WSL networking](https://learn.microsoft.com/en-us/windows/wsl/networking): localhost access depends on WSL networking mode; verify actual association.

No dependency installed, launcher source written, baseline rebuilt, package published, tag changed or push performed during C1.

## Implementation verification notes (2026-09-22)

The production wrapper uses a Windows loopback asset server plus same-origin API proxy
to the explicitly spawned WSL Node Host. Both listeners select OS-assigned ephemeral
ports; a random launch token and exact Origin/protocol/Host checks authenticate readiness.
The project owner is resolved in WSL (this machine's default WSL user is root, while
the project/runtime belong to arch). WSL UNC distro identity is retained.
Host stdin EOF is the ownership lease; closing the window closes the pipe, shuts down
Preview, awaits an active Build and reaps the Host. Single-instance ownership prevents
two desktop windows from competing for one project Build state.

Packaged CSP permits eval only because the pre-existing ndarray dependency compiles
array accessors with Function (ndarray.js lines 47, 80, 251). Scripts otherwise load
only packaged same-origin assets. Node integration remains disabled, renderer sandbox
and context isolation enabled; IPC remains narrow and origin/sender checked. This is
a documented dependency constraint, not a claim of eval-free renderer execution.
Native file dialogs preserve Host create-new / containment validation.

Desktop development validation uses the exact production package; no Vite-dependent
desktop development branch is shipped. Packager produces a portable Windows x64 folder,
with Electron license notices. Signing, installer, auto-update and Native Windows Core
are outside Phase 2G-C. Detailed actual UAT evidence is in the desktop launch report.
