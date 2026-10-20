# Hydrodynamic time integration

This directory combines hydrodynamic flux divergence and physical sources into
updates of a block's conserved state.

- [TimeIntegratorEuler.h](TimeIntegratorEuler.h),
  [TimeIntegratorRK2.h](TimeIntegratorRK2.h) and
  [TimeIntegratorRK3.h](TimeIntegratorRK3.h) bind the hydro stage sequences.
- [TimeIntegratorHelper.h](TimeIntegratorHelper.h) owns common cell divergence
  and host block operations.
- [GeometricSources.h](../curvilinear/GeometricSources.h) owns curvilinear momentum sources.
- [HydroGeometryBinding.h](HydroGeometryBinding.h) selects the chart and validates
  the actual boundary/domain independently of the optional gravity source.
- [IHydroSolver.h](IHydroSolver.h) and [HydroSolverImpl.h](HydroSolverImpl.h)
  connect the block interface to concrete EOS/flux policies.
  The interface uses borrowed declarations and does not import the configuration
  loader. The implementation binds the actual gravity source; the common helper
  deduces that source type, so diffusion can reuse divergence without including
  a concrete gravity solver. The mathematical body and stage order remain shared.

The shared [StageScheduler](../../driver/schedule/StageScheduler.h) owns stage weights
and scheduling. CUDA executors bind those stages to the common cell operations,
using the same integration coefficients. The driver and AMR modules own ghost
exchange, reflux correction and state publication.

The [gravity source binding](../../physics/gravity/HydroSourceBinding.h) owns
source discovery, actual external/self claims and gravitational work. Hydro
calls it at the original stage boundaries: flux divergence, source face work,
AMR registration, geometric sources, then source body work. Geometry does not
select gravity models. Disabling gravity leaves geometry, EOS/boundary checks
and AMR conservation active; an unknown active source is still rejected.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
