# Diffusion operators and integration

This directory advances viscous, thermal and species diffusion on the existing
grid and AMR hierarchy.

- [DiffFlux.h](DiffFlux.h) owns the shared face/cell operators, geometric terms,
  face thermodynamics and stability estimates.
- [NewtonianViscousStress.h](NewtonianViscousStress.h) owns the sole three-dimensional
  Stokes constitutive law and physical frame connections.
  [CurvilinearViscousStress.h](../curvilinear/CurvilinearViscousStress.h) pairs polar/3D
  cylindrical metric gradients, face traction, geometry and work with their row bounds.
- [DiffFunction.h](DiffFunction.h) and [DiffFunction.cpp](DiffFunction.cpp) own
  RKL stage selection and recurrence coefficients.
- [DiffusionAMRStages.h](DiffusionAMRStages.h) owns common stage updates;
  [RKL1TimeIntegrator.h](RKL1TimeIntegrator.h) and
  [RKL2TimeIntegrator.h](RKL2TimeIntegrator.h) bind the host integration paths.
- [DiffusionTypes.h](DiffusionTypes.h) provides the shared records;
  [DiffDispatch.h](DiffDispatch.h) binds the resolved diffusion-policy ID to execution.

CUDA readers and kernels manage state access, launches and storage while
consuming these same operators and stage descriptors. Transport material models
remain in [physics/diffusionCoe](../../physics/diffusionCoe/README.md), and
[GridMetrics](../curvilinear/GridMetrics.h) supplies physical measures.
Runtime owns completed-state EOS acceptance; this operator defines neither a
second EOS gate authority nor an alternative constitutive law.

See the [Reference](../../../docs/Reference.md),
[diffusion validation](../../../validation/diffusion/README.md) and
[AMR validation](../../../validation/amr/README.md).

An operator being enabled does not establish a nonzero material coefficient.
The current Helmholtz stellar closure supplies thermal conduction, with zero
viscosity and species diffusivity; the corresponding flags cannot extend that
model. Constant-coefficient nonstellar tests qualify their own closures. See
[combination rules](../../../docs/Reference.md#combining-methods-and-physics)
before extending a coupled capability claim.
