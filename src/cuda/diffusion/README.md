# Device diffusion traversal

[DiffusionKernels.cuh](DiffusionKernels.cuh) binds device state to the common
diffusion operators; [DiffusionSolver.cuh](DiffusionSolver.cuh) supplies device
execution helpers.

Thermal, species and viscous formulas remain in
[numerics/diffusion](../../numerics/diffusion/README.md). The
[runtime interface](../runtime/diffusion/README.md) owns launch binding and
connects to shared RKL scheduling. Do not duplicate coefficients, stability
criteria or geometric corrections in the backend.
