# Simulation orchestration

[Driver.h](Driver.h) coordinates stages, regridding and output. A stage is one
intermediate update within a timestep. The driver tracks which state is ready
for the next stage or for output; each backend performs the requested work and
reports completion. The main entry points are:

- [ComputeBackend.h](ComputeBackend.h): backend execution contract;
- [StageScheduler.h](StageScheduler.h): shared hydro/RKL stage descriptors;
- [StateResidency.h](StateResidency.h): state versions, slots and completion;
- [DriverBurnPolicy.h](DriverBurnPolicy.h): common cell preparation and accepted
  burn-energy handoff; [DriverBurn.h](DriverBurn.h) owns host traversal;
- [ReductionSpec.h](ReductionSpec.h): shared reduction records and ordering;
- [dispatch](dispatch/README.md): registration, capability checks and typed dispatch.

Scheduling logic and publication rules must remain strictly shared. Backend adapters are exclusively responsible for supplying execution and synchronization, while the underlying mathematical formulas stay precisely within their respective numerical or physical owners. For further details on remaining helper responsibilities, refer to the [ownership map](../../docs/development/ImplementationOwnership.md), and consult [Validation](../../validation/README.md) for the records of accepted behavior.
