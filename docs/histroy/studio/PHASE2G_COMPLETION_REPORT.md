# Phase 2G completion

Date: 2026-09-22. Authorized A → B → C work complete.
Final local checkpoint: studio-phase2g-v0.16.0.
Branch: studio/phase2g-desktop-launcher; resolve annotated tag for exact final commit.

## Immutable checkpoints

- A: studio-phase2g-a-v0.14.0 → 13345ba4d1b7379ff5481bf9ca4f2fc9156aee7c.
- B: studio-phase2g-b-v0.15.0 → 2cd4dbbabc6a17123950a4b2ab6d612a6c0831b2.
- C is a studio-only increment from B. Neither previous tag was recreated or moved.

## Delivered

A: continuous authoritative CPU Preview with owned session generation, latest-pending
coalescing, cancellation/reaping and last-successful retention.
B: binary registry/11-model inspection, AMR parameter/resources workflow, real initial
Sod/Cellular hierarchy and identity-safe field overlay.
C: actual Electron Windows desktop, packaged React assets, semantic CLI, native project
and Save As dialogs, Windows↔WSL ownership/path mapping and managed Host/session shutdown.

Reports:
- PHASE2G_A_SESSION_COMPLETION_REPORT.md / PHASE2G_CONTINUOUS_PREVIEW_REPORT.md
- PHASE2G_B_AMR_COMPLETION_REPORT.md / PHASE2G_INITIAL_AMR_REPORT.md
- PHASE2G_C_DESKTOP_ARCHITECTURE_AUDIT.md
- PHASE2G_DESKTOP_LAUNCH_REPORT.md

## Validation

C final npm 164/164, Host 69/69; lint, typecheck, production build, Windows packaging and
diff-check PASS. Actual native Windows + WSL UAT covered project root/nested cwd,
space-containing paths, picker/recovery, missing prerequisites/binary, port ownership,
Build, Save/Save As, warm Preview, AMR overlay, close and relaunch. Final owned Node/ARCH
processes were absent after close; unrelated Host was left running.

A/B Core scoped 18/18 and their real UAT remain recorded in the existing reports.
C did not alter scientific Core or rerun those tests. No simulation, CUDA baseline,
new Core build tree, Phase 3, main merge or push.

## Portable artifact

Windows directory: E:\.Codex\.ShiroAkane\releases\ARCH-Studio-v0.16.0-win32-x64
Entry: arch-studio.exe.
Requires existing WSL2 Linux Node 24+, approved fixed profile and valid CPU binary/manifest.
Unsigned portable distribution; no installer or auto-updater. See desktop/README.md.
Extracted size approximately 375 MiB. Production assets are shipped outside Git.

Final state: local checkpoint and STOP. Publishing requires separate authorization.
