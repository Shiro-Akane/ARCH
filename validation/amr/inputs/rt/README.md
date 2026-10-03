# Rayleigh–Taylor replay inputs

These four two-dimensional Cartesian inputs reproduce the reported AMR state
failure using ideal-gas hydrodynamics, PPM, HLLC, RK3 and external gravity:

| Input | Diffusion |
| --- | --- |
| [plain.par](plain.par) | None |
| [species.par](species.par) | Species |
| [thermal.par](thermal.par) | Thermal |
| [coupled.par](coupled.par) | Thermal and species |

The physics, boundary conditions and refinement criteria come from the four
files supplied on `codex/rt-amr-validation-notes-20261003` at `2ee826655`.
Only output paths, the step limit and checkpoint cadence have been adjusted.
The files explicitly disable burning, as in the reported experiment.

From the repository root, run a case with a locally built executable:

```bash
bin/ARCH RT validation/amr/inputs/rt/plain.par
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

The current repair evidence and data-admission decision are in the
[developer record](../../../../docs/development/RTAmrDataCorrectnessRepair.zh-CN.md).
Successful execution alone does not qualify a dataset. CPU/CUDA scientific
comparisons must use the same physical time; topology differences require an
appropriate spatial comparison rather than comparing file indices blindly.
