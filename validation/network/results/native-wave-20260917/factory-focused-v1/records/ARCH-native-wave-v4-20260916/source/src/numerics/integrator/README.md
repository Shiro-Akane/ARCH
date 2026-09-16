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

The shared [StageScheduler](../../driver/StageScheduler.h) comprehensively owns all stage weights and scheduling logic. CUDA executors simply bind to those stages and the common cell mathematics; they absolutely do not define separate integration coefficients. Furthermore, AMR exchange, reflux correction, and data publication strictly remain the responsibilities of the driver and AMR modules.

See the [Reference](../../../docs/Reference.md),
[hydro validation](../../../validation/hydro/README.md) and
[AMR validation](../../../validation/amr/README.md).
