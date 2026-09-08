# CUDA backend review history and evidence

Contributor archive. This preserves interim and hardware-specific observations,
including superseded statements. For current features and limitations, read
[backend capabilities](../CudaBackendStatus.md). This archive is not acceptance
of the current release candidate.

Chinese translation: [CudaBackendEvidence.zh-CN.md](CudaBackendEvidence.zh-CN.md).
The English file is authoritative.

Status date: 2026-09-05. This tree includes review fixes on top of `2226456`.

The subsequent shared AMR/sparse/coordinate refactor is tracked separately in
[CudaRefactorSmoke.md](CudaRefactorSmoke.md). Its source changes supersede the
older capability boundaries below, but do not inherit the earlier test results.

## Current review fixes and acceptance boundary

The historical H100 results below are not final acceptance of this worktree.
The three production AMR/restart JSON reports identify executable `d42711fa…`,
whereas the historical final relink identifies `e903232e…`. They remain intact
as historical evidence and must not be relabeled as results of the new code.

- `arch_build_contract` now owns strict floating-point compile **and link**
  semantics for ordinary C++ and CUDA host/device compilation. Reassociation,
  implicit FMA contraction and flush-to-zero are not permitted to invalidate
  the one shared compensated sum. Debug and Release use the same mathematics;
  there is no separate CPU or CUDA summation formula.
- `LimitedLinearProlongation.h` now also owns species reconstruction, sibling
  family classification, sum-then-subtract closure, and constant-composition
  fallback. Backends only bind memory and execute transfers. Invalid density
  rejects the complete plan before any destination is written on either backend.
- Immutable burn reference **data** was extracted from frozen main `8c76be8`,
  using the identified Helmholtz table. Twelve longer-duration network/solver
  routes independently check current CPU behavior; the CUDA parity tests use
  the same reference before comparing backends. No legacy physics copy or
  automatic baseline-update mechanism is introduced.
- Both runtime validators share `validation_provenance.py`. Final acceptance
  requires the same source, runtime input, build configuration, executable and
  checkpoint-validator identity, rechecked after execution. The aggregate
  `qualify_cuda_amr_evidence.py` gate rejects incomplete or mismatched evidence.

Local bounded verification and remaining work are recorded in
[`validation/amr/results/local-fixes-20260905/`](../../validation/amr/results/local-fixes-20260905/).
CPU Debug CTest passed 21/21; Debug GPU burn policy tests passed 16/16 with
unchanged parity budgets. Release focused CPU/GPU checks passed 5/5, and the
two mathematical GPU tests passed memcheck/racecheck in both configurations.
The complete runtime matrices and Release GPU burn rerun remain outstanding.
A focused kernel pass is not a complete ARCH, AMR/restart, sanitizer, or 16 GiB
long-running capacity qualification.

## Historical H100 qualification summary

- The CPU-authoritative topology / Host-lowered CUDA architecture is retained.
  CUDA-specific code is limited to kernels, device storage, streams, fences,
  resource retirement, and launch routing.
- A clean Debug CUDA backend archive and the complete `ARCH` executable build
  and link under an enforced 16 GiB memory limit with swap disabled.
- Ten production CPU/CUDA AMR cases, containing 27 comparison checkpoints,
  pass on an NVIDIA H100-20C (SM90). The maximum field-normalized CPU/CUDA
  difference is `9.99201e-16`.
- Smooth and ENUC-driven restart matrices both pass CPU-to-CPU, CUDA-to-CUDA,
  CPU-to-CUDA, CUDA-to-CPU, and uninterrupted CPU/CUDA comparison routes.
- The focused CUDA AMR tests pass with `CUDA_LAUNCH_BLOCKING=1`.
- The first full 65-test CUDA CTest run passed 52 tests under external GPU
  contention. The first resource-isolated retry passed 10/13 and exposed three
  deterministic burn CPU/CUDA precision defects. After introducing shared
  compensated `double` reduction and a measured route-specific budget, the
  burn-policy tests pass 16/16, the original focused set passes 13/13, the full
  CTest suite passes 65/65, and the tooling tests pass 72/72.
- `compute-sanitizer` memcheck and racecheck cannot instrument this NVIDIA vGPU:
  the driver reports that GPU debugging features are disabled. The attempted
  logs are retained and sanitizer cleanliness is not claimed.

Machine-readable evidence and the exact build/test records are in
[`validation/amr/results/h100-sm90-20260903/`](../../validation/amr/results/h100-sm90-20260903/).

## Completed implementation

### Shared AMR mathematics

- Mixed-level exchange supports `Current`, `Next`, and `Scratch` fields in 1D,
  2D, and 3D and on X, Y, and Z faces.
- Coarse-to-fine exchange uses one shared conservative limited-linear
  prolongation path. Species are reconstructed through `rho X`; fine-to-coarse
  restriction is conservative and uses physical cell volume on the Host path.
- PPM falls back to MUSCL-MinMod at coarse/fine faces. CPU and CUDA consume the
  same face classification and reconstruction mathematics.
- ENUC and the five conserved fluid fields participate in exchange and
  topology migration.

### Reflux and time integration

- Hydro reflux covers Euler, RK2, and RK3 with stage weights `1`,
  `1/2, 1/2`, and `1/6, 1/6, 2/3`.
- RKL1 and RKL2 register and reflux every stage, including negative `gamma`.
  RKL2 retains `F(Y0)` in a compact surface cache and does not reuse it across
  time steps.
- Flux registers are face-sized and registration happens before flux scratch
  is overwritten. Reflux is applied to `Current` after final slot rotation.

### Dynamic topology and lifetime safety

- Refine/derefine decisions, Morton topology, neighbors, and migration plans
  remain CPU-authoritative.
- CUDA publishes topology, device blocks, handles, and reflux plans as one
  staged generation after uploads quiesce. Failure keeps the old generation
  live; retired storage is released only after its fence.
- Failure-injection and stale-handle/store-lifecycle paths are covered by the
  CUDA tests.

### EOS, burn, network, and restart

- Free-energy interpolation and thermodynamic closure live in the shared
  `TabularFreeEnergyMath.h`; Host owns HDF5, derivative-table generation, and
  storage, while CUDA owns upload/view lifetime only.
- The 3D/4D tabular temperature iteration now tests the Newton increment
  against the current temperature scale. Direct and free-energy EOS paths use
  finite CPU/CUDA comparison tolerances without duplicating device formulas.
- Built-in burn/network routes use one registry and fail closed when a route is
  unavailable or not device-capable. External KLU remains CPU-only.
- Ye, Timmes RHS/Jacobian and temperature-energy terms, ODE energy closure, and
  NSE conservation totals now use one fixed-order compensated `double`
  reduction on Host and device. The short-step policy test retains its
  existing route/field-specific budgets (including aprox19 and aprox21);
  the historical H100 follow-up recalibrated aprox19 ROS4 to 125% of measured
  maxima. Those budgets are separate from the new frozen-main reference's
  independently fixed ODE-error and roundoff criteria. This review does not
  widen the existing CPU/CUDA parity budgets.
- Checkpoint schema v3 is shared by both backends and records ENUC, EOS/table
  SHA-256, burn/network/NSE state, and species metadata. CUDA materializes
  `Current` before invoking the Host writer.

## Historical verified environment and build envelope

| Item | Recorded value |
| --- | --- |
| GPU | NVIDIA H100-20C, compute capability 9.0, 20,480 MiB |
| Driver / CUDA toolkit | 570.133.20 / 12.8.93 |
| Host compiler | GCC 11.4.0 (`g++-12` was unavailable on this host) |
| Build tools | CMake 4.4.0, Ninja 1.13.2 |
| Memory boundary | systemd user scope, `MemoryMax=16G`, `MemorySwapMax=0` |
| CUDA archive | 176,691,142 bytes; SHA-256 `9fa494d579c5450265789b19a16e83c4c2ba380785980a4fef8a11a9f81a4eb8` |
| `ARCH` executable | 204,248,832 bytes; SHA-256 `e903232e298ee9ea2cc958a404fe370a305463c8008ec5969b4adb9430f4f8e2` |

The original serial clean archive build completed in `1:11:37` with maximum
recorded RSS `4,466,132 KiB`. The compensated-reduction backend rebuild took
`1:13:21` with `4,752,228 KiB`; the final incremental executable link took
`1:06.81` with `1,206,036 KiB`. All CUDA test targets originally compiled in
`20:31.74` with `4,807,348 KiB`. Every record reports zero swaps. One-second cgroup samples
attributed to active CUDA translation units are retained in
`tu-memory-samples.csv`; they include build-scope overhead and must not be read
as isolated compiler-process RSS.

The historical `--parallel 1` build baseline completed. A clean
`--parallel 6` run is a throughput optimization, not part of the established
memory-safe baseline, and is not claimed here.

## Production validation matrix and historical results

The production manifest covers:

- 1D Hydro AMR with Euler, RK2, and RK3;
- a multi-step refine/derefine topology cycle;
- diffusion AMR with RKL1 and RKL2 using 2, 3, and 5 stages;
- the RKL2 negative-`gamma` path and `F(Y0)` cache lifetime;
- 2D and 3D RK3 AMR runs;
- CPU/CUDA checkpoint parity and conservation at every selected step.

Separate CUDA tests cover all dimensions, face directions, coarse/fine sides,
and `Current` / `Next` / `Scratch` exchange. The production manifest and its
evidence are intentionally kept separate from synthetic kernel tests.

Restart qualification covers both SmoothAdvection and `refine_var = ENUC`.
Smooth restart routes are bitwise equal at the reported comparisons. The ENUC
matrix passes its declared scale-aware tolerance; its largest normalized field
and ENUC difference is `6.71411e-4`.

## Open qualification items

1. Rebuild final ARCH and its checkpoint validator separately in Debug and
   Release, rerun the complete CTest/AMR/restart matrices, and pass the aggregate
   identity/coverage gate. The old H100 reports cannot close this item.
2. Run full runtime memcheck and racecheck coverage. The H100 vGPU disabled
   debugging. Locally the user enabled Windows `GPUDebugger/EnableInterface`
   after the initial WDDM error; both focused compensation and AMR composition
   tests now pass memcheck/racecheck in Debug and Release (eight runs, no
   reported errors/leaks/hazards, no reboot required). This resolves the local
   interface blocker, not the complete runtime sanitizer qualification.
3. Qualify sustained representative AMR/burn/restart workloads within 16 GiB
   host RAM and the declared GPU memory budget. A serial build peak alone does
   not establish runtime capacity, deeper-level coverage, or convergence.
4. Record a clean `--parallel 6` build only if parallel-build throughput is
   needed; it must not weaken the 16 GiB / zero-swap acceptance envelope.

These open items are qualification boundaries. They do not change the AMR
mathematics or authorize bypasses, reduced networks, fake validators, or
permissive tolerances.

## Scope after the subsequent refactor

cuDSS, device-callable generated networks/large burn, device AMR migration, and
cylindrical/spherical execution are now part of the subsequent implementation
and smoke work, tracked in [CudaRefactorSmoke.md](CudaRefactorSmoke.md). They are
not covered by the historical qualification above. The 2026-09-06
[release refactor](CudaReleaseStandard.md) adds shared external-gravity CUDA
sources, corrected common geometry/CFL and an AMR roundoff policy; final
production qualification is still pending. Self-gravity/Jeans,
generated weak-rate table owners (including immutable tables), and WENO5
registration remain outside the implemented CUDA capability set.

## Reproduction

~~~bash
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=ON \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-cuda/bin"

cmake --build build-cuda --target arch_cuda_backend --parallel 1
cmake --build build-cuda --target ARCH arch_cuda_single_level_validation --parallel 1

python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json \
  --arch ./build-cuda/bin/ARCH \
  --checkpoint-validator ./build-cuda/arch_cuda_single_level_validation \
  --build-dir ./build-cuda \
  --source-root . \
  --output-root /tmp/arch-gpu-amr-validation
~~~

Use a separate build/output directory for Release. The full two-restart and
final aggregate-gate recipe is in [the AMR README](../../validation/amr/README.md).
