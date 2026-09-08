# Face reconstruction

This directory reconstructs hydrodynamic and species states at cell faces.

- [Reconstruction.h](Reconstruction.h) owns the reconstruction policies.
- [Limiters.h](Limiters.h) owns their slope-limiter functions.
- [AMRInterfaceStencil.h](AMRInterfaceStencil.h) owns the backend-neutral
  coarse/fine stencil predicate.
- [AMRInterfaceReconstruction.h](AMRInterfaceReconstruction.h) binds host grid
  access to the shared AMR-interface reconstruction choice.

Both CPU traversal loops and CUDA kernels feed neighboring cell samples into the exact same reconstruction and limiter mathematics. Coarse/fine ghost-cell transfers and regrid prolongations are treated as distinct AMR operations; as such, they are maintained strictly within the AMR module rather than being duplicated here.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
