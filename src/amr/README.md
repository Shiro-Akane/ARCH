# Adaptive mesh refinement

This directory owns mesh control and the shared mathematics for moving fields
between mesh levels. Refined parent blocks are represented by their children;
coarsening restores the parent representation. Transfer, neighboring ghost cells
and coarse/fine flux correction must agree with that topology.

| Responsibility | Main entries |
| --- | --- |
| Topology and identity | [AmrTree.h](topology/AmrTree.h), [Morton.h](topology/Morton.h), [BlockHandle.h](topology/BlockHandle.h), [Block.h](storage/Block.h) |
| Mesh lifecycle | [AMRControl.h](AMRControl.h), [TopologyTransaction.h](topology/TopologyTransaction.h), [RegridExecutionPlan.h](transfer/RegridExecutionPlan.h), [MemoryPool.h](storage/MemoryPool.h) |
| Indicators and conservative migration | [RefinementIndicatorMath.h](refinement/RefinementIndicatorMath.h), [RegridTransferMath.h](transfer/RegridTransferMath.h) |
| Ghost reconstruction and restriction | [GhostExchange.h](exchange/GhostExchange.h), [LimitedLinearProlongation.h](transfer/LimitedLinearProlongation.h), [ConservativeRestriction.h](transfer/ConservativeRestriction.h) |
| Exchange plans | [ExchangePlan.h](exchange/ExchangePlan.h), [AmrTransferPlans.h](transfer/AmrTransferPlans.h), [CoarseFineCellPlan.h](exchange/CoarseFineCellPlan.h), [BoundaryPlan.h](exchange/BoundaryPlan.h), [CoordinateSeamPlan.h](exchange/CoordinateSeamPlan.h), [CoordinateSeamMath.h](exchange/CoordinateSeamMath.h) |
| Flux correction | [AmrFluxMath.h](flux/AmrFluxMath.h), [FluxRegister.h](flux/FluxRegister.h), [AmrFluxPlan.h](flux/AmrFluxPlan.h), [AmrFluxExecutionPlan.h](flux/AmrFluxExecutionPlan.h), [AMRFluxRegistering.h](flux/AMRFluxRegistering.h) |

Topology definition and Morton ordering remain strictly on the CPU. Meanwhile, CUDA [AMR kernels](../cuda/amr/README.md) rely on shared numerical helpers, and the [runtime AMR](../cuda/runtime/amr/README.md) handles the binding of plans, buffers, and completion logic. It is important to remember that ghost-cell admissibility and complete-family regrid conservation operate under distinct contracts; helpers with similar names are not automatically interchangeable. For complete details, consult the [AMR validation](../../validation/amr/README.md).
