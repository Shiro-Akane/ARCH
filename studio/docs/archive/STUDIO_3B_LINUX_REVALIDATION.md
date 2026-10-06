# Studio 3B independent Linux revalidation

Status: automatic checks and the scoped Linux open / Save As / reopen UAT passed.
This is an evidence update, not a new 3B release or a completed Linux launcher.

## Exact inputs

- compute/optim: 8fc0dd25eefd2243e8c36f85440bac46994e2e73.
- Reviewed Studio subtree: c96e9da0a6d114fd0323102b73dba4a3cd8da9ba, studio-phase3b-v0.21.0.
- Integration import: 8b7080a63d234fbc3977fdf79b91632b7f4bca4e.
- Workspace: /home/arch/projects/ARCH-compute-optim.
- Branch: studio/compute-optim-integration.
- Fresh npm ci; Node 24.21.0 / npm 11.19.0. No reused node_modules.

## Executed checks

| Command | Result |
| --- | --- |
| npm ci | PASS |
| npm test | PASS, 177 tests; Host coverage is included |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS |

Local logs: studio/.local/integration/npm-ci.log, 3b-check-0.log through
3b-check-3.log, and 3b-checks.json. No duplicate Host run or Core rebuild
was required to establish this unchanged Studio import's automatic result.
Existing Core evidence has not been relabeled as a new build or new test run.

## Linux UAT boundary

Linux Electron starts under WSLg with the new production assets at
http://127.0.0.1:4194. The ignored test harness is not the planned 3C launcher.
No Windows executable launcher or forwarding script is used.

The Host is explicitly associated with /home/arch/projects/ARCH-mainline,
not with the Studio development checkout. It uses the existing CPU executable
build-phase3a-cpu/bin/ARCH, SHA-256
fb4f9de20f6ca152bf016d56182b15a6760a83224fbec6ab8643a4f0c2b5d9a4,
and isolated ignored configuration studio/.local/integration-3b/sod.par.
Its fixed Build Profile still reports dependenciesComplete=false; this is
not complete dependency-based freshness evidence.

The initial application-access timeout was followed by a WSLg display failure.
Weston logged shared-memory EIO and use_gfxredir=0; the window was transparent
and carried WARN:COPY MODE. With explicit user authorization, Weston was
restarted; use_gfxredir=1 and a visible Studio window returned. No project
source or permanent system setting was changed.

Computer Use then exercised the actual Linux GUI:
- Connect Local Host; enter Real Config; open the native GTK file picker.
- Load the isolated project configuration through Open Project Config.
- Save Working Copy As using the project-relative form.
- Confirm the new current path and disk in-sync state.
- Use native Open File to reopen sod_copy.par; observe its name, successful
  input inspection and 95 current standard controls. Browser import correctly
  loses trusted Host-path association.

The saved file is studio/.local/integration-3b/sod_copy.par under the managed
project. It is 951 bytes and byte-identical to sod.par, SHA-256
cd54c4000a8bd3917f5b3fb658c2614f91bddc7ebd84e7e076f6c3bb79489284.
No shell command created the copy; the read-only filesystem check verified
the UI write. Save As used the existing browser-mode form, not a claimed
3C native Save dialog. Existing automatic preview behavior was observed;
no simulation or formal scientific output was requested.

Non-blocking UX observation: the Save As form opens near the top of the
scrollable parameter pane while its trigger is at the bottom; it requires
scrolling back to see. Several action labels also have weak contrast in the
Linux theme. These observations are not recorded as repaired.

Local process handles and logs are in studio/.local/integration/uat-processes.json.
PIDs are observations, not durable process identities; verify command/cwd
before cleanup. The test processes remain available for the pending UAT.

## Dependency observation

npm audit reports one high-severity indirect development dependency:
brace-expansion 5.0.9, reached through minimatch in lint/packaging tools.
The saved audit includes GHSA-q2hr-2g5m-vwhr, GHSA-qhr7-859c-m2p7 and
GHSA-6j4f-fj2g-mc7p. No exploit or production exposure is asserted.
No blind audit fix or lockfile change was performed while reproducing the
reviewed baseline. The local full response is npm-audit.json.

## Next contract boundary

The checked-in configuration API still documents implemented extension v2.
The new O7.0 plan requires v3 nullable parsed values, explicit presence,
conditional requirements and allowed-default provenance. Passing the checks
above does not certify v3, G retirement, Run/Restart, JENS or RZ.
The scoped Linux UAT is complete. Next deliver the candidate contract and shared
fixtures before changing implementations, following the joint handoff.
