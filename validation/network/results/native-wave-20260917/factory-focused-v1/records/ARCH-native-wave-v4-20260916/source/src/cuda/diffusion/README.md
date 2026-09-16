# Device diffusion traversal

[DiffusionKernels.cuh](DiffusionKernels.cuh) binds device state to the common
diffusion operators; [DiffusionSolver.cuh](DiffusionSolver.cuh) supplies device
execution helpers.

All fundamental thermal, species, and viscous formulas remain correctly located in [numerics/diffusion](../../numerics/diffusion/README.md). The associated [runtime interface](../runtime/diffusion/README.md) takes full ownership of launch binding and connects seamlessly to the shared RKL scheduling system. You must absolutely refrain from duplicating numerical coefficients, stability criteria, or any geometric corrections within this backend.
