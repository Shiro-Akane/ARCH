# Device AMR operations

- [RefinementIndicators.h](RefinementIndicators.h) and its `.cu` implementation
  evaluate shared indicators and reduce block summaries.
- [RegridMigration.h](RegridMigration.h) and its `.cu` implementation bind shared
  conservative transfer mathematics to device stores.
- [CoarseFineExchangeKernels.cuh](CoarseFineExchangeKernels.cuh) performs device
  ghost reconstruction and restriction.
- [AmrFluxSurfaceKernels.cuh](AmrFluxSurfaceKernels.cuh) and
  [AmrFluxSurfaceTypes.cuh](AmrFluxSurfaceTypes.cuh) implement device surface traversal.

Topology and formulas remain in the common [AMR module](../../amr/README.md).
Plan binding, migration transactions and completion live in
[runtime/amr](../runtime/amr/README.md). Keep kernel traversal separate from
mesh-tree decisions and test both through [AMR validation](../../../validation/amr/README.md).
