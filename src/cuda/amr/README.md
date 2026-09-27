# Device AMR operations

- [RefinementIndicators.h](RefinementIndicators.h) and its `.cu` implementation
  evaluate shared indicators and reduce block summaries.
- [RegridMigration.h](RegridMigration.h) and its `.cu` implementation bind shared
  conservative transfer mathematics to device stores.
- [CoarseFineExchangeKernels.cuh](CoarseFineExchangeKernels.cuh) performs device
  ghost reconstruction and restriction.
- [AmrFluxSurfaceKernels.cuh](AmrFluxSurfaceKernels.cuh) and
  [AmrFluxSurfaceTypes.cuh](AmrFluxSurfaceTypes.cuh) implement device surface traversal.

The shared [AMR module](../../amr/README.md) owns topology and transfer formulas.
[Runtime/amr](../runtime/amr/README.md) binds plans, manages migration transactions
and verifies completion. Device traversal is separate from host mesh decisions;
[AMR validation](../../../validation/amr/README.md) covers their interaction.
