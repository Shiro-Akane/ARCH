# Phase 2G-C — Local Desktop Launcher completion

Date: 2026-09-22. Result: PASS on the local Windows + WSL2 environment.
Scope: PHASE2G_TARGET.md C1–C8 and the user's C authorization; no Phase 3 or push.
Baseline B: 2cd4dbbabc6a17123950a4b2ab6d612a6c0831b2.
Branch: studio/phase2g-desktop-launcher. Final checkpoint: studio-phase2g-v0.16.0.

## Architecture and delivery

The architecture audit preceded implementation; see PHASE2G_C_DESKTOP_ARCHITECTURE_AUDIT.md.
Electron 44.4.3 reuses the React production bundle and existing WSL Node Host.
Tauri would add Rust/MSVC/sidecar work without removing the Windows–WSL boundary.
The portable Windows x64 package is approximately 375 MiB extracted. It includes
Chromium/Electron, production React assets, Host source/runtime inputs and license notices.
It does not include scientific binaries, EOS tables or a new build tree.

Default entry is arch-studio.exe; a bundled arch-studio WSL shim preserves cwd/distro.
No npm terminal, Vite process, browser or manually copied port is required.
WSL2, Linux Node 24+, approved Host profile and current tracked CPU Build are prerequisites.
Tested Linux Node 24.21.0 / npm 11.19.0. Windows Node/npm are not launch prerequisites.

Windows path:
E:/.Codex/.ShiroAkane/releases/ARCH-Studio-v0.16.0-win32-x64.
Linux packaging output: studio/.local/desktop-release/arch-studio-win32-x64.

## Ownership and safety

- Two OS-assigned ephemeral loopback listeners: packaged Windows assets/proxy and WSL Host.
- Readiness authenticates the owned Host using a random token plus existing exact
  Host/Origin/protocol checks; it never adopts whatever happens to occupy a port.
- WSL UNC distribution association is preserved; Linux runtime runs as project owner.
- CLI accepts semantic project/binary/source/config/case/distro/cwd values, not commands.
  Existing fixed Build Profiles retain canonical managed-root/binary binding.
- Source matching uses authoritative absolute/relative registry paths; duplicate,
  unknown, null, outside-root and case-disagreeing associations fail closed.
- Renderer sandbox/context isolation are enabled; no Node integration. IPC is narrow.
  CSP limits scripts to packaged same-origin assets, with the documented eval allowance
  required by the existing ndarray accessor generator.
- Native Save As retains Host create-new, containment, symlink and conflict safeguards.
- Dirty close asks first. Host stdin EOF is the ownership lease. Shutdown retires
  Preview, waits active Build completion, closes sockets and lets bounded in-flight
  inspection children drain before Node exits. No killall or unrelated-process cleanup.
- One desktop instance at a time; a second invocation focuses the existing window.
- Browser mode remains optional/debug; frontend scientific/parser contracts are reused.

## Actual Windows + WSL UAT

Native Windows UI automation used the computer-use skill, not browser-only screenshots
or a simulated DOM. Screenshots and accessibility trees were inspected in the task.
All writes went to the ignored studio/.local/Desktop UAT directory.

| Requirement | Observed result |
|---|---|
| Independent window / packaged assets | PASS. ARCH Studio project-titled native window; production bundle served by Electron, no desktop Vite process. |
| Root launch | PASS. No arguments, Windows cwd = WSL UNC project root; discovered project and simulation/Sod/Sod.par; Preview Current. |
| Nested launch | PASS. WSL shim from simulation/Sod with --case Sod and ../../studio/.local/Desktop UAT/Sod desktop.par; correct project/config and Current. |
| --project / --binary | PASS. Final package with explicit root and build-preview-audit/bin/ARCH; Current. |
| --source / --config | PASS. Absolute Sod.cpp registry association plus explicit binary and spaced config; Current. No basename inference. |
| Paths containing spaces | PASS. App initially ran from Phase2G C Candidate; configuration directory/file both contained spaces. |
| Native project picker | PASS. Windows directory dialog selected WSL UNC project; missing-binary launch recovered to a connected project and Current Preview. |
| Missing binary | PASS. Nonexistent ARCH-missing produced explicit missing-executable error, no automatic Build. |
| Missing WSL/runtime | PASS for exercised prerequisite failures: nonexistent distro produced a recoverable launcher error; initial Node-missing runtime path produced an actionable Node 24+ message. No WSL installation was removed. |
| Port collision / ownership | PASS. Existing unrelated WSL Host PID 5399 still occupied 127.0.0.1:4180; final desktop Host PID 24495 used 127.0.0.1:39985. |
| Build | PASS through native UI. Fixed-profile Build succeeded at 2026-09-22T10:28:50.944Z. No configure command or simulation was issued by launcher. |
| Save As | PASS through Windows native dialog. New studio/.local/Desktop UAT/Sod desktop.par created; initial SHA-256 cda6ed59c87765c564368225f07620396e26bd57c842d8612ac7620c2a055170 matched source. |
| Save / warm Preview | PASS. x_pos edited .43 → .47 in native UI; real Preview became Current, same owned session PID 10967; Save wrote x_pos=.47 to the new file. Original reference unchanged. |
| AMR overlay | PASS. Generate initial AMR returned Complete; UI displayed matching field/config/build/EOS identity and actual 1D block/tick overlay. No AMR field-value claim. |
| Window close / no orphan | PASS. Final Windows main PID 43292, bridge 4328, WSL Host 24495 and ARCH 24524 gone after close; log records owned Host exit and clean shutdown at 10:47:39Z. Existing unrelated Host 5399 remained. |
| Relaunch | PASS across independent launches; new Host PID/port each time, no adoption of old session. |

Active-Build close behavior is implemented as wait-for-completion, not cancellation;
this UAT Build was incremental and completed before close. It is not claimed as an
interrupted long compiler stress test. Existing A/B scientific UAT is retained rather
than repeated under a claim of new scientific coverage.

## Final checks

- npm test: 164/164 PASS, no skips/failures.
- npm run test:host: 69/69 PASS (subset of 164).
- npm run lint / npm run typecheck / npm run build: PASS.
- Windows production packaging: PASS with @electron/packager 20.3.0.
- git diff --check: PASS.
- Core 18 groups: prior A/B records remain authoritative; no Core changes or rerun.
- Existing Vite large-bundle advisory remains non-blocking.
- Added coverage: strict CLI/path/distro parsing, authoritative registry path matching
  and desktop ownership-token enforcement.
- Desktop-only fixes found by UAT: WSL default-user mismatch, forwarding readiness race,
  absolute registry source paths, packaged module type, ndarray CSP compatibility.

## Distribution limits / stop

Portable unsigned local Windows x64 + WSL2 package. No installer, code signing,
auto-updater, arbitrary project-to-build integration or native Windows scientific Core.
Only registered Host Profiles can build. WSL localhost forwarding must function.
A/B tags unchanged. All delivery changes are under studio/. No dist/node_modules/.local
or test screenshots are tracked. Root STATUS.md and scientific Core unchanged.
STOP at studio-phase2g-v0.16.0; no automatic push or Phase 3.
