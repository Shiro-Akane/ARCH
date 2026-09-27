# Hydro kernels and device policy adapters

Face assembly, state updates and stage traversal live in
[HydroFaceKernel.cuh](kernels/HydroFaceKernel.cuh), [HydroStateKernels.cuh](kernels/HydroStateKernels.cuh)
and [HydroStageKernels.cuh](kernels/HydroStageKernels.cuh). Flux, reconstruction and
integrator policy adapters select shared numerical leaves. Geometric sources
and EOS error transport have dedicated adapters, rather than copied formulas.

[Boundary.cuh](boundary/Boundary.cuh), [BoundaryPlan.h](boundary/BoundaryPlan.h) and
[ExchangeKernels.cuh](boundary/ExchangeKernels.cuh) handle device boundary work.
[GridGeometryAdapter.cuh](GridGeometryAdapter.cuh) binds the common geometry view.

All host-side control logic and typed EOS instantiation mechanics reside in [runtime/hydro](../runtime/hydro/README.md). Any mathematical changes must be directed to either [numerics](../../numerics/README.md) or [physics](../../physics/README.md).
