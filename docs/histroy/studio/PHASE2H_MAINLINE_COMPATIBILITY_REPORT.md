# Phase 2H — Mainline / Desktop Compatibility Report

Date: 2026-09-27. Result: **PASS WITH NON-BLOCKING ISSUES**.
Scope: authorized Phase 2H-C Desktop / Workstation Compatibility Regression only.
Target: PHASE2H_TARGET.md sections 28–35 plus the user's C acceptance checklist.
No implementation change, repeated A/B migration, Core build, simulation or push.

## Exact baseline and delivery

- Branch: studio/phase2h-mainline-capability-sync.
- A: studio-phase2h-a-v0.17.0 → 01860f291d5348531eb84cbb6a505ca7a21a9d1b.
- B / tested implementation: studio-phase2h-b-v0.18.0 → af3c94aee31d748e4dbc9ecc8deb85728dafc3a6.
- A annotated tag object: 5cc514a5aa9bea7d504980b901a8161e15f46e13.
- B annotated tag object: 0a106007ff0db4aa3012f982d95fd0d65aecd7cc.
- Both tag objects and peeled commits verified unchanged at closure.
- Final local checkpoint: studio-phase2h-v0.19.0. Resolve the annotated tag's
  peeled commit for the final reports-only commit; implementation remains B.
- Project: /home/arch/projects/ARCH-phase2h-mainline-capability-sync.
- Windows candidate: E:/.Codex/.ShiroAkane/releases/ARCH-Studio-v0.19.0-candidate-win32-x64.
- WSL distribution: ARCH-Ubuntu-24.04; project owner: arch.

## Packaging and binary identity

Built an independent Windows x64 package using the existing desktop/package.mjs
and B's already validated production dist. No Vite server or reused browser
session participated in desktop UAT. The package's dist/, host/ and src/ were
compared file-by-file with the B worktree: zero mismatches. No node_modules or
generated package was added to Git.

Windows Electron 44.4.3 owns the independent window, loopback asset/proxy server,
and WSL Node Host. WSL Node v24.21.0 / npm 11.19.0 remain the approved environment.
The actual CPU executable is:

/home/arch/projects/ARCH-phase2h-mainline-capability-sync/build-phase2h-cpu/bin/ARCH

SHA-256: 53fdf7939cf4d68c806b206f1e7cf2f4d0e6d3724682a203af6e01250375fcf7

Build ID: 819e0395-cf95-4138-856d-0fa489386973.
Profile: arch-mainline-cpu-integration.
Profile fingerprint: 6c99ca19a9869f83f57eb781b8416a5cd91d6f1ff28d59f99c9de4f273ed9182.
The 77 explicit input fingerprints and binary match the successful Build.
The historical manifest source HEAD is the Core sync commit 86f999bafccf5868cef4e4258ff0e166048a35ae;
later Studio/docs commits do not change those tracked build inputs.
Repository dirty at build time is not treated as scientific-input dirtiness.
dependenciesComplete=false remains truthful: complete dependency freshness is
unknown, while the tracked inputs and output are validated. No fake new Build
Manifest was generated for packaging.

## Native Windows desktop smoke

Actual native Windows interactions used computer-use / @oai/sky. Browser mode did
not substitute for desktop UAT. Configs were disposable ignored copies under
studio/.local/phase2h/c; original scientific .par files were untouched.

| Required item | Evidence / outcome |
| --- | --- |
| Packaged startup | Independent Electron window, owned WSL Host handshake, packaged assets; PASS |
| Current CPU identity | UI Build 819e0395 / binary 53fdf7939cf4, full hash checked; PASS |
| 92-key schema | Packaged UI displays 92 standard keys / 92 controls; runtime schema extension v2; PASS |
| 14-model registry | Native model dropdown contains current binary's 14 cases, including GravityBox/JeansWave/SNIaCoupled; PASS |
| Five retired keys | Legacy file opens unchanged with 5 migration issues, separate retired controls and blocked Preview; PASS |
| Four Gravity keys | gravity_boundary, gravity_rtol, gravity_atol, gravity_max_cycles present from runtime schema; omitted values labeled Schema Default / not written; PASS |
| self gravity | GravityBox self + periodic accepted; schema describes real applicability, no global unavailable label; PASS |
| JENS | Working Copy retains JENS; Core validation error; no fallback. Test edit undone; PASS |
| Source association | --source simulation/GravityBox/GravityBox.cpp selects GravityBox and displays authoritative source; current paths, no stale renamed source; PASS |
| Build Manifest | Existing successful manifest accepted with tracked-input/full-freshness distinction; PASS |
| Session | Actual packaged warm generation 1 / sequence 4; cancellation/recovery below; request-limit recycle retains B verified evidence; PASS |
| Sod field | Open, inspect-case, warm update, authoritative x_pos drag to 0.4002808988764045, matching real response; PASS |
| Cellular field | 128x128 then 256x256 real 2D; Density → Temperature, correct colorbar and raw units; PASS |
| Initial AMR | Sod 8 leaves; Cellular 20 leaves, L1:4/L2:16, complete; matching field/config/build/EOS overlay; PASS |
| Save lifecycle | Save, native Windows Save As, exact disk comparison, close/relaunch/Reopen; PASS |
| Cleanup | idle warm, active field and active AMR scenarios below; PASS |

Sod Save As used the native Windows dialog and created
studio/.local/phase2h/c/Sod-saved-as.par. Its bytes matched the explicitly saved
Sod.par; relaunch opened that path and retained x_pos=0.4002808988764045.

The combined legacy fixture contained all five retired keys. The UI identified
each as retired rather than Custom/alias; Core reports the first diagnostic and
other rows honestly await matching Core diagnostics. Removing timeintegrator
reduced issues from 5 to 4; one Undo restored 5. It was removed again, then the
other four were explicitly removed. Before Save, all five assignments still
existed on disk. After explicit Save, all five assignments were absent and all
five trailing comments remained. Open Project Config reloaded the disk result,
valid inspection and Saved state without retired controls. B already contains
individual-key Undo regression/UAT; it was not repeated for every key in C.

GravityBox was launched via --source, inspected successfully, and labeled
inspection only. Full field Preview was disabled and no Generate initial AMR
action was offered. No gravity potential/acceleration fields were invented.

## Lifecycle verification method and results

Ordinary Preview/field/AMR/Save/retired flows above were native UI operations.
Warm requests can complete before a manual cancellation click: two initial UI
attempts ended before cancellation and were NOT counted as cancellation PASS.

For precise lifecycle timing, an ignored test driver called the existing
authenticated API of the Host owned by the actual packaged Electron window.
It did not create a substitute Host, alter product code, inject UI state, modify
Core, or run simulation. Cancellation reached state cancelled and a following
request reached succeeded. Last successful field retention remained explicit in
the UI when the driver used a different configRevision.

For active-close tests, the driver asserted the real Host state immediately
before a native Alt+F4 window close. This is instrumented packaged-Host lifecycle
coverage, not a claim that a human clicked Cancel at that exact instant.

| Close scenario | Host / Windows bridge | Observed boundary | Clean shutdown UTC |
| --- | --- | --- | --- |
| Idle warm Sod | 7030 / 37564; ARCH worker 7047 | live idle --preview-session before window close | 02:41:39.889 |
| Active Cellular field | 494 / 8668 | generating; request 9f0b4c71-b377-4e2f-a5f6-cdab6760e688 | 02:54:40.125 |
| Active Cellular AMR | 486 / 41172 | running; request 8fa61f62-3a56-4a48-bd9c-9db83a2ec335 | 02:55:42.250 |

Active AMR used the supported initial-only operation with a disposable unsaved
configuration, lrefinemax=9/refine_threshold=0.001 and published limits of
1024 blocks / 256 MiB, solely to exercise active shutdown. It was not accepted
as a completed scientific result.

After each scenario, Windows arch-studio processes and WSL Node/ARCH process
lists were empty. The desktop log recorded owned Host exit and clean shutdown.
Current AMR uses the owned session worker; no separate AMR worker or orphan child
remained. Real relaunch succeeded after prior cleanup.
B's actual request-limit/recycle and latest-only queue tests are retained, not
rerun or falsely relabeled as new C native UI tests.

## Regression provenance and final audit

The implementation is byte-identical to B; C changes only these two reports and
studio/STATUS.md. User explicitly prohibited repeating A/B work. Retained B gates:

- npm test: 171/171 PASS.
- npm run test:host: 70/70 PASS.
- lint, typecheck, production build: PASS.
- Current Core targeted suite: 13/13 PASS (231.18 s); simulation oracle disabled.
- Session recycle, source hashes for all 14 models and field/AMR identity tests:
  PASS, detailed in PHASE2H_B_CAPABILITY_MIGRATION_REPORT.md.

C additionally verified fresh Windows packaging, package/source byte identity,
new native UAT, real packaged lifecycle cancellation/cleanup, exact unchanged
Core tree (18,928 non-Studio tracked entries against 502eadcb), immutable A/B
tag objects, legacy disk safety and final git diff --check.

## Non-blocking observations / limits

- Generated desktop package metadata still says 0.16.0 (existing packaging script).
  Do not use this metadata as checkpoint identity: tested Git commit, package
  folder and recorded file hashes identify this candidate. C did not relabel or
  refactor the frozen implementation.
- Existing desktop README CLI examples name the older sample project; the tested
  explicit current project/source/binary arguments work. This report gives the
  actual tested paths.
- Some inherited Save/retired-action button text has low visual contrast. Actual
  operation succeeded; no styling change was made in this regression-only stage.
- Native file dialog accessibility focus/index output was inconsistent; observed
  screenshot and keyboard input verified the actual filename before saving.
  This tooling issue is not evidence of a product failure.
- Portable unsigned package, no installer/update/signing guarantee.
- Full field/AMR still limited to Sod/CellularDet; registered != supported.
  AMR field colors remain Init samples, not cell-value arrays.
- No CUDA qualification, simulation, new scientific functionality or Phase 3.

Ignored evidence: studio/.local/phase2h/c/{package.log,package-full-identity.json,
native-uat-evidence.json,packaged-cancel-recovery.json,active-preview-start.json,
active-amr-start.json,final-static-audit.json,desktop-lifecycle.log}.
Windows copy: E:/.Codex/.ShiroAkane/.local/phase2h-c-uat/native-uat-evidence.json.
Temporary validation scripts/configs are ignored and excluded from the checkpoint.
