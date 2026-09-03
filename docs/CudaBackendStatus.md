# CUDA Backend and GPU-AMR Qualification Status

Chinese translation: [CudaBackendStatus.zh-CN.md](CudaBackendStatus.zh-CN.md).
The English file is authoritative.

Status date: 2026-09-03. This tree is based on the GPU-AMR handoff commit
`21d6b2c` and includes the completion and qualification changes recorded below.

## Qualification summary

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
- The first full 65-test CUDA CTest run passed 52 tests. The remaining 13
  large-network tests failed at allocation with `out of memory` while two
  unrelated workloads occupied 9.4 GiB of the 20 GiB vGPU. They are a pending
  resource-isolated rerun, not recorded as passes.
- `compute-sanitizer` memcheck and racecheck cannot instrument this NVIDIA vGPU:
  the driver reports that GPU debugging features are disabled. The attempted
  logs are retained and sanitizer cleanliness is not claimed.

Machine-readable evidence and the exact build/test records are in
[`validation/amr/results/h100-sm90-20260903/`](../validation/amr/results/h100-sm90-20260903/).

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
- Checkpoint schema v3 is shared by both backends and records ENUC, EOS/table
  SHA-256, burn/network/NSE state, and species metadata. CUDA materializes
  `Current` before invoking the Host writer.

## Verified environment and build envelope

| Item | Recorded value |
| --- | --- |
| GPU | NVIDIA H100-20C, compute capability 9.0, 20,480 MiB |
| Driver / CUDA toolkit | 570.133.20 / 12.8.93 |
| Host compiler | GCC 11.4.0 (`g++-12` was unavailable on this host) |
| Build tools | CMake 4.4.0, Ninja 1.13.2 |
| Memory boundary | systemd user scope, `MemoryMax=16G`, `MemorySwapMax=0` |
| CUDA archive | 174,611,782 bytes; SHA-256 `ea789c913caa1091433fd0c82ad340ee0679dc32ee1e5eb68d390ef00a6eedf8` |
| `ARCH` executable | 202,176,184 bytes; SHA-256 `d42711fa4a19357713be49df77cd3ab714faf4ccfe89c5d9bb16efa09231490b` |

The serial clean archive build completed in `1:11:37` with maximum recorded RSS
`4,466,132 KiB`; the complete executable link completed in `1:54.77` with
`1,206,568 KiB`; all CUDA test targets compiled in `20:31.74` with
`4,807,348 KiB`. All three records report zero swaps. One-second cgroup samples
attributed to active CUDA translation units are retained in
`tu-memory-samples.csv`; they include build-scope overhead and must not be read
as isolated compiler-process RSS.

The requested `--parallel 1` acceptance baseline is complete. A clean
`--parallel 6` run is a throughput optimization, not part of the established
memory-safe baseline, and is not claimed here.

## Real-GPU validation matrix

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

1. Rerun the 13 large aprox19/aprox21/NSE tests, then the full 65-test suite,
   after the unrelated GPU jobs release enough device memory.
2. Run memcheck and racecheck on a bare-metal or vGPU profile that exposes CUDA
   debugging. The current H100 vGPU cannot satisfy this requirement.
3. Record a clean `--parallel 6` build only if parallel-build throughput is
   needed; it must not weaken the 16 GiB / zero-swap acceptance envelope.

These open items are qualification boundaries. They do not change the AMR
mathematics or authorize bypasses, reduced networks, fake validators, or
permissive tolerances.

## Deliberately out of scope

The following are later CUDA capability expansions and do not block Cartesian
GPU-AMR completion: a cuDSS provider, CUDA burn networks above 30 isotopes,
generated custom CUDA networks, CUDA external gravity, CUDA cylindrical or
spherical geometry, self-gravity/Jeans refinement, and WENO5 registration.

## Reproduction

~~~bash
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_ARCHITECTURES=90

cmake --build build-cuda --target arch_cuda_backend --parallel 1
cmake --build build-cuda --target ARCH --parallel 1

python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json \
  --arch ./bin/ARCH \
  --checkpoint-validator ./build-cuda/arch_cuda_single_level_validation \
  --source-root . \
  --output-root /tmp/arch-gpu-amr-validation
~~~
