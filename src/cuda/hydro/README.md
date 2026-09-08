# Hydro kernels and device policy adapters

Face assembly, state updates and stage traversal live in
[HydroFaceKernel.cuh](HydroFaceKernel.cuh), [HydroStateKernels.cuh](HydroStateKernels.cuh)
and [HydroStageKernels.cuh](HydroStageKernels.cuh). Flux, reconstruction and
integrator policy adapters select shared numerical leaves. Geometric sources
and EOS error transport have dedicated adapters, rather than copied formulas.

[Boundary.cuh](Boundary.cuh), [BoundaryPlan.h](BoundaryPlan.h) and
[ExchangeKernels.cuh](ExchangeKernels.cuh) handle device boundary work.
[GridGeometryAdapter.cuh](GridGeometryAdapter.cuh) binds the common geometry view.

Host control and typed EOS instantiation are in
[runtime/hydro](../runtime/hydro/README.md). Mathematical changes belong in
[numerics](../../numerics/README.md) or [physics](../../physics/README.md).
