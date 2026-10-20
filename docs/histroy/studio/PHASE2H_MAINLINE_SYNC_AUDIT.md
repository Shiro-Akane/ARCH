# Phase 2H-A Mainline Sync Audit

Date: 2026-09-27. Target: ARCH_STUDIO_PHASE2H_MAINLINE_CAPABILITY_SYNC_TARGET.md.
Result at audit gate: PASS for the exact-tree strategy, before implementation.
Subsequent CPU/runtime gate: PASS; see PHASE2H_A_MAINLINE_SYNC_REPORT.md.

## Verified Git identities
- Studio HEAD / v0.16.0: e13b4bcd00fd763029a50c17b95c002d84cf780b
- Core UI authoritative ref: 502eadcb33a9e2d3c8bd079a10dbdc8af208ef10
- Its parent / synchronized main: 48f6d357d6b8085501d2012d70e923080bae3402
- Merge base: 7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866
- Left/right commits: 23 Studio / 128 upstream.
- Original Studio worktree: clean on studio/phase2g-desktop-launcher.
- Disposable audit: /home/arch/projects/ARCH-phase2h-sync-audit (detached baseline).
- No merge main, rebase, reset, stash, or single docs cherry-pick was performed.

## Ownership
Studio owns the complete studio/ subtree, including its Host, frontend, desktop,
tests, phase reports and ignore rules. Everything tracked outside studio/ follows the
authoritative upstream repository tree: scientific Core/API/include/simulation,
CMake, tests (including all Core tests/host and tests/api), tools, EOS_toolkit,
validation, examples, upstream docs, root policies/licenses and .github.
No separate Studio-owned tests/host subtree exists outside studio/.
Untracked build artifacts, local files and other worktrees are not synchronization inputs.

## Strategies
1. Fast-forward: impossible for this diverged history.
2. Merge main: prohibited and not attempted; unnecessary conflict resolution risks
   preserving obsolete cherry-picked paths.
3. Cherry-pick 502eadcb only: rejected; it changes only two API documentation files.
4. Selected: exact authoritative non-studio tree replacement on a descendant of
   v0.16.0, followed by a separate Studio build/path compatibility commit.
   This preserves every Studio ancestor without pretending upstream is a merge parent.
   The synchronization commit records the exact upstream ref.

## Disposable validation
Used git restore --source=<upstream> --staged --worktree -- . ':(exclude)studio'.
Compared every recursive ls-tree entry (path, mode, object ID), not only diff counts.
- Candidate tree: 548bf073a7444c537f174dac3f647d68bbe250ef
- Baseline and candidate studio tree: 125e04ea0f2a1b963960f1263aa9d60a4e2c9e03
- Authoritative non-studio entries: 18928; exact equality PASS.
- Unstaged difference from candidate index: empty.
- Upstream-deleted paths removed, including old rename sources: PASS by full-map equality.
- No local scientific resolutions or content rewrites.

The v0.16.0 worktree was not modified. After this audit report is recorded, create
studio/phase2h-mainline-capability-sync in a separate worktree at v0.16.0, repeat the
verified operation, commit only upstream paths, then make A-only Studio profile/path
changes. Preserve dependenciesComplete=false; no semantic migration until B.

## Scope of upstream differences
Rename-aware comparison: 647 additions, 280 modifications, 19 deletions,
270 detected renames. Git rename scores are heuristic; exact tree equality is authority.
Notable moves: cmake/project, cmake/dependencies, src/api/configuration/inspection/
preview/protocol/resources/session, src/core/config/files/problem, AMR subdirectories,
and simulation model categories. New include/ ownership follows upstream.
API directory itself is included in the same exact Core comparison.

### Deleted paths
- SECURITY.md
- SECURITY.zh-CN.md
- src/api/ParameterMetadata.h
- src/api/Progress.h
- src/api/ResourceEstimates.h
- src/api/StateSnapshot.h
- src/api/WorkerLimits.h
- src/core/InitialStateConversion.h
- src/core/StandardParameters.h
- src/cuda/hydro/CheckedHydroEos.cuh
- src/cuda/microphysics/CuDssSparseSolver.cpp
- src/cuda/microphysics/SparseBeNrBatch.cuh
- src/driver/DriverBurn.h
- src/physics/gravity/ExternalGravitySource.h
- src/physics/gravity/GravityNone.h
- tests/cuda/test_refinement_indicators.cpp
- tests/fixtures/RoeFluxReference.h
- tests/fixtures/amr_composition_test_cases.h
- tests/host/test_generated_nse.cpp

### Renamed/moved paths (complete detected list)

- R085 → cmake/CudaBackend.cmake → cmake/cuda/CudaBackend.cmake
- R097 → cmake/CudaBurnDenseRoutes.cmake → cmake/cuda/CudaBurnDenseRoutes.cmake
- R100 → cmake/CudaBurnNetworks.cmake → cmake/cuda/CudaBurnNetworks.cmake
- R089 → cmake/CudaBurnSparseRoutes.cmake → cmake/cuda/CudaBurnSparseRoutes.cmake
- R100 → cmake/CudaCodeImages.cmake → cmake/cuda/CudaCodeImages.cmake
- R097 → cmake/Dependencies.cmake → cmake/dependencies/Dependencies.cmake
- R064 → cmake/FindCuDSS.cmake → cmake/dependencies/FindCuDSS.cmake
- R082 → cmake/Application.cmake → cmake/project/Application.cmake
- R099 → cmake/BuildOptions.cmake → cmake/project/BuildOptions.cmake
- R100 → cmake/SelectIpoLinker.cmake → cmake/project/SelectIpoLinker.cmake
- R092 → cmake/CudaCustomDenseRoute.cu.in → cmake/templates/CudaCustomDenseRoute.cu.in
- R095 → docs/physics/AmrIndicatorNoiseReview.md → docs/development/archive/AmrIndicatorNoiseReview.md
- R094 → docs/development/CudaBackendEvidence.md → docs/development/archive/CudaBackendEvidence.md
- R094 → docs/development/CudaBackendEvidence.zh-CN.md → docs/development/archive/CudaBackendEvidence.zh-CN.md
- R097 → docs/development/CudaRefactorSmoke.md → docs/development/archive/CudaRefactorSmoke.md
- R095 → docs/physics/CurvilinearMetricReview.md → docs/development/archive/CurvilinearMetricReview.md
- R086 → docs/physics/DiffusionCoefficientAlignment.md → docs/development/archive/DiffusionCoefficientAlignment.md
- R086 → docs/physics/DiffusionCoefficientAlignment.zh-CN.md → docs/development/archive/DiffusionCoefficientAlignment.zh-CN.md
- R098 → src/amr/BoundaryPlan.h → src/amr/exchange/BoundaryPlan.h
- R097 → src/amr/CoarseFineCellPlan.h → src/amr/exchange/CoarseFineCellPlan.h
- R096 → src/amr/ExchangePlan.h → src/amr/exchange/ExchangePlan.h
- R080 → src/amr/GhostExchange.h → src/amr/exchange/GhostExchange.h
- R094 → src/amr/AMRFluxRegistering.h → src/amr/flux/AMRFluxRegistering.h
- R098 → src/amr/AmrFluxExecutionPlan.h → src/amr/flux/AmrFluxExecutionPlan.h
- R089 → src/amr/AmrFluxMath.h → src/amr/flux/AmrFluxMath.h
- R098 → src/amr/AmrFluxPlan.h → src/amr/flux/AmrFluxPlan.h
- R098 → src/amr/FluxRegister.h → src/amr/flux/FluxRegister.h
- R093 → src/amr/RefinementIndicatorMath.h → src/amr/refinement/RefinementIndicatorMath.h
- R073 → src/amr/RefinementThermodynamics.h → src/amr/refinement/RefinementThermodynamics.h
- R096 → src/amr/Block.h → src/amr/storage/Block.h
- R092 → src/amr/MemoryPool.h → src/amr/storage/MemoryPool.h
- R082 → src/amr/AmrDefines.h → src/amr/topology/AmrDefines.h
- R098 → src/amr/AmrTree.h → src/amr/topology/AmrTree.h
- R094 → src/amr/BlockHandle.h → src/amr/topology/BlockHandle.h
- R091 → src/amr/Morton.h → src/amr/topology/Morton.h
- R096 → src/amr/TopologyTransaction.h → src/amr/topology/TopologyTransaction.h
- R097 → src/amr/AmrTransferPlans.h → src/amr/transfer/AmrTransferPlans.h
- R086 → src/amr/ConservativeRestriction.h → src/amr/transfer/ConservativeRestriction.h
- R074 → src/amr/LimitedLinearProlongation.h → src/amr/transfer/LimitedLinearProlongation.h
- R094 → src/amr/RegridExecutionPlan.h → src/amr/transfer/RegridExecutionPlan.h
- R096 → src/amr/RegridTransferMath.h → src/amr/transfer/RegridTransferMath.h
- R086 → src/api/Configuration.cpp → src/api/configuration/Configuration.cpp
- R088 → src/api/ParameterMetadata.cpp → src/api/configuration/ParameterMetadata.cpp
- R083 → src/api/ParameterPresentation.h → src/api/configuration/ParameterPresentation.h
- R081 → src/api/PresentationMetadata.cpp → src/api/configuration/PresentationMetadata.cpp
- R069 → src/api/ValueDomain.h → src/api/configuration/ValueDomain.h
- R088 → src/api/CaseInspection.cpp → src/api/inspection/CaseInspection.cpp
- R064 → src/api/CaseUnitEvidence.cpp → src/api/inspection/CaseUnitEvidence.cpp
- R066 → src/api/Discovery.cpp → src/api/inspection/Discovery.cpp
- R090 → src/api/InitialMesh.h → src/api/preview/InitialMesh.h
- R091 → src/api/Preview.cpp → src/api/preview/Preview.cpp
- R090 → src/api/PreviewCommand.cpp → src/api/preview/PreviewCommand.cpp
- R078 → src/api/ResourceEstimates.cpp → src/api/preview/ResourceEstimates.cpp
- R080 → src/api/Sampling.h → src/api/preview/Sampling.h
- R085 → src/api/StateSnapshot.cpp → src/api/preview/StateSnapshot.cpp
- R089 → src/api/Json.h → src/api/protocol/Json.h
- R078 → src/api/LogCapture.h → src/api/protocol/LogCapture.h
- R071 → src/api/RequestInput.h → src/api/protocol/RequestInput.h
- R080 → src/api/Response.h → src/api/protocol/Response.h
- R081 → src/api/WorkerLimits.cpp → src/api/resources/WorkerLimits.cpp
- R077 → src/api/InitialSampleCache.h → src/api/session/InitialSampleCache.h
- R091 → src/api/PreviewSession.cpp → src/api/session/PreviewSession.cpp
- R081 → src/api/SessionInput.h → src/api/session/SessionInput.h
- R088 → src/core/RuntimeParams.h → src/core/config/RuntimeParams.h
- R096 → src/core/UserInterface.h → src/core/config/UserInterface.h
- R096 → src/core/FileFingerprint.cpp → src/core/files/FileFingerprint.cpp
- R077 → src/core/FileFingerprint.h → src/core/files/FileFingerprint.h
- R066 → src/core/InspectionSources.h → src/core/files/InspectionSources.h
- R070 → src/core/VerifiedFileCache.h → src/core/files/VerifiedFileCache.h
- R074 → src/core/ProblemHelper.cpp → src/core/problem/ProblemHelper.cpp
- R100 → src/core/ProblemHelper.h → src/core/problem/ProblemHelper.h
- R092 → src/core/ProblemRegistry.h → src/core/problem/ProblemRegistry.h
- R060 → src/cuda/hydro/Boundary.cuh → src/cuda/hydro/boundary/Boundary.cuh
- R066 → src/cuda/hydro/BoundaryPlan.h → src/cuda/hydro/boundary/BoundaryPlan.h
- R095 → src/cuda/hydro/ExchangeKernels.cuh → src/cuda/hydro/boundary/ExchangeKernels.cuh
- R078 → src/cuda/hydro/HydroFaceKernel.cuh → src/cuda/hydro/kernels/HydroFaceKernel.cuh
- R081 → src/cuda/hydro/HydroSourceKernels.cuh → src/cuda/hydro/kernels/HydroSourceKernels.cuh
- R066 → src/cuda/hydro/HydroStageKernels.cuh → src/cuda/hydro/kernels/HydroStageKernels.cuh
- R088 → src/cuda/hydro/HydroStateKernels.cuh → src/cuda/hydro/kernels/HydroStateKernels.cuh
- R076 → src/cuda/hydro/HydroFluxPolicies.cuh → src/cuda/hydro/policies/HydroFluxPolicies.cuh
- R093 → src/cuda/hydro/HydroIntegratorPolicies.cuh → src/cuda/hydro/policies/HydroIntegratorPolicies.cuh
- R079 → src/cuda/hydro/HydroReconstructionPolicies.cuh → src/cuda/hydro/policies/HydroReconstructionPolicies.cuh
- R088 → src/cuda/microphysics/SparseBurnCells.cuh → src/cuda/microphysics/burn/SparseBurnCells.cuh
- R094 → src/cuda/microphysics/SparseOdeBatch.cuh → src/cuda/microphysics/burn/SparseOdeBatch.cuh
- R094 → src/cuda/microphysics/device_eos_owner_utils.cpp → src/cuda/microphysics/eos/device_eos_owner_utils.cpp
- R091 → src/cuda/microphysics/device_eos_owner_utils.h → src/cuda/microphysics/eos/device_eos_owner_utils.h
- R050 → src/cuda/microphysics/helm_eos_loader.h → src/cuda/microphysics/eos/helm_eos_loader.h
- R094 → src/cuda/microphysics/helm_eos_device_owner.cpp → src/cuda/microphysics/eos/owners/helm_eos_device_owner.cpp
- R085 → src/cuda/microphysics/helm_eos_device_owner.h → src/cuda/microphysics/eos/owners/helm_eos_device_owner.h
- R092 → src/cuda/microphysics/tabular3_eos_device_owner.cpp → src/cuda/microphysics/eos/owners/tabular3_eos_device_owner.cpp
- R084 → src/cuda/microphysics/tabular3_eos_device_owner.h → src/cuda/microphysics/eos/owners/tabular3_eos_device_owner.h
- R056 → src/cuda/microphysics/tabular4_eos_device_owner.cpp → src/cuda/microphysics/eos/owners/tabular4_eos_device_owner.cpp
- R083 → src/cuda/microphysics/tabular4_eos_device_owner.h → src/cuda/microphysics/eos/owners/tabular4_eos_device_owner.h
- R078 → src/cuda/microphysics/CuDssSparseSolver.h → src/cuda/microphysics/linalg/CuDssSparseSolver.h
- R067 → src/cuda/microphysics/SparseEquilibration.cu → src/cuda/microphysics/linalg/SparseEquilibration.cu
- R061 → src/cuda/microphysics/SparseEquilibration.h → src/cuda/microphysics/linalg/SparseEquilibration.h
- R093 → src/cuda/microphysics/device_network_owner.h → src/cuda/microphysics/network/device_network_owner.h
- R091 → src/cuda/microphysics/device_species_owner.cpp → src/cuda/microphysics/network/device_species_owner.cpp
- R089 → src/cuda/microphysics/device_species_owner.h → src/cuda/microphysics/network/device_species_owner.h
- R091 → src/cuda/runtime/burn/CudaBackendBurnDenseRoutes.h → src/cuda/runtime/burn/dense/CudaBackendBurnDenseRoutes.h
- R072 → src/cuda/runtime/burn/CudaBackendBurnImpl.cuh → src/cuda/runtime/burn/dense/CudaBackendBurnImpl.cuh
- R073 → src/cuda/runtime/burn/CudaBackendBurnNetworkRouteImpl.cuh → src/cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRouteImpl.cuh
- R092 → src/cuda/runtime/burn/CudaBackendBurnNetworkRoutes.h → src/cuda/runtime/burn/dispatch/CudaBackendBurnNetworkRoutes.h
- R088 → src/cuda/runtime/burn/CudaBackendBurnRegisteredRoutes.h → src/cuda/runtime/burn/dispatch/CudaBackendBurnRegisteredRoutes.h
- R084 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular3D.cu → src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3D.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox13.cu → src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox13.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox19.cu → src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox19.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DAprox21.cu → src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DAprox21.cu
- R057 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular3DIso7.cu → src/cuda/runtime/burn/routes/tabular3/CudaBackendBurnTabular3DIso7.cu
- R084 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular4D.cu → src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4D.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox13.cu → src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox13.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox19.cu → src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox19.cu
- R058 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DAprox21.cu → src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DAprox21.cu
- R057 → src/cuda/runtime/burn/routes/CudaBackendBurnTabular4DIso7.cu → src/cuda/runtime/burn/routes/tabular4/CudaBackendBurnTabular4DIso7.cu
- R085 → src/cuda/runtime/burn/CudaBackendBurnSparse.h → src/cuda/runtime/burn/sparse/CudaBackendBurnSparse.h
- R088 → src/cuda/runtime/burn/CudaBackendBurnSparseFactory.cpp → src/cuda/runtime/burn/sparse/CudaBackendBurnSparseFactory.cpp
- R093 → src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh → src/cuda/runtime/burn/sparse/CudaBackendBurnSparseImpl.cuh
- R087 → src/cuda/runtime/burn/CudaBackendBurnSparseRoutes.h → src/cuda/runtime/burn/sparse/CudaBackendBurnSparseRoutes.h
- R089 → src/driver/dispatch/DispatchImpl.h → src/driver/dispatch/bindings/DispatchImpl.h
- R085 → src/driver/dispatch/Dispatch_Euler.cpp → src/driver/dispatch/bindings/Dispatch_Euler.cpp
- R084 → src/driver/dispatch/Dispatch_RK2.cpp → src/driver/dispatch/bindings/Dispatch_RK2.cpp
- R084 → src/driver/dispatch/Dispatch_RK3.cpp → src/driver/dispatch/bindings/Dispatch_RK3.cpp
- R096 → src/driver/dispatch/BackendCapabilities.h → src/driver/dispatch/capability/BackendCapabilities.h
- R094 → src/driver/dispatch/ResolvedExecutionPlan.h → src/driver/dispatch/capability/ResolvedExecutionPlan.h
- R097 → src/driver/dispatch/RuntimeProbe.cpp → src/driver/dispatch/capability/RuntimeProbe.cpp
- R087 → src/driver/dispatch/RuntimeProbe.h → src/driver/dispatch/capability/RuntimeProbe.h
- R055 → src/driver/InitialMesh.h → src/driver/initialization/InitialMesh.h
- R069 → src/driver/ComputeBackend.h → src/driver/runtime/ComputeBackend.h
- R098 → src/driver/StateResidency.h → src/driver/runtime/StateResidency.h
- R098 → src/driver/TopologyIdentityRegistry.h → src/driver/runtime/TopologyIdentityRegistry.h
- R090 → src/driver/DriverControl.h → src/driver/schedule/DriverControl.h
- R097 → src/driver/ReductionSpec.h → src/driver/schedule/ReductionSpec.h
- R092 → src/driver/StageScheduler.h → src/driver/schedule/StageScheduler.h
- R079 → src/driver/DriverBurnPolicy.h → src/driver/stages/DriverBurnPolicy.h
- R089 → src/numerics/burnsolver/BurnThermodynamics.h → src/numerics/burnsolver/coupling/BurnThermodynamics.h
- R095 → src/numerics/burnsolver/NetworkDerivative.h → src/numerics/burnsolver/coupling/NetworkDerivative.h
- R083 → src/numerics/burnsolver/odeFunction.h → src/numerics/burnsolver/coupling/odeFunction.h
- R084 → src/numerics/burnsolver/OdeContinuation.h → src/numerics/burnsolver/ode/OdeContinuation.h
- R092 → src/numerics/burnsolver/ode_bd.h → src/numerics/burnsolver/ode/ode_bd.h
- R093 → src/numerics/burnsolver/ode_be-nr.h → src/numerics/burnsolver/ode/ode_be-nr.h
- R088 → src/numerics/burnsolver/ode_ros4.h → src/numerics/burnsolver/ode/ode_ros4.h
- R086 → src/physics/eos/Tabular3DEOS.cpp → src/physics/eos/sources/Tabular3DEOS.cpp
- R071 → src/physics/eos/Tabular4DEOS.cpp → src/physics/eos/sources/Tabular4DEOS.cpp
- R097 → src/physics/eos/TabularBaryonSource.cpp → src/physics/eos/sources/TabularBaryonSource.cpp
- R094 → src/physics/eos/TabularBaryonSource.h → src/physics/eos/sources/TabularBaryonSource.h
- R096 → src/physics/eos/TabularCompletion.cpp → src/physics/eos/sources/TabularCompletion.cpp
- R084 → src/physics/eos/TabularCompletion.h → src/physics/eos/sources/TabularCompletion.h
- R060 → src/physics/eos/TabularLoaderUtils.h → src/physics/eos/sources/TabularLoaderUtils.h
- R089 → src/physics/eos/TabularSource.h → src/physics/eos/sources/TabularSource.h
- R064 → src/physics/eos/Tabular3DEOS.h → src/physics/eos/tabular/Tabular3DEOS.h
- R051 → src/physics/eos/Tabular4DEOS.h → src/physics/eos/tabular/Tabular4DEOS.h
- R098 → src/physics/eos/TabularFreeEnergy.h → src/physics/eos/tabular/TabularFreeEnergy.h
- R091 → src/physics/eos/TabularInterpolation.h → src/physics/eos/tabular/TabularInterpolation.h
- R095 → src/physics/eos/TabularInversion.h → src/physics/eos/tabular/TabularInversion.h
- R076 → tests/api/test_configuration.py → tests/api/configuration/test_configuration.py
- R098 → tests/api/test_parameter_metadata.py → tests/api/configuration/test_parameter_metadata.py
- R096 → tests/api/test_parameter_reads.cpp → tests/api/configuration/test_parameter_reads.cpp
- R094 → tests/api/test_case_inspection.py → tests/api/inspection/test_case_inspection.py
- R096 → tests/api/test_initialization_probe.cpp → tests/api/inspection/test_initialization_probe.cpp
- R093 → tests/api/cellular_reference.cpp → tests/api/preview/cellular_reference.cpp
- R096 → tests/api/read_mesh_checkpoint.cpp → tests/api/preview/read_mesh_checkpoint.cpp
- R100 → tests/api/test_cellular_preview.py → tests/api/preview/test_cellular_preview.py
- R066 → tests/api/test_initial_conversion.cpp → tests/api/preview/test_initial_conversion.cpp
- R095 → tests/api/test_preview.py → tests/api/preview/test_preview.py
- R097 → tests/api/test_sampling_limits.cpp → tests/api/preview/test_sampling_limits.cpp
- R098 → tests/api/test_ui_expansion.py → tests/api/preview/test_ui_expansion.py
- R097 → tests/api/test_initial_sample_cache.cpp → tests/api/session/test_initial_sample_cache.cpp
- R100 → tests/api/test_preview_session.py → tests/api/session/test_preview_session.py
- R097 → tests/api/test_verified_file_cache.cpp → tests/api/session/test_verified_file_cache.cpp
- R079 → tests/cuda/test_cuda_amr_composition.cu → tests/cuda/amr/test_cuda_amr_composition.cu
- R077 → tests/cuda/test_cuda_amr_exchange.cpp → tests/cuda/amr/test_cuda_amr_exchange.cpp
- R099 → tests/cuda/test_cuda_regrid_migration.cu → tests/cuda/amr/test_cuda_regrid_migration.cu
- R099 → tests/cuda/test_cuda_regrid_transaction.cpp → tests/cuda/amr/test_cuda_regrid_transaction.cpp
- R087 → tests/cuda/GeneratedSparseBurnFactory.h → tests/cuda/generated/GeneratedSparseBurnFactory.h
- R098 → tests/cuda/test_generated_network_math.cu → tests/cuda/generated/test_generated_network_math.cu
- R096 → tests/cuda/test_generated_nse_device.cu → tests/cuda/generated/test_generated_nse_device.cu
- R086 → tests/cuda/test_generated_sparse_burn.cpp → tests/cuda/generated/test_generated_sparse_burn.cpp
- R079 → tests/cuda/test_generated_sparse_burn_factory.cu → tests/cuda/generated/test_generated_sparse_burn_factory.cu
- R098 → tests/cuda/test_generated_weak_factory.cu → tests/cuda/generated/test_generated_weak_factory.cu
- R097 → tests/cuda/test_generated_weak_trajectory.cu → tests/cuda/generated/test_generated_weak_trajectory.cu
- R099 → tests/cuda/test_boundary_plan_parity.cu → tests/cuda/grid/test_boundary_plan_parity.cu
- R099 → tests/cuda/test_curvilinear_geometry_smoke.cu → tests/cuda/grid/test_curvilinear_geometry_smoke.cu
- R100 → tests/cuda/test_grid_metrics_cache.cu → tests/cuda/grid/test_grid_metrics_cache.cu
- R090 → tests/cuda/test_cuda_hydro_block.cu → tests/cuda/hydro/test_cuda_hydro_block.cu
- R053 → tests/cuda/test_cuda_multiblock_hydro.cu → tests/cuda/hydro/test_cuda_multiblock_hydro.cu
- R098 → tests/cuda/test_hydro_dispatch.cu → tests/cuda/hydro/test_hydro_dispatch.cu
- R070 → tests/cuda/test_hydro_eos_failure.cu → tests/cuda/hydro/test_hydro_eos_failure.cu
- R089 → tests/cuda/test_hydro_leaf_parity.cu → tests/cuda/hydro/test_hydro_leaf_parity.cu
- R097 → tests/cuda/test_burn_controller_parity.cu → tests/cuda/microphysics/burn/test_burn_controller_parity.cu
- R083 → tests/cuda/test_burn_eos_failure.cu → tests/cuda/microphysics/burn/test_burn_eos_failure.cu
- R093 → tests/cuda/test_burn_policy_parity.cu → tests/cuda/microphysics/burn/test_burn_policy_parity.cu
- R099 → tests/cuda/test_burn_thermal_math.cu → tests/cuda/microphysics/burn/test_burn_thermal_math.cu
- R074 → tests/cuda/test_cuda_multiblock_burn.cu → tests/cuda/microphysics/burn/test_cuda_multiblock_burn.cu
- R068 → tests/cuda/test_eos_host_device_parity.cu → tests/cuda/microphysics/eos/test_eos_host_device_parity.cu
- R097 → tests/cuda/test_native_tabular_owner.cu → tests/cuda/microphysics/eos/test_native_tabular_owner.cu
- R097 → tests/cuda/test_tabular_completion_device.cu → tests/cuda/microphysics/eos/test_tabular_completion_device.cu
- R098 → tests/cuda/test_tabular_free_energy_owner.cu → tests/cuda/microphysics/eos/test_tabular_free_energy_owner.cu
- R098 → tests/cuda/test_cuda_sparse_burn_factory.cpp → tests/cuda/microphysics/linalg/test_cuda_sparse_burn_factory.cpp
- R085 → tests/cuda/test_cudss_sparse_solver.cpp → tests/cuda/microphysics/linalg/test_cudss_sparse_solver.cpp
- R098 → tests/cuda/test_sparse_be_nr_batch.cu → tests/cuda/microphysics/linalg/test_sparse_be_nr_batch.cu
- R095 → tests/cuda/test_network_derivative.cu → tests/cuda/microphysics/network/test_network_derivative.cu
- R099 → tests/cuda/test_network_nse_device.cu → tests/cuda/microphysics/network/test_network_nse_device.cu
- R098 → tests/cuda/test_compensated_sum.cu → tests/cuda/numerics/test_compensated_sum.cu
- R065 → tests/cuda/test_cuda_multiblock_diffusion.cu → tests/cuda/numerics/test_cuda_multiblock_diffusion.cu
- R091 → tests/cuda/test_diffusion_rkl_parity.cu → tests/cuda/numerics/test_diffusion_rkl_parity.cu
- R097 → tests/cuda/test_cuda_compile_probe.cu → tests/cuda/runtime/test_cuda_compile_probe.cu
- R087 → tests/cuda/test_cuda_policy_resolution.cu → tests/cuda/runtime/test_cuda_policy_resolution.cu
- R095 → tests/cuda/test_cuda_reduction_contract.cu → tests/cuda/runtime/test_cuda_reduction_contract.cu
- R099 → tests/cuda/test_cuda_store_lifecycle.cpp → tests/cuda/runtime/test_cuda_store_lifecycle.cpp
- R098 → tests/cuda/test_mainline_authority.cpp → tests/cuda/runtime/test_mainline_authority.cpp
- R098 → tests/fixtures/regrid_migration_fixture.h → tests/fixtures/amr/regrid_migration_fixture.h
- R100 → tests/fixtures/BurnMainlineReference.h → tests/fixtures/burn/BurnMainlineReference.h
- R099 → tests/fixtures/BurnTimeReference.h → tests/fixtures/burn/BurnTimeReference.h
- R100 → tests/fixtures/SparseTransferNetwork.h → tests/fixtures/burn/SparseTransferNetwork.h
- R100 → tests/fixtures/HelmReference.h → tests/fixtures/eos/HelmReference.h
- R100 → tests/fixtures/NativeTabularFixture.h → tests/fixtures/eos/NativeTabularFixture.h
- R099 → tests/fixtures/checkpoint_conservation_metrics.h → tests/fixtures/io/checkpoint_conservation_metrics.h
- R100 → tests/fixtures/GeneratedNseReference.h → tests/fixtures/network/GeneratedNseReference.h
- R100 → tests/fixtures/NseReference.h → tests/fixtures/network/NseReference.h
- R099 → tests/host/test_amr_flux_surface_plan.cpp → tests/host/amr/test_amr_flux_surface_plan.cpp
- R070 → tests/host/test_amr_operation_plans.cpp → tests/host/amr/test_amr_operation_plans.cpp
- R099 → tests/host/test_block_handle.cpp → tests/host/amr/test_block_handle.cpp
- R099 → tests/host/test_boundary_plan.cpp → tests/host/amr/test_boundary_plan.cpp
- R098 → tests/host/test_refinement_indicator_math.cpp → tests/host/amr/test_refinement_indicator_math.cpp
- R097 → tests/host/test_same_level_exchange_plan.cpp → tests/host/amr/test_same_level_exchange_plan.cpp
- R099 → tests/host/test_topology_transaction.cpp → tests/host/amr/test_topology_transaction.cpp
- R096 → tests/host/test_burn_mainline_reference.cpp → tests/host/burn/test_burn_mainline_reference.cpp
- R098 → tests/host/test_sparse_ode_continuation.cpp → tests/host/burn/test_sparse_ode_continuation.cpp
- R100 → tests/host/test_physical_constants.cpp → tests/host/core/test_physical_constants.cpp
- R075 → tests/host/test_compute_backend.cpp → tests/host/driver/test_compute_backend.cpp
- R100 → tests/host/test_device_block_store_lifecycle.cpp → tests/host/driver/test_device_block_store_lifecycle.cpp
- R094 → tests/host/test_reduction_contract.cpp → tests/host/driver/test_reduction_contract.cpp
- R099 → tests/host/test_resolved_execution_plan.cpp → tests/host/driver/test_resolved_execution_plan.cpp
- R094 → tests/host/test_runtime_probe_and_capabilities.cpp → tests/host/driver/test_runtime_probe_and_capabilities.cpp
- R097 → tests/host/test_shared_stage_scheduler.cpp → tests/host/driver/test_shared_stage_scheduler.cpp
- R099 → tests/host/test_state_residency.cpp → tests/host/driver/test_state_residency.cpp
- R098 → tests/host/BaryonEosRegression.cpp → tests/host/eos/BaryonEosRegression.cpp
- R099 → tests/host/BaryonSourceRegression.cpp → tests/host/eos/BaryonSourceRegression.cpp
- R067 → tests/host/HelmComponentsRegression.cpp → tests/host/eos/HelmComponentsRegression.cpp
- R098 → tests/host/NativeTabularRegression.cpp → tests/host/eos/NativeTabularRegression.cpp
- R096 → tests/host/TabularCompletionRegression.cpp → tests/host/eos/TabularCompletionRegression.cpp
- R084 → tests/host/TabularEOSRegression.cpp → tests/host/eos/TabularEOSRegression.cpp
- R096 → tests/host/test_curvilinear_metrics.cpp → tests/host/grid/test_curvilinear_metrics.cpp
- R096 → tests/host/test_checkpoint_compatibility.cpp → tests/host/io/test_checkpoint_compatibility.cpp
- R099 → tests/host/test_checkpoint_conservation_metrics.cpp → tests/host/io/test_checkpoint_conservation_metrics.cpp
- R099 → tests/cuda/test_cuda_single_level_validation.cpp → tests/host/io/test_cuda_single_level_validation.cpp
- R100 → tests/host/test_generated_network_reference.cpp → tests/host/network/test_generated_network_reference.cpp
- R098 → tests/host/test_generated_nse_network.cpp → tests/host/network/test_generated_nse_network.cpp
- R100 → tests/host/SparseKLURegression.cpp → tests/host/numerics/SparseKLURegression.cpp
- R094 → tests/host/test_compensated_sum.cpp → tests/host/numerics/test_compensated_sum.cpp
- R100 → tests/math/CurvilinearMetricCases.h → tests/math/geometry/CurvilinearMetricCases.h
- R100 → tests/math/ViscousGeometryCases.h → tests/math/geometry/ViscousGeometryCases.h
- R091 → tests/math/BurnThermalCases.h → tests/math/microphysics/BurnThermalCases.h
- R098 → tests/math/GeneratedNseCases.h → tests/math/microphysics/GeneratedNseCases.h
- R098 → tests/math/NetworkDerivativeCases.h → tests/math/microphysics/NetworkDerivativeCases.h
- R091 → tests/math/test_tabular_strict.cpp → tests/math/microphysics/test_tabular_strict.cpp
- R080 → tests/tooling/test_audit_architecture.py → tests/tooling/architecture/test_audit_architecture.py
- R098 → tests/tooling/test_build_configuration.py → tests/tooling/build_tools/test_build_configuration.py
- R080 → tests/tooling/test_ci_results.py → tests/tooling/build_tools/test_ci_results.py
- R095 → tests/tooling/test_cuda_code_images.py → tests/tooling/build_tools/test_cuda_code_images.py
- R098 → tests/tooling/test_cuda_compile_memory.py → tests/tooling/build_tools/test_cuda_compile_memory.py
- R099 → tests/tooling/test_portable_network_generator.py → tests/tooling/network/test_portable_network_generator.py
- R099 → tests/tooling/test_run_memory_guarded.py → tests/tooling/resources/test_run_memory_guarded.py
- R098 → tests/tooling/test_validation_device_memory.py → tests/tooling/resources/test_validation_device_memory.py
- R099 → tests/tooling/test_curvilinear_manifest.py → tests/tooling/validation/test_curvilinear_manifest.py
- R090 → tests/tooling/test_runtime_validation_inputs.py → tests/tooling/validation/test_runtime_validation_inputs.py
- R099 → tests/tooling/test_smoke_cuda_amr_runtime.py → tests/tooling/validation/test_smoke_cuda_amr_runtime.py
- R092 → tests/tooling/test_validate_backend_results.py → tests/tooling/validation/test_validate_backend_results.py
- R096 → tests/tooling/test_validation_provenance.py → tests/tooling/validation/test_validation_provenance.py
- R099 → tests/tooling/test_validation_subprocess_logs.py → tests/tooling/validation/test_validation_subprocess_logs.py

## A-only build gate
Use a new independent CPU Debug tree with ARCH_ENABLE_CUDA=OFF,
ARCH_ENABLE_KLU=OFF, ARCH_ENABLE_OPENMP=ON, BUILD_TESTING=ON.
Do not reuse the v0.16 binary. Read actual CTest registry and execute the handoff regex
at -j1, then capture raw config-schema/list-cases/preview-capabilities responses.
Migrate the explicit tracked input set for the new profile (no silent missing paths),
generate new Build provenance, and report real capability counts. No CUDA/simulation
or Studio semantic/UI migration is authorized in A.
