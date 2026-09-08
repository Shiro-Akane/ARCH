# Adaptive mesh refinement

This directory owns mesh control and the shared mathematics for moving fields
between mesh levels. Refined parent blocks are represented by their children;
coarsening restores the parent representation. Transfer, neighboring ghost cells
and coarse/fine flux correction must agree with that topology.

| Responsibility | Main entries |
| --- | --- |
| Topology and identity | [AmrTree.h](AmrTree.h), [Morton.h](Morton.h), [BlockHandle.h](BlockHandle.h), [Block.h](Block.h) |
| Mesh lifecycle | [AMRControl.h](AMRControl.h), [TopologyTransaction.h](TopologyTransaction.h), [RegridExecutionPlan.h](RegridExecutionPlan.h), [MemoryPool.h](MemoryPool.h) |
| Indicators and conservative migration | [RefinementIndicatorMath.h](RefinementIndicatorMath.h), [RegridTransferMath.h](RegridTransferMath.h) |
| Ghost reconstruction and restriction | [GhostExchange.h](GhostExchange.h), [LimitedLinearProlongation.h](LimitedLinearProlongation.h), [ConservativeRestriction.h](ConservativeRestriction.h) |
| Exchange plans | [ExchangePlan.h](ExchangePlan.h), [AmrTransferPlans.h](AmrTransferPlans.h), [CoarseFineCellPlan.h](CoarseFineCellPlan.h), [BoundaryPlan.h](BoundaryPlan.h) |
| Flux correction | [AmrFluxMath.h](AmrFluxMath.h), [FluxRegister.h](FluxRegister.h), [AmrFluxPlan.h](AmrFluxPlan.h), [AmrFluxExecutionPlan.h](AmrFluxExecutionPlan.h), [AMRFluxRegistering.h](AMRFluxRegistering.h) |

Topology definition and Morton ordering remain strictly on the CPU. Meanwhile, CUDA [AMR kernels](../cuda/amr/README.md) rely on shared numerical helpers, and the [runtime AMR](../cuda/runtime/amr/README.md) handles the binding of plans, buffers, and completion logic. It is important to remember that ghost-cell admissibility and complete-family regrid conservation operate under distinct contracts; helpers with similar names are not automatically interchangeable. For complete details, consult the [AMR validation](../../validation/amr/README.md).
