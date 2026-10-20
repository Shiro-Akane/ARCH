# Phase 3A main-based integration audit

- Main baseline: 25adec4224497981a0c124a3485f786194975be4
- Studio source: studio-phase2h-v0.19.0 / 840ab538f676f9b898a4680acaabc80201b13647
- Branch: studio/phase3a-main-integration
- Workspace: /home/arch/projects/ARCH-mainline
- Import: git restore --source=studio-phase2h-v0.19.0 --staged --worktree -- studio
- Before compatibility changes, imported Studio tree exactly equals the reviewed tag.
- Non-Studio tracked paths, file modes and Git object identities exactly equal main.
- Exceptions: none. No scientific Core edits or historical Core replacement.
- Existing historical worktrees and checkpoints are preserved.
- Scope: Phase 3A only; subsequent stages require authorization.

Build input migration and runtime verification are pending.

## Explicit Build input migration

All 77 previous inputs accounted for; no deleted or renamed source inputs. One cache path migrates to the fresh build directory. 17 additional explicit mainline build/dispatch/gravity/EOS inputs are tracked. Full transitive dependency coverage remains unknown; dependenciesComplete=false.

| Previous tracked input | Classification | Current input |
|---|---|---|
| simulation/Sod/Sod.cpp | exists and retained | simulation/Sod/Sod.cpp |
| src/main.cpp | exists and retained | src/main.cpp |
| src/core/problem/ProblemRegistry.h | exists and retained | src/core/problem/ProblemRegistry.h |
| CMakeLists.txt | exists and retained | CMakeLists.txt |
| CMakePresets.json | exists and retained | CMakePresets.json |
| cmake/project/Application.cmake | exists and retained | cmake/project/Application.cmake |
| cmake/project/BuildOptions.cmake | exists and retained | cmake/project/BuildOptions.cmake |
| cmake/cuda/CudaBackend.cmake | exists and retained | cmake/cuda/CudaBackend.cmake |
| cmake/dependencies/Dependencies.cmake | exists and retained | cmake/dependencies/Dependencies.cmake |
| cmake/CustomNetworks.cmake | exists and retained | cmake/CustomNetworks.cmake |
| build-phase2h-cpu/CMakeCache.txt | build-tree path migrated | build-phase3a-cpu/CMakeCache.txt |
| src/core/config/StandardParameters.h | exists and retained | src/core/config/StandardParameters.h |
| src/api/Configuration.h | exists and retained | src/api/Configuration.h |
| src/api/configuration/Configuration.cpp | exists and retained | src/api/configuration/Configuration.cpp |
| src/api/configuration/PresentationMetadata.cpp | exists and retained | src/api/configuration/PresentationMetadata.cpp |
| src/api/protocol/LogCapture.h | exists and retained | src/api/protocol/LogCapture.h |
| src/driver/dispatch/PolicyDescriptor.h | exists and retained | src/driver/dispatch/PolicyDescriptor.h |
| src/driver/dispatch/capability/ResolvedExecutionPlan.h | exists and retained | src/driver/dispatch/capability/ResolvedExecutionPlan.h |
| src/api/preview/Preview.cpp | exists and retained | src/api/preview/Preview.cpp |
| src/api/preview/PreviewCommand.cpp | exists and retained | src/api/preview/PreviewCommand.cpp |
| src/api/Preview.h | exists and retained | src/api/Preview.h |
| src/api/protocol/Json.h | exists and retained | src/api/protocol/Json.h |
| src/core/problem/InitialStateConversion.h | exists and retained | src/core/problem/InitialStateConversion.h |
| src/core/problem/ProblemHelper.cpp | exists and retained | src/core/problem/ProblemHelper.cpp |
| src/core/config/RuntimeParams.h | exists and retained | src/core/config/RuntimeParams.h |
| src/core/files/FileFingerprint.cpp | exists and retained | src/core/files/FileFingerprint.cpp |
| src/core/files/FileFingerprint.h | exists and retained | src/core/files/FileFingerprint.h |
| src/interface/GenericProblem.h | exists and retained | src/interface/GenericProblem.h |
| src/interface/ProblemGenerator.h | exists and retained | src/interface/ProblemGenerator.h |
| src/data/UserTypes.h | exists and retained | src/data/UserTypes.h |
| src/io/ConfigParser.h | exists and retained | src/io/ConfigParser.h |
| cmake/tests/HostTests.cmake | exists and retained | cmake/tests/HostTests.cmake |
| src/api/configuration/ParameterMetadata.cpp | exists and retained | src/api/configuration/ParameterMetadata.cpp |
| src/api/configuration/ParameterMetadata.h | exists and retained | src/api/configuration/ParameterMetadata.h |
| src/interface/PreviewMetadata.h | exists and retained | src/interface/PreviewMetadata.h |
| src/data/GlobalDefs.h | exists and retained | src/data/GlobalDefs.h |
| src/api/preview/Sampling.h | exists and retained | src/api/preview/Sampling.h |
| src/api/protocol/Response.h | exists and retained | src/api/protocol/Response.h |
| src/core/problem/ProblemHelper.h | exists and retained | src/core/problem/ProblemHelper.h |
| src/grid/Grid.h | exists and retained | src/grid/Grid.h |
| src/physics/constant/PhysicalConstants.h | exists and retained | src/physics/constant/PhysicalConstants.h |
| src/amr/topology/AmrDefines.h | exists and retained | src/amr/topology/AmrDefines.h |
| simulation/Cellular/Cellular.cpp | exists and retained | simulation/Cellular/Cellular.cpp |
| src/amr/refinement/RefinementThermodynamics.h | exists and retained | src/amr/refinement/RefinementThermodynamics.h |
| src/api/ApplicationContract.h | exists and retained | src/api/ApplicationContract.h |
| src/api/inspection/CaseInspection.cpp | exists and retained | src/api/inspection/CaseInspection.cpp |
| src/api/CaseInspection.h | exists and retained | src/api/CaseInspection.h |
| src/api/inspection/CaseUnitEvidence.cpp | exists and retained | src/api/inspection/CaseUnitEvidence.cpp |
| src/api/inspection/Discovery.cpp | exists and retained | src/api/inspection/Discovery.cpp |
| src/api/preview/InitialMesh.h | exists and retained | src/api/preview/InitialMesh.h |
| src/api/session/InitialSampleCache.h | exists and retained | src/api/session/InitialSampleCache.h |
| src/api/configuration/ParameterPresentation.h | exists and retained | src/api/configuration/ParameterPresentation.h |
| src/api/session/PreviewSession.cpp | exists and retained | src/api/session/PreviewSession.cpp |
| src/api/PreviewSession.h | exists and retained | src/api/PreviewSession.h |
| src/api/protocol/Progress.h | exists and retained | src/api/protocol/Progress.h |
| src/api/protocol/RequestInput.h | exists and retained | src/api/protocol/RequestInput.h |
| src/api/preview/ResourceEstimates.cpp | exists and retained | src/api/preview/ResourceEstimates.cpp |
| src/api/preview/ResourceEstimates.h | exists and retained | src/api/preview/ResourceEstimates.h |
| src/api/session/SessionInput.h | exists and retained | src/api/session/SessionInput.h |
| src/api/preview/StateSnapshot.cpp | exists and retained | src/api/preview/StateSnapshot.cpp |
| src/api/preview/StateSnapshot.h | exists and retained | src/api/preview/StateSnapshot.h |
| src/api/configuration/ValueDomain.h | exists and retained | src/api/configuration/ValueDomain.h |
| src/api/resources/WorkerLimits.cpp | exists and retained | src/api/resources/WorkerLimits.cpp |
| src/api/resources/WorkerLimits.h | exists and retained | src/api/resources/WorkerLimits.h |
| src/core/files/InspectionSources.h | exists and retained | src/core/files/InspectionSources.h |
| src/core/config/UserInterface.h | exists and retained | src/core/config/UserInterface.h |
| src/core/files/VerifiedFileCache.h | exists and retained | src/core/files/VerifiedFileCache.h |
| src/driver/Driver.h | exists and retained | src/driver/Driver.h |
| src/driver/initialization/InitialMesh.h | exists and retained | src/driver/initialization/InitialMesh.h |
| src/driver/SolverDispatch.cpp | exists and retained | src/driver/SolverDispatch.cpp |
| src/physics/eos/IdealGas.h | exists and retained | src/physics/eos/IdealGas.h |
| src/physics/eos/InspectionEosCache.h | exists and retained | src/physics/eos/InspectionEosCache.h |
| src/physics/eos/eosdispatch.h | exists and retained | src/physics/eos/eosdispatch.h |
| src/physics/network/timmes_common/TimmesNetworkSupport.h | exists and retained | src/physics/network/timmes_common/TimmesNetworkSupport.h |
| src/physics/species/Species.h | exists and retained | src/physics/species/Species.h |
| include/UserInterface.h | exists and retained | include/UserInterface.h |
| include/GlobalDefs.h | exists and retained | include/GlobalDefs.h |
| — | new required input | cmake/project/SelectIpoLinker.cmake |
| — | new required input | src/physics/eos/eosdispatch.cpp |
| — | new required input | src/physics/gravity/GravityExecution.cpp |
| — | new required input | src/physics/gravity/GravityBoundary.cpp |
| — | new required input | src/physics/gravity/self/SelfGravity.cpp |
| — | new required input | src/physics/gravity/self/GravityWorkspace.cpp |
| — | new required input | src/numerics/elliptic/CartesianPoisson.cpp |
| — | new required input | src/numerics/elliptic/CompositePoisson.cpp |
| — | new required input | src/numerics/multigrid/HostMultigrid.cpp |
| — | new required input | src/numerics/multigrid/CompositeMultigrid.cpp |
| — | new required input | src/numerics/multigrid/CompositeExecution.cpp |
| — | new required input | src/driver/runtime/DriverRuntime.cpp |
| — | new required input | src/driver/runtime/DriverBoundary.cpp |
| — | new required input | src/driver/runtime/DriverRegrid.cpp |
| — | new required input | src/driver/io/DriverIO.cpp |
| — | new required input | src/driver/stages/GravityStage.cpp |
| — | new required input | src/amr/elliptic/EllipticMeshAdapter.cpp |
