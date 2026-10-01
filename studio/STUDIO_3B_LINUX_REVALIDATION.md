# Studio 3B independent Linux revalidation

Status: automatic checks passed; Linux native file-dialog UAT pending.
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

Computer Use application access to the WSLg window timed out. Open-file,
Save As and Reopen are therefore NOT signed off in this Linux run.
The permission question remains pending; no substitute automated assertion
is presented as native-dialog UAT.

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
Complete the Linux UAT, then deliver the candidate contract and shared
fixtures before changing implementations, following the joint handoff.
