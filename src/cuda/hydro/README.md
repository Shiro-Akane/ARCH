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

[HydroBatchKernels.cuh](kernels/HydroBatchKernels.cuh) maps independent AMR blocks
to the second launch dimension for stages and CFL work. It reuses the scalar CFL
candidate and ordered reduction leaves. Required mean-state pressure and sound
speed are recomputed once for each stage input and shared by its face limiters;
unused corner ghosts are excluded. These arrays are scratch, not evolving or
checkpointed thermodynamic state. Wide-species arenas retain single-block waves
to prevent aliasing. Cheap ideal-gas queries keep their direct path.

A Helm face query may also borrow those required mean results when its complete
`rho,e,X` input matches. Changed reconstructed inputs use the original shared
EOS, including optional-probe and required-failure behavior. The binding lives
inside one face traversal and never carries values across RK stages or regrids.

The HLLC policy also receives the shared `MeanThermoView`; CUDA supplies resident
pointers after the stage mean kernel completes in stream order. The identical
conserved-state shortcut therefore uses the same matching and flux code as CPU,
including cases where intermediate directional energy recovery rounds differently.
