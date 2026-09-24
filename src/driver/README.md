# Simulation orchestration

[Driver.h](Driver.h) coordinates stages, regridding and output. A stage is one
intermediate update within a timestep. The driver tracks which state is ready
for the next stage or for output; each backend performs the requested work and
reports completion. The main entry points are:

- [ComputeBackend.h](runtime/ComputeBackend.h): backend execution contract;
- [StageScheduler.h](schedule/StageScheduler.h): shared hydro/RKL stage descriptors;
- [StateResidency.h](runtime/StateResidency.h): state versions, slots and completion;
- [DriverBurnPolicy.h](stages/DriverBurnPolicy.h): common cell preparation and accepted
  burn-energy handoff; [DriverBurn.h](stages/DriverBurn.h) owns host traversal;
- [ReductionSpec.h](schedule/ReductionSpec.h): shared reduction records and ordering;
- [dispatch](dispatch/README.md): registration, capability checks and typed dispatch.

Scheduling logic and publication rules must remain strictly shared. Backend adapters are exclusively responsible for supplying execution and synchronization, while the underlying mathematical formulas stay precisely within their respective numerical or physical owners. For further details on remaining helper responsibilities, refer to the [ownership map](../../docs/development/ImplementationOwnership.md), and consult [Validation](../../validation/README.md) for the records of accepted behavior.

The production composition is `B(dt/2) D(dt/2) H(dt) D(dt/2) B(dt/2)`;
gravity prepares fields at the required hydro stages. Representative
[HLLC/MUSCL/RK2 + RKL2 + BD + MG + AMR runs](../../simulation/SNIaCoupled/README.md)
use this driver. Policy registration and shared formulas do not qualify every
physical combination; the [Reference](../../docs/Reference.md#combining-methods-and-physics)
keeps the material, geometry, build and validation boundaries together.
