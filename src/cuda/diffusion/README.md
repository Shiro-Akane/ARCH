# Device diffusion traversal

[DiffusionKernels.cuh](DiffusionKernels.cuh) binds device state to the common
diffusion operators; [DiffusionSolver.cuh](DiffusionSolver.cuh) supplies device
execution helpers.

[Numerics/diffusion](../../numerics/diffusion/README.md) owns thermal, species and
viscous formulas. The [runtime interface](../runtime/diffusion/README.md) binds
launches to the shared RKL schedule. Numerical coefficients, stability criteria
and geometric corrections remain in those shared owners.
