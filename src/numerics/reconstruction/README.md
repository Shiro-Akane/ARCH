# Face reconstruction

This directory reconstructs hydrodynamic and species states at cell faces.

- [Reconstruction.h](Reconstruction.h) owns the reconstruction policies.
- [Limiters.h](Limiters.h) owns their slope-limiter functions.
- [AMRInterfaceStencil.h](AMRInterfaceStencil.h) owns the backend-neutral
  coarse/fine stencil predicate.
- [AMRInterfaceReconstruction.h](AMRInterfaceReconstruction.h) binds host grid
  access to the shared AMR-interface reconstruction choice.

CPU loops and CUDA kernels supply neighbouring samples to the same
reconstruction and limiter mathematics. Coarse/fine ghost transfer and regrid
prolongation are distinct AMR operations, maintained in the AMR module rather
than copied here.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
