# Device AMR operations

- [RefinementIndicators.h](RefinementIndicators.h) and its `.cu` implementation
  evaluate shared indicators and reduce block summaries.
- [RegridMigration.h](RegridMigration.h) and its `.cu` implementation bind shared
  conservative transfer mathematics to device stores.
- [CoarseFineExchangeKernels.cuh](CoarseFineExchangeKernels.cuh) performs device
  ghost reconstruction and restriction.
- [AmrFluxSurfaceKernels.cuh](AmrFluxSurfaceKernels.cuh) and
  [AmrFluxSurfaceTypes.cuh](AmrFluxSurfaceTypes.cuh) implement device surface traversal.

Topology definitions and mathematical formulas remain securely within the common [AMR module](../../amr/README.md). Meanwhile, plan binding, migration transactions, and completion logic are maintained in [runtime/amr](../runtime/amr/README.md). You must always keep device kernel traversal completely separate from host mesh-tree decisions, and comprehensively test both using the [AMR validation](../../../validation/amr/README.md) framework.
