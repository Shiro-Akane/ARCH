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
  [DiffDispatch.h](DiffDispatch.h) binds the resolved diffusion-policy ID to execution.

CUDA readers and kernels manage state access, kernel launches, and storage handling; however, they consume these exact same operators and stage descriptors. Transport material models are maintained in [physics/diffusionCoe](../../physics/diffusionCoe/README.md), and physical metric measures are provided by `GridMetrics`.

See the [Reference](../../../docs/Reference.md),
[diffusion validation](../../../validation/diffusion/README.md) and
[AMR validation](../../../validation/amr/README.md).

An operator being enabled does not establish a nonzero material coefficient.
The current Helmholtz stellar closure supplies thermal conduction, with zero
viscosity and species diffusivity; the corresponding flags cannot extend that
model. Constant-coefficient nonstellar tests qualify their own closures. See
[combination rules](../../../docs/Reference.md#combining-methods-and-physics)
before extending a coupled capability claim.
