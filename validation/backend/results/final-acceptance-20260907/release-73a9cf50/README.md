# Final CPU/CUDA candidate acceptance

Chinese translation: [README.zh-CN.md](README.zh-CN.md). English is authoritative.

The owner-approved CPU/CUDA release profile has completed technical acceptance.
The [final index](index-final-930/README.md), produced on 2026-09-07 at
19:49:58 UTC, closes all 41 required gates: 41 pass, zero pending and zero
failed. The two deferred entries are the owner-approved audit150/audit200
large-network qualification workloads, not local passes.

This conclusion applies to the reviewed working tree. It is ready for source
release preparation; no staging, commit, push or release tag was performed.
Timmes free use and redistribution authorization remain the separately
recorded administrative item in the [third-party notices](../../../../../THIRD_PARTY_NOTICES.md).
Attribution and successful testing do not supply that confirmation.

## Completed scope

| Area | Final evidence |
| --- | --- |
| Regression | CUDA-enabled Release 98/98, CUDA-enabled Debug 98/98, CPU-only 31/31; no skipped tests |
| Scientific verification | Independent hydro/Sedov/time, geometry, diffusion, EOS, burn, NSE, external-gravity and weak-reaction references; original accuracy and conservation budgets |
| CPU/CUDA applications | Cartesian, cylindrical and spherical configurations; uniform and dynamic AMR, generated networks, all three burn ODEs and actual KLU/cuDSS execution |
| AMR and restart | Complete Cartesian 3D and curved 2D refine/coarsen/refine lifecycles; strict native checkpoint recovery in all four backend directions; sustained regridding and continuation |
| Device safety | Focused memcheck and racecheck each pass 23 complete routes; additional complete application checks cover dynamic 3D AMR, coupled curved AMR and burning restart |
| Build and capacity | Optimized cold/incremental core compilation and declared host/device workloads complete under the resource guard |
| Documentation and assets | Explicit review of 38 current documents and 172 asset file identities, including all 139 untracked maintained inputs |

The public [validation overview](../../../../README.md) links the module results
and their scientific scope. CPU and CUDA continue to share mathematical and
physical authorities; backend-specific storage, kernels, transfers and solver
adapters remain separate. The [capability guide](../../../../../docs/CudaBackendStatus.md)
describes the supported configuration and factory/duck-typed interfaces.

The [focused instrumentation guide](../../final-first-law-20260907/README.md)
records the approved time-window distinction. Ordinary audit31 science and
memcheck retain the full `1e-10 s` interval. Racecheck alone uses `1e-12 s`,
with all three ODEs, both storage sizes, four subdivisions, real cuDSS calls
and unchanged field/limiter budgets. It does not replace scientific trajectories.

The [core build measurement](../../cold-core-first-law-20260907/release-909/README.md)
retains optimized commands and uses two heavy jobs within four total jobs as the
measured mid-range reference. The cold ARCH build took 2213.445 seconds, the
no-op check 1.012 seconds and the compact-route incremental build 27.323 seconds.
Cold-build peak owned RSS was 3763288 KiB and swap growth was 77336 KiB, without
a guard stop. These are measured workloads, not a universal minimum or optimum;
larger machines retain configurable concurrency. Device allocator requests and
whole-device memory observations are separated in the
[capacity report](../../device-memory-first-law-20260907/README.md).

## Frozen identities and audit

| Item | SHA-256 |
| --- | --- |
| Maintained code/input scope | `73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171` |
| Reference ARCH executable | `9bba8f657a46ccfdf5b264387b9269566b6a7e724759027f63e4cd45aa7fe3ba` |
| Reference checkpoint comparator | `e5cc53d3d19a8ad547f1d990353cc55d55d884ce24dcd2e4f99621dd50825bbc` |
| Final index JSON | `2af13844b90959a57e4479fc269641cd87ce1b8c6ef2404f62f59c5f8f690b85` |
| Explicit delivery review | `9bf9f7de57fd4616bbf7dfe6ebeda508132df2162ed510e3fc4e96a483904857` |
| Index recipe | `50ced592b529c3416e07f162903fccf7b63f15825396a7da782c9d5c58cf0520` |

The base commit is `2226456c1f87415a317dd6a2a96053a7e154b949`; the code/input
fingerprint also includes the reviewed dirty-tree modifications and additions.
The [machine-readable index](index-final-930/index.json) checks 1517 referenced
file identities, records all 43 entries and preserves ten historical attempts.
Earlier failed and partial runs retain their actual outcomes and successor links.

The index is an audit of existing results, not a new simulation or an automatic
scientific/legal certificate. Its `release_qualified: false` field intentionally
remains unchanged. The technical conclusion here combines the passing evidence
with the explicit [content and source-readiness review](delivery-review.json).
The index ran alone under the existing guard: 126.407 seconds, peak owned RSS
49412 KiB, minimum available 7094952 KiB, swap 234496 to 237056 KiB, no stop.
Its retained local log is `build/index-final-930.log`, SHA-256
`3f611cae5ce2119c058afacfd40a96e2bd59ab50d5a2b57ef14d7ecc88bece99`.

## Source publication handoff

- Include the 139 maintained additions and intended deletions identified in the
  [source-asset review](delivery-assets-README.md), together with documentation
  and notices. An archive of the current HEAD alone omits this work.
- Keep the actual Helmholtz Git LFS object available. Generated networks are
  rebuilt from maintained recipes; exact generated-package redistribution also
  needs its upstream notices and rate-data provenance. cuDSS is separately installed.
- Preserve the distinction between source delivery and a portable binary package.
  The reviewed binaries contain local CPU optimization and configured CUDA images.
  Large HDF5 outputs and local build artifacts are not automatically part of Git;
  this recorded identity audit requires those original artifacts, while a new
  machine reproduces the campaigns into new result directories.
- Complete the recorded Timmes contact/redistribution confirmation (Now resolved and authorized) before treating
  that administrative release item as closed. Further metadata caching and the
  approved large-network measurements remain follow-up work, without changing
  the completed profile's numerical acceptance.

The sole continuing contributor plan is
[CudaReleaseStandard.md](../../../../../docs/development/CudaReleaseStandard.md).
This file records the final result and handoff, not a second implementation plan.
