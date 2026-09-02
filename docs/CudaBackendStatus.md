# CUDA Backend and GPU-AMR Handoff Status

Chinese translation: [CudaBackendStatus.zh-CN.md](CudaBackendStatus.zh-CN.md).
This file is the authoritative English status record.

## Purpose and acceptance language

This document records the handoff state of the CUDA and GPU-AMR work. It is a
status snapshot, not a production-readiness claim. The following labels are
deliberately distinct:

- **Verified** means that the stated build or test actually completed in the
  environment described below.
- **Implemented, validation pending** means that the source path exists and has
  been reviewed or partially compile-qualified, but it has not completed both a
  full CUDA link and execution on a real GPU for the required matrix.
- **Not implemented** means that no working provider/path exists. Parsing a
  setting or installing a third-party library does not change this label.

The architectural rule remains: ARCH owns one set of mathematical and physical
logic. CPU-only topology construction and shared math are reused by CUDA.
Backend-specific code is limited to necessities such as kernels, device-memory
ownership and pools, streams, events/fences, and external accelerator-library
handles. A second CUDA copy of Morton/topology, EOS, reconstruction, AMR flux,
or solver-selection mathematics is not an accepted direction.

## Snapshot environment

- The CPU build and test suite completed on the current working tree: **17/17
  tests passed**. One initial tooling test failed only because the Windows
  `TEMP` directory was read-only from WSL; rerunning with `TMPDIR`, `TEMP`, and
  `TMP` set to `/tmp` passed.
- CUDA 12.3 and NVCC are installed and CUDA configuration succeeds with GCC/G++
  12 as the host compiler and SM 86 as the selected architecture.
- On the final AMR restriction sources, the CUDA exchange and compact AMR-flux
  translation units both completed compile-only qualification with NVCC 12.3.
  This is syntax/object evidence, not an archive, link, or runtime result.
- This host cannot perform real-device validation: `/dev/dxg` is absent and
  `nvidia-smi` reports that GPU access is blocked by the operating system.
- A complete CUDA backend archive, complete application link, and real-GPU run
  are **not recorded as successful in this snapshot**. Several CUDA/host
  translation units compiled, but the resource-constrained build was stopped
  rather than risking another WSL failure.

## Completed and verified

### Shared architecture and fail-closed dispatch

- Backend selection uses the shared registry/factory/capability path. Requests
  are parsed first and materialized for a CPU or CUDA candidate before backend
  construction; unsupported combinations fail explicitly instead of silently
  falling back after state has been constructed.
- Binary configuration switches are case-insensitive strings, and the CPU and
  CUDA candidates consume the same resolved configuration.
- Linear-solver routing is case-insensitive. `Auto` selects DenseLU for at most
  30 isotopes; larger CPU and CUDA candidates resolve to SparseKLU and cuDSS,
  respectively. Explicit CPU+cuDSS and CUDA+SparseKLU combinations are rejected
  before backend construction. The present cuDSS route then fails closed
  because no provider exists.
- CPU regression coverage for dispatch/capabilities, AMR planning and shared
  math, checkpoint compatibility and SHA-256 provenance, EOS/table ownership,
  and the existing physical modules is included in the 17/17 passing suite.

### Shared AMR, restart, and physical logic

- AMR topology, Morton-index work, neighbor classification, regrid decisions,
  and migration plans remain CPU-authoritative. CUDA receives lowered plans;
  it does not implement an independent topology/mathematics stack.
- Coarse/fine interface predicates, limiter/math helpers, area/volume scaling,
  flux-register weighting, and checkpoint semantics are shared between CPU and
  CUDA call paths.
- The shared Host checkpoint schema is version 3. It carries ENUC and scientific
  provenance (resolved EOS, gamma or table digest, burn/network/NSE selection,
  ordered species metadata), while retaining legacy v1/v2 readers with their
  documented limitations.
- Existing retained CPU validation covers hydro, diffusion, external gravity,
  one-zone burn, AMR transfer/reflux conservation, tabular EOS, restart, and
  generated-network/KLU cases. See [validation/README.md](../validation/README.md)
  for the evidence boundaries; those CPU results are not CUDA results.

### Source organization and build controls

- The former large CUDA backend implementation was split by responsibility into
  core, factory, resource, store, hydro-control, microphysics-control, exchange,
  AMR-flux, diffusion, and burn units.
- EOS device ownership was split by species/utility and Helmholtz/Tabular3D/
  Tabular4D resources. Tabular burn instantiations were split by EOS family and
  built-in network (`aprox13`, `aprox19`, `aprox21`, `iso7`).
- CMake contains a serial heavy-compile pool, explicit phase ordering, and
  file-backed completion barriers to prevent the backend archive, solver
  dispatch, and application from overlapping unexpectedly under Ninja.
- CUDA compiler caching is enabled when `ccache` is available.

## Implemented, but full CUDA link and real-GPU validation are pending

The following items are present in source and participate in the intended
CUDA backend, but this snapshot does not claim production parity:

- Cartesian 1D/2D/3D hydro for the registered flux/reconstruction/integrator
  combinations, including Euler, RK2, and RK3 stage handling.
- Ideal, Helmholtz, Tabular3D, and Tabular4D EOS device ownership and dispatch.
- DenseLU burn routing for all 48 built-in combinations of four EOS families,
  four built-in networks, and three ODE solvers, including the existing NSE
  route where supported.
- Cartesian RKL1/RKL2 species diffusion.
- Same-level and mixed-level ghost exchange for Current, Next, and Scratch
  state slots. The GPU coarse/fine path performs device gather/average/scatter
  from CPU-lowered plans.
- PPM interface handling that deliberately falls back to shared MUSCL-MinMod at
  a coarse/fine face. This is a stability/correctness rule and does not claim
  third-order accuracy across AMR interfaces.
- Hydro Euler/RK2/RK3 compact surface-flux registration and reflux.
- RKL1/RKL2 per-stage compact reflux, including negative gamma and the compact
  RKL2 `F(Y0)` cache.
- Dynamic AMR with CPU-authoritative regrid and a transactional CUDA store:
  quiesce device work, stage topology/store/flux plans, upload migrated Current
  state including ghosts, publish the new generation, then retire old device
  resources.
- Plot/checkpoint output by materializing CUDA Current state through the shared
  Host writer, and restart by reading the same Host schema before constructing
  and uploading a CUDA store.

Required qualification is still missing for this group: complete clean CUDA
archive/application links, retained tests running on a real NVIDIA GPU, and
same-input CPU/CUDA quantitative comparisons. In particular, retain tests for
mixed-level Current/Next/Scratch exchange, dynamic regrid plus Hydro and RKL
reflux, all supported EOS/network/ODE combinations, uninterrupted-versus-split
restart, and CPU-to-CUDA/CUDA-to-CPU restart interoperability.

## Not implemented

- **cuDSS provider:** `cuDSS` is parseable and capability-routed, but there is no
  CUDA provider, CMake package binding, resource/descriptor lifetime owner,
  sparse device assembly/active-cell compaction, analysis cache, or solve/update
  state machine. Installing cuDSS alone does not define
  `ARCH_HAS_CUDSS_PROVIDER`, remove the >30-isotope gate, or enable execution.
- CUDA burn for generated custom networks and CUDA burn above 30 isotopes.
- CUDA SparseKLU (intentionally unsupported; KLU is the CPU provider).
- CUDA external gravity and non-Cartesian geometry. These combinations are
  rejected by the CUDA capability contract.
- Self-gravity and the Jeans refinement indicator are absent from both current
  backends. WENO5 is not registered for either backend.

## Known limitations and risks

### Build-memory risk

A Debug compile of the Tabular4D+aprox19 CUDA translation unit previously drove
an 8 GiB WSL guest to approximately **150 MiB available memory**. It was
intentionally interrupted before the guest was killed. The functional split,
serial heavy lane, reduced debug information for immutable owners, and phase
barriers reduce overlap, but they do not yet prove a bounded clean build.

For the handoff, correctness and a successful conservative build take priority
over `--parallel 6`. Use `--parallel 1` for heavy CUDA qualification until the
memory work is complete. The next optimization milestone has a hard acceptance
criterion: **a clean Debug CUDA build and link must complete reliably on a
16 GiB machine without OOM, WSL termination, or dependence on excessive swap**.
After that baseline passes, qualify `--parallel 6`; do not relabel serial success
as parallel-build success.

Likely next measurements/actions are per-TU peak RSS logging, comparison of
Debug versus RelWithDebInfo, inspection of template-instantiation duplication,
and further functional extraction of shared network/EOS machinery only where it
improves ownership and readability. Do not split files solely to increase their
count or fork shared mathematics into a CUDA-only implementation.

### Shared AMR limitations

- Coarse-to-fine ghost filling is currently piecewise constant. It can create a
  refinement signal at smooth block interfaces and eventually refine a periodic
  problem globally.
- Fine-to-coarse restriction now conserves species as
  `sum(V rho X) / sum(V rho)`. Host curvilinear exchange uses physical cell
  volumes, while the Cartesian-only CUDA path uses the equivalent unit weights.
  CPU tests and NVCC object compilation pass; real-device parity is still
  pending.
- Dynamic split-run equivalence with `refine_var = ENUC` needs end-to-end CPU
  v3 evidence and real-device CUDA evidence. Legacy v1/v2 checkpoints initialize
  ENUC to zero and cannot establish this property.

### Qualification and portability risks

- CUDA runtime behavior, numerical parity, race freedom, memory-pool lifetime,
  stream/fence ordering, regrid failure rollback, and device-memory peaks remain
  unverified without a real GPU.
- The current local configure uses CUDA 12.3, GCC/G++ 12, and SM 86. At least one
  additional supported GPU architecture and a Release-like configuration should
  be included before a portability claim.
- The Tabular3D/Tabular4D public burn dispatchers contain a manual built-in
  `NetworkId` switch as a compilation boundary. It should remain a thin route,
  with physics/factory policy centralized; duplicated policy in those switches
  would be maintenance debt.

## Next-stage task list

1. Complete a clean CUDA backend archive and full ARCH application link with
   `--parallel 1`; fix compile/link errors before pursuing parallel speed.
2. Measure peak RSS for every heavy TU and satisfy the 16 GiB clean Debug build
   criterion. Then test and tune `--parallel 6` without weakening correctness or
   the one-facility architecture.
3. Run the focused CUDA targets on a real GPU, then execute same-input CPU/CUDA
   validation for hydro, EOS, burn, diffusion, ghost exchange, AMR/reflux,
   regrid, output, and restart. Commit metrics and hardware/toolchain metadata
   under `validation/`; do not record compile-only results as parity.
4. Add failure-injection tests for CUDA store publication/rollback and verify
   stream/fence/resource retirement under regrid and restart.
5. Replace piecewise-constant coarse-to-fine ghost filling with one shared,
   conservative limited-linear interpolation, then repeat CPU and CUDA AMR
   tests. Keep topology and stencil lowering CPU-authoritative.
6. Implement cuDSS only as a real CUDA provider: CMake discovery and build
   contract, RAII handles/descriptors, stream integration, reusable analysis,
   device sparse assembly/compaction, solve/update/convergence flow, and >30
   isotope/generated-network enablement. Keep request resolution shared.
7. Reassess external gravity/non-Cartesian CUDA support only after the supported
   Cartesian matrix is validated. Keep unsupported modes fail-closed meanwhile.

## Reproduction commands

These commands are a qualification recipe. The CPU sequence below has passed in
this snapshot. The CUDA sequences are deliberately labeled as pending and must
not be cited as successful until their complete output is retained.

### CPU regression (verified)

```bash
cmake -S . -B build-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=OFF
cmake --build build-cpu --parallel 2
TMPDIR=/tmp TEMP=/tmp TMP=/tmp \
  ctest --test-dir build-cpu --output-on-failure -j2
```

### Conservative CUDA compile/link qualification (pending)

```bash
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 \
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 \
  -DCMAKE_CUDA_ARCHITECTURES=86

/usr/bin/time -v \
  cmake --build build-cuda --target arch_cuda_backend --parallel 1
/usr/bin/time -v \
  cmake --build build-cuda --target ARCH --parallel 1
```

Record `free -h`, swap use, the `/usr/bin/time -v` peak RSS, compiler versions,
and the first failing command. Once serial compilation and the 16 GiB criterion
pass, repeat the clean build with `--parallel 6` and record it separately.

### Real-device validation (pending; unavailable on the snapshot host)

```bash
nvidia-smi
test -e /dev/dxg

cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build build-cuda --target arch_cuda_single_level_validation --parallel 1
TMPDIR=/tmp TEMP=/tmp TMP=/tmp \
  ctest --test-dir build-cuda --output-on-failure -R cuda
```

An absent/inaccessible GPU, a skipped test, successful CMake generation, or a
successful compile without the final link is not a real-device pass.
