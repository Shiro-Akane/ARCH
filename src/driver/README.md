# Simulation orchestration

[Driver.h](Driver.h) coordinates stages, regridding and output. The main seams are:

- [ComputeBackend.h](ComputeBackend.h): backend execution contract;
- [StageScheduler.h](StageScheduler.h): shared hydro/RKL stage descriptors;
- [StateResidency.h](StateResidency.h): state versions, slots and completion;
- [DriverBurnPolicy.h](DriverBurnPolicy.h): common cell preparation and accepted
  burn-energy handoff; [DriverBurn.h](DriverBurn.h) owns host traversal;
- [ReductionSpec.h](ReductionSpec.h): shared reduction records and ordering;
- [dispatch](dispatch/README.md): registration, capability checks and typed dispatch.

Keep scheduling and publication rules shared. Backend adapters supply execution
and synchronization, while formulas stay in their numerical/physical owners.
The [ownership map](../../docs/development/ImplementationOwnership.md) links the
remaining helper responsibilities and [Validation](../../validation/README.md)
records the accepted behavior.
