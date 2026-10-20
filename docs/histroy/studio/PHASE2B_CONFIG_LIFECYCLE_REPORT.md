# Phase 2B Config Lifecycle completion report

## Baseline
- Base tag: `studio-phase2-v0.5.0`.
- Base commit: `92e260a419120679d313f23753c59061991b8371`.
- Independent branch: `studio/phase2b-config-lifecycle`.
- Worktree: `/home/arch/projects/ARCH-phase2b-config-lifecycle`.
- Baseline verified clean before branch creation; fresh npm ci and 59 baseline tests passed.
- Active scope: PHASE2B_TARGET.md, P2B-M0 through M9. Original development worktrees and root STATUS.md were not modified.

## Protocol 1.1
Frontend and service require version 1.1; old protocol is rejected. writeConfig is true on the supported Linux service only. readProject remains true; build, preview and watchFiles remain false. Structured config errors replace raw Node exceptions.

## Config association
Open Project Config is explicit. Host returns exact UTF-8 text, project ID, relative path and SHA-256/size/mtime fingerprint. The existing ParDocument creates the Working Copy. Loaded fingerprint, saved fingerprint, editor state and disk state remain separate. Connection/Refresh never replaces editor contents. Save requires matching project and selected config association plus write capability.

## Save algorithm
The existing serializer supplies exact text. Host validates the request, rereads and checks the expected saved fingerprint, checks write access, creates a temporary file in the same directory, writes all text, flushes and closes it, rechecks the parent and disk fingerprint, then atomically renames it over the original. Final bytes are reread and fingerprinted. Successful Save updates the saved snapshot and config identity; it neither generates Preview nor clears stale Preview.

## Atomic write behavior
The Linux implementation pins the canonical destination directory with a file descriptor and uses a unique exclusive no-follow temporary file. Original mode bits are preserved where applicable. Content is fsynced before publication; directory sync is attempted. Injected write/rename failure and permission failures preserve original contents and remove temporary files. No truncate-first save is used. Native Windows host writing remains disabled rather than claiming equivalent filesystem guarantees.

## Conflict model
Expected fingerprint includes digest, size and modification time. External changes are checked even without prior Refresh and return a conflict. The Working Copy remains intact, with explicit Reload, Save As and Cancel. Revert uses the latest successful Load/Save/Save As in-memory snapshot; it never reads external changes. Dirty/Invalid replacement operations show a guard. No automatic merge or force overwrite exists.

## Save As behavior
Destination is a plain project-relative .par path under the authorized root, with an existing real parent directory. Publication uses an atomic no-clobber link of the completed temporary file; existing targets are refused. Success updates the current project parameter file and editor association. Original file remains unchanged. Browser Download Copy remains available independently.

## Security boundary
Loopback-only host, exact Origin/Host, dedicated studio/protocol headers, fixed endpoints, strict JSON fields and a 1 MiB request-body cap remain enforced. Config text/read cap is 1 MiB. No general path read/write, shell, execution, mkdir, delete or permission API exists. Traversal, absolute/encoded/backslash/NUL paths, non-.par destinations, symlink components/targets, missing parents and existing destinations are refused. Operations within one host session serialize; stale concurrent saves conflict.

## Manual UAT
A finite agent-operated browser UAT used the production frontend at 127.0.0.1:4178 and Local Host at 127.0.0.1:4180, with disposable copied config files in `/tmp/arch-p2b-uat-koyi680o`. This is development verification, not a claim of new human acceptance.

- Explicitly loaded case.par; changed nblockx1 4 to 8, saved, reloaded: 8 persisted and Config was Saved.
- Byte comparison confirmed only the intended value changed; comments and unknown user_note text survived.
- Edited 8 to 12, Revert returned 8.
- Edited Working Copy to 12; external disk edit changed 8 to 16. Save without prior Refresh reported conflict, retained 12 in the editor and did not overwrite disk 16.
- Refresh retained the edited Working Copy. Cancel replacement retained it as well.
- Save As `../escape.par` was rejected and Working Copy retained.
- Save As `experiment.par` succeeded, became current config and was Saved/in-sync. Original case.par remained 16; new file contained 12; no .arch-config temporary file remained.
- Edited new config 12 to 20, Reload showed unsaved guard; explicit Discard and Reload restored disk value 12.

## Regression
Existing Mock/preview state, parameter editing/history, numeric controls, real .par exactness and real 1D Plotfile tests continue to pass. ParDocument and parState/parser/serializer were not changed. Only studio/ files changed; no ARCH Core, scientific fixtures or root STATUS changes. No CPU/CUDA/ARCH execution was repeated.

## Automated checks
Final run on WSL Linux, Node 24.21.0 / npm 11.19.0:

| Check | Result |
| --- | --- |
| npm test | PASS: 67/67 |
| npm run test:host | PASS: 16/16 (subset of full suite) |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS |
| git diff --check | PASS |

Expected negative HDF5 diagnostics are emitted by invalid-file tests. Vite reports the existing large-bundle advisory; build succeeds. No tests were disabled to obtain these results.

## Deferred
Build/Run, ARCH execution, Setup()+Init() preview, AMR/graphical binding, SSH, parameter metadata backend, watcher, automatic merge and force overwrite remain outside scope. No Phase 2C work or push was performed.

## Remaining issues and limits
- Safe writes are qualified for the WSL/Linux service, not a native Windows Node host.
- Optimistic fingerprint checking is not a filesystem compare-and-swap transaction with unrelated external editors. Such a writer can race the last check/rename interval; avoid simultaneous external writes during Save. Readback detects observable post-publication changes, but cannot eliminate this OS-level interval.
- A post-publication readback error can mean disk changed despite an error response; the Working Copy is retained and explicit Reload/Refresh is required to inspect disk.
- JSON escaping/header fields count toward the 1 MiB body cap, so a near-limit config may be readable but too large to save through JSON.
- No full-disk/power-loss fault simulation or independent human Phase 2B acceptance is claimed.

## Checkpoint
Commit message: `feat(studio): add safe local config file lifecycle`.
Tag: `studio-phase2b-v0.6.0` (local only). Resolve its exact commit with `git rev-parse studio-phase2b-v0.6.0^{commit}` after creation. node_modules, dist, .local, disposable UAT files and screenshots are excluded from the checkpoint. Stage stops here.
