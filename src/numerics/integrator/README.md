# Hydrodynamic time integration

This directory combines hydrodynamic flux divergence and physical sources into
updates of a block's conserved state.

- [TimeIntegratorEuler.h](TimeIntegratorEuler.h),
  [TimeIntegratorRK2.h](TimeIntegratorRK2.h) and
  [TimeIntegratorRK3.h](TimeIntegratorRK3.h) bind the hydro stage sequences.
- [TimeIntegratorHelper.h](TimeIntegratorHelper.h) owns common cell divergence
  and host block operations.
- [GeometricSources.h](GeometricSources.h) owns curvilinear momentum sources.
- [IHydroSolver.h](IHydroSolver.h) and [HydroSolverImpl.h](HydroSolverImpl.h)
  connect the block interface to concrete EOS/flux policies.

The shared [StageScheduler](../../driver/schedule/StageScheduler.h) owns stage weights
and scheduling. CUDA executors bind those stages to the common cell operations,
using the same integration coefficients. The driver and AMR modules own ghost
exchange, reflux correction and state publication.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
