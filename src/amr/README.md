# Adaptive mesh refinement

This directory keeps mesh control and shared transfer mathematics together.
The following functional index avoids splitting a tightly connected public
header interface merely to reduce the file count.

| Responsibility | Main entries |
| --- | --- |
| Topology and identity | [AmrTree.h](AmrTree.h), [Morton.h](Morton.h), [BlockHandle.h](BlockHandle.h), [Block.h](Block.h) |
| Mesh lifecycle | [AMRControl.h](AMRControl.h), [TopologyTransaction.h](TopologyTransaction.h), [RegridExecutionPlan.h](RegridExecutionPlan.h), [MemoryPool.h](MemoryPool.h) |
| Indicators and conservative migration | [RefinementIndicatorMath.h](RefinementIndicatorMath.h), [RegridTransferMath.h](RegridTransferMath.h) |
| Ghost reconstruction and restriction | [GhostExchange.h](GhostExchange.h), [LimitedLinearProlongation.h](LimitedLinearProlongation.h), [ConservativeRestriction.h](ConservativeRestriction.h) |
| Exchange plans | [ExchangePlan.h](ExchangePlan.h), [AmrTransferPlans.h](AmrTransferPlans.h), [CoarseFineCellPlan.h](CoarseFineCellPlan.h), [BoundaryPlan.h](BoundaryPlan.h) |
| Flux correction | [AmrFluxMath.h](AmrFluxMath.h), [FluxRegister.h](FluxRegister.h), [AmrFluxPlan.h](AmrFluxPlan.h), [AmrFluxExecutionPlan.h](AmrFluxExecutionPlan.h), [AMRFluxRegistering.h](AMRFluxRegistering.h) |

Topology and Morton ordering stay on the CPU. CUDA [AMR kernels](../cuda/amr/README.md)
call the shared numerical helpers, while [runtime AMR](../cuda/runtime/amr/README.md)
binds plans, buffers and completion. Ghost-cell admissibility and complete-family
regrid conservation are different contracts; similarly named helpers are not
automatically interchangeable. See [AMR validation](../../validation/amr/README.md).
