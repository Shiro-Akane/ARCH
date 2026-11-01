# Rayleigh–Taylor replay inputs

These four two-dimensional Cartesian inputs test ideal-gas hydrodynamics,
PPM, HLLC, RK3 and external gravity on an evolving AMR mesh:

| Input | Diffusion |
| --- | --- |
| [plain.par](plain.par) | None |
| [species.par](species.par) | Species |
| [thermal.par](thermal.par) | Thermal |
| [coupled.par](coupled.par) | Thermal and species |

All four inputs disable burning and explicitly select `limiter=minmod`.
The physical initial state, boundaries and refinement criteria are recorded
in the parameter files; the variants differ in diffusion channels.

From the repository root, run a case with a locally built executable:

```bash
build-cpu/bin/ARCH RT validation/amr/inputs/rt/plain.par
```

Each input runs 256 steps and writes a checkpoint every 32 steps. Keep outputs
outside version control. For CUDA, copy the input, set `compute_backend = cuda`
and choose a separate output directory; do not change the physics to make a
comparison pass. A CUDA-enabled build is required for device execution.

For a restart check, retain the uninterrupted run, then use another copied input
with `restart = true`, `restart_file` pointing to its step-128 checkpoint
(`rt_chk_0004.h5`), and separate output and log directories. Keep the original
step limit, checkpoint cadence and physical parameters. Compare the terminal
checkpoint with the uninterrupted terminal checkpoint.

The existing `arch_cuda_single_level_validation` executable reads conservation
metrics with `--metrics CHECKPOINT --parameters ACTUAL_RUN.par`. Its comparison
modes and the repository-owned validation tools are described in
[tests/README.md](../../../../tests/README.md) and
[tools/README.md](../../../../tools/README.md). Numerical receipts must be
included when evaluating species mass changes.

The [CPU/CUDA continuation results](../../../backend/results/release-readiness/REL4CudaDeviceAndIntegration-20261019.json)
include these RT cases. Compare physical fields at the same time and account
for any different mesh topology through a common spatial representation.
