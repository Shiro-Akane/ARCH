# P1 CPU refactor checkpoint — 2026-09-21

Candidate implementation: `95b858fc9f97147a8bced5146dd229dcc3d08df4`.
Baseline: GUI Core `d98f6f6e853ccb23eaa916ab1d20356019087622`, descending from
main `01cc4f723e674d47fe23850e7c0fef221e92e98c`.
Scope: preserve existing CPU behavior while extracting Driver responsibilities
and adding domain preparation / scalar identity contracts. This is not a
self-gravity, CUDA, sparse-provider or performance acceptance record.

## Results

| Check | Result / evidence |
|---|---|
| CPU Debug build; GCC 13, Ninja, OpenMP ON, CUDA OFF, KLU OFF | Pass, [build.log](build.log). Final incremental build 46.884 s; peak owned RSS 1,968,760 KiB; no swap growth. These numbers are build observations, not before/after performance claims. |
| Five affected/new core CTests | 5/5, [ctest.log](ctest.log). |
| Architecture and header dependency audit | Clean candidate source snapshot passes, [architecture.log](architecture.log). |
| Architecture audit tooling controls | 102/102, [architecture-tests.log](architecture-tests.log). |
| Ten CPU baseline/candidate cases | 40 comparisons at steps 0, 2, 4, 5; all field errors zero, [parity.log](parity.log), [evidence.json](evidence.json). |
| Restart with old checkpoints | Four groups: RKL2 and burn/ENUC, each with intermediate and terminal sources. Baseline/candidate restored endpoints and candidate uninterrupted endpoints agree. |

Cases include uniform external gravity with Euler/RK2/RK3; no gravity on mixed
AMR with the same methods; external gravity with AMR/RK2; external gravity with
AMR/RK3 plus RKL1/RKL2; and Helmholtz/aprox13 burn with ENUC refinement. Every
case advances five steps. Runtime uses two OpenMP threads on both sides.
The burn case actually changes leaf count from 5 to 8; mixed AMR reaches levels
0 and 1. The report includes topology, native composition, ENUC, controller
values, continuation phase and output counters, not just density norms.

Field, ENUC and burn-limiter tolerances are zero. Checkpoint time/controller
comparison uses the existing comparator's temporal rules. Terminal-source
restart uses its explicit output-index adjustment; intermediate sources retain
the existing continuation counters. This is short one-dimensional regression
coverage and does not establish general numerical convergence or speedup.

## Identity and inherited evidence

The baseline executable was copied from the GUI worktree's existing Debug CPU
build before running; its tracked code was at the baseline above. Existing
hydrated data/archived result changes in that worktree were not imported.
Both programs read the same canonical inputs and current EOS table. Binary,
source, input and dependency hashes are retained in `evidence.json`.

The existing `arch_cuda_single_level_validation` binary was used only for
Host HDF5 comparison and metrics. Its identity is recorded separately; it was
not rebuilt, and no CUDA solver/device validation was run. Failed attempts to
request its CUDA-only build target in the CPU configuration produced no build
or CUDA execution; the bounded build then selected only available CPU targets.

GUI's previously reported 18/18 CPU checks were not repeated, per user request;
see [GUI session handoff](../../../../src/api/PREVIEW_SESSION_HANDOFF.md).
CUDA compilation, runtime parity, sanitizer and performance gates are explicitly
deferred by the user's current instruction. KLU was disabled for this P1 build;
no new sparse provider was implemented or qualified.

The old audit initially reported two GUI-integration false positives in a clean
snapshot: a changed strict-Boolean exception marker and a parameter default
mistaken for hidden CPU fallback. P1 updates these narrow rules with positive
and negative controls. Direct auditing of the working folder also finds build
dependencies and other worktrees; that scope issue remains open. Final audit
and its repository-tree test ran on 3,252 tracked/candidate code, build-template
and parameter files copied into a clean temporary tree, including every new P1
source. No include or authority failures remained.

## Reproduction

Use the two frozen revisions and equivalent Debug CPU configurations. Build
the candidate's `ARCH`, `arch_shared_stage_scheduler`,
`arch_gravity_stage_contract`, `arch_compute_backend`, `arch_state_residency`
and `arch_topology_transaction` targets. Configure with:

```sh
cmake -S . -B build/selfgravity-p1 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DARCH_ENABLE_CUDA=OFF -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_OPENMP=ON
ctest --test-dir build/selfgravity-p1 --output-on-failure \
  -R '^(gravity_stage_contract|shared_stage_scheduler|state_residency|compute_backend|topology_transaction)$'
```

The local run additionally reused the existing HighFive source directory with
`FETCHCONTENT_SOURCE_DIR_HIGHFIVE` and set `ARCH_RUNTIME_OUTPUT_DIRECTORY` to
`build/selfgravity-p1/bin`; no fresh dependency download was required.
Reuse an existing compatible project checkpoint comparator as the explicit
`--checkpoint-validator` argument; the current CPU-only configuration does not
build that CUDA-named utility target.

```sh
python3 validation/gravity/p1_cpu_refactor.py \
  --baseline /absolute/path/to/baseline/ARCH \
  --candidate /absolute/path/to/candidate/ARCH \
  --checkpoint-validator /absolute/path/to/arch_cuda_single_level_validation \
  --output /absolute/path/to/new-empty-results
```

The recipe reuses canonical parameter overrides, process/log handling, HDF5
comparison and restart helpers. Checkpoints and raw runtime logs remain local
under `build/selfgravity-p1/parity`; this package keeps the compact report,
hashes, topology/metadata and pass logs rather than duplicating HDF5 binaries.

For architecture checks, export the frozen candidate into a clean temporary
directory (without build artifacts/other worktrees), then run there:

```sh
python3 tools/audit_architecture.py .
python3 -m unittest discover -s tests/tooling -p test_audit_architecture.py
```

The implementation boundaries and next-stage obligations are recorded in the
[P1 handoff](../../../../docs/development/SelfGravityP1Handoff.zh-CN.md).
