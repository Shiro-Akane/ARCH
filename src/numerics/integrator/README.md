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

The shared [StageScheduler](../../driver/StageScheduler.h) owns stage weights
and scheduling. CUDA executors bind those stages and common cell mathematics;
they do not define separate integration coefficients. AMR exchange, reflux and
publication remain driver/AMR responsibilities.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
