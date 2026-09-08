# Diffusion operators and integration

This directory advances viscous, thermal and species diffusion on the existing
grid and AMR hierarchy.

- [DiffFlux.h](DiffFlux.h) owns the shared face/cell operators, geometric terms,
  face thermodynamics and stability estimates.
- [DiffFunction.h](DiffFunction.h) and [DiffFunction.cpp](DiffFunction.cpp) own
  RKL stage selection and recurrence coefficients.
- [DiffusionAMRStages.h](DiffusionAMRStages.h) owns common stage updates;
  [RKL1TimeIntegrator.h](RKL1TimeIntegrator.h) and
  [RKL2TimeIntegrator.h](RKL2TimeIntegrator.h) bind the host integration paths.
- [DiffusionTypes.h](DiffusionTypes.h) provides the shared records;
  [DiffDispatch.h](DiffDispatch.h) connects configuration to execution.

CUDA readers and kernels provide state access, launch and storage handling;
they consume these same operators and stage descriptors. Transport material
models live in [physics/diffusionCoe](../../physics/diffusionCoe/README.md),
and physical measures come from GridMetrics.

See the [Reference](../../../docs/Reference.md),
[diffusion validation](../../../validation/diffusion/README.md) and
[AMR validation](../../../validation/amr/README.md).
