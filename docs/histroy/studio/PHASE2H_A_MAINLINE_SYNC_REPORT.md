# Phase 2H-A — Mainline Sync and CPU Revalidation

2026-09-27. **PASS — A checkpoint only. STOP before B.**

Active target: [PHASE2H_TARGET.md](PHASE2H_TARGET.md), exact copy of the reuploaded
ARCH_STUDIO_PHASE2H_MAINLINE_CAPABILITY_SYNC_TARGET.md. Rechecked A sections 0–11
and tracked-input migration obligations at completion.

## Integration

- Studio baseline: e13b4bcd00fd763029a50c17b95c002d84cf780b / studio-phase2g-v0.16.0.
- Authoritative upstream: 502eadcb33a9e2d3c8bd079a10dbdc8af208ef10.
- Upstream parent: 48f6d357d6b8085501d2012d70e923080bae3402.
- Branch: studio/phase2h-mainline-capability-sync.
- Independent implementation worktree: /home/arch/projects/ARCH-phase2h-mainline-capability-sync.
- Disposable audit: /home/arch/projects/ARCH-phase2h-sync-audit.
- Pure upstream tree synchronization commit: 86f999bafccf5868cef4e4258ff0e166048a35ae.
- Separate Studio compatibility/report commit: resolve the annotated
  studio-phase2h-a-v0.17.0 tag to obtain the final checkpoint hash.

[Mainline Sync Audit](PHASE2H_MAINLINE_SYNC_AUDIT.md) records ownership, divergence,
the selected exact-tree strategy, deletions and all detected renames.
The disposable audit passed before the implementation worktree was created.
The implementation is a linear descendant of Studio v0.16 with authoritative
non-Studio tree replacement, not a main merge or a docs-only cherry-pick.

All 18928 tracked non-Studio entries match upstream by path, mode and
object ID, including CMake, scientific Core/API, simulation, tests/api, tests/host,
docs/development, include and other upstream repository paths. Upstream deletions
and rename sources do not remain in the tracked tree. No local scientific patches.

At the sync commit, the complete Studio tree remained identical to v0.16:
125e04ea0f2a1b963960f1263aa9d60a4e2c9e03. Subsequent code changes are confined to the
CPU Build Profile and missing-input validation, plus its focused regression.
No frontend, parser, parameter/discovery semantics, Session/AMR UI or desktop
implementation was migrated.

The original Phase 2G checkout is clean at its original commit. Its tag, binary
and build tree were not modified. The old ARCH-linux/CUDA tree was not rebound.

## Fresh CPU build

~~~sh
cmake -S . -B build-phase2h-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DARCH_ENABLE_CUDA=OFF \
  -DARCH_ENABLE_KLU=OFF -DARCH_ENABLE_OPENMP=ON -DBUILD_TESTING=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY=/home/arch/projects/ARCH-phase2h-mainline-capability-sync/build-phase2h-cpu/bin
cmake --build build-phase2h-cpu --target ARCH \
  arch_preview_initial_conversion arch_preview_parameter_reads \
  arch_preview_sampling_limits arch_initialization_probe \
  arch_verified_file_cache arch_initial_sample_cache \
  arch_preview_mesh_checkpoint arch_preview_cellular_reference --parallel 8
~~~

Configure and all 97 build steps passed. This is a new CPU tree and binary, not
the v0.16 artifact. CMAKE_HOME_DIRECTORY equals the implementation worktree;
CMAKE_CACHEFILE_DIR equals its build-phase2h-cpu directory.
Parallelism 8 was used on 28 logical CPUs / approximately 23 GiB WSL RAM.

- Executable: /home/arch/projects/ARCH-phase2h-mainline-capability-sync/build-phase2h-cpu/bin/ARCH.
- SHA-256: 53fdf7939cf4d68c806b206f1e7cf2f4d0e6d3724682a203af6e01250375fcf7.
- Size: 67789160 bytes.
- mtime: 2026-09-27T01:33:52.551Z.
- CPU Debug, CUDA OFF, KLU OFF, OpenMP ON, BUILD_TESTING ON.
- No CUDA build, GPU validation or formal simulation.

## Current Core gate

~~~sh
ARCH_PREVIEW_SIMULATION_ORACLE=0 ctest --test-dir build-phase2h-cpu \
  -R '^(preview_|configuration_api_contract|ui_expansion_contract|initialization_probe|case_inspection_contract)' \
  --output-on-failure -j1
~~~

**13/13 groups PASS, 231.54 seconds.** The actual registry matches the handoff;
no selected CTest group was removed. The upstream optional production-simulation
oracle remained disabled; this does not claim simulation validation.

| Test | Seconds |
| --- | ---: |
| preview_initial_conversion | 0.00 |
| preview_api_contract | 12.79 |
| configuration_api_contract | 1.83 |
| preview_parameter_reads | 0.00 |
| preview_parameter_metadata | 0.14 |
| preview_sampling_limits | 0.83 |
| initialization_probe | 0.00 |
| case_inspection_contract | 23.08 |
| preview_session_contract | 34.25 |
| preview_verified_resources | 0.03 |
| preview_exact_sample_cache | 0.00 |
| ui_expansion_contract | 29.09 |
| preview_cellular_2d | 129.48 |

## New binary runtime evidence

Raw unmodified responses are saved under ignored studio/.local/phase2h/.

| Response | Actual result | SHA-256 |
| --- | --- | --- |
| config-schema.json | configuration version="2", 92 parameters | 86e6f90a4826a54a0eeddf9db8c317d5b924bebe7e09a2cfcdb33e4247d1bde8 |
| list-cases.json | 14 registered cases | 0c78b8f9b67526c545c8889db1a4b6e11acb3322e335608091cc2f3865b52664 |
| preview-capabilities.json | CPU Sod 1D / CellularDet 2D | d68b30521dda715cbcfc62f1933f825851c9f8698ff3b2e96b3e7b3ab31791a6 |

The protocol envelope remains schemaVersion="1.0"; configuration extension
version is "2". These are different fields. No permanent 92/14 assertion was
added to production code.

Actual models: BurnGradient, BurnOneZone, CellularDet, CooperativeHotspots, DiffusionMode, ExternalGravity, Gaussian, GravityBox, JeansWave, RT, SNIaCoupled, Sedov, SmoothAdvection, Sod.

Only Sod and CellularDet advertise initialFieldPreview and initialAmrPreview.
modelCapabilities specifies [1] / [2] dimensions; extensions.amr.cases lists both.
Legacy top-level cases=["Sod"] and amrHierarchy=false remain in the raw response;
they are not the authoritative full model list. Registered does not imply field
or AMR support. No additional full Preview model was enabled.

## Fixed Profile and truthful Manifest

- Profile: arch-mainline-cpu-integration.
- Managed source: /home/arch/projects/ARCH-phase2h-mainline-capability-sync.
- Build directory: /home/arch/projects/ARCH-phase2h-mainline-capability-sync/build-phase2h-cpu.
- Target: ARCH; executable: build-phase2h-cpu/bin/ARCH.
- Profile fingerprint: 6c99ca19a9869f83f57eb781b8416a5cd91d6f1ff28d59f99c9de4f273ed9182.
- Explicit tracked inputs: 77; dependenciesComplete=false.
- Case/binary mapping remains configured, not verified.
- Existing Sod/Cellular Preview scope remains unchanged. B will handle
  runtime registry/UI migration; no static 14-model whitelist was introduced.

After the full fresh build, the existing real Host BuildRunner executed its
fixed incremental ARCH build to persist provenance. Ninja reported no work to
do; this was the Host manifest recording step, not a second full rebuild.

- Build ID: 819e0395-cf95-4138-856d-0fa489386973.
- Start/end: 2026-09-27T01:40:03.816Z / 2026-09-27T01:40:04.349Z.
- Build-time source Git HEAD: 86f999bafccf5868cef4e4258ff0e166048a35ae.
- repositoryDirty=true at build time: A Studio compatibility/report changes
  were pending; no Core-owned source was modified.
- Before/after executable fingerprint equal; all tracked inputs stable.
- Source fingerprint, pre/post input fingerprints, executable SHA/size/mtime,
  profile identity, paths and timestamps are recorded.
- changedInputs is empty; binaryState remains freshness-unknown because the
  explicit list is not a complete transitive dependency graph.
- Committing Studio-only changes does not change compiled inputs. The Manifest
  retains the actual build-time HEAD and dirty state, rather than rewriting history.

Persisted Manifest: studio/.local/build-*.json. Audit copy:
.local/phase2h/build-snapshot.json; Host events: host-build-events.json.
Missing/non-file tracked inputs invalidate the Profile before CMake spawns, with
a migration-required error. Old paths are never silently skipped.

## Every prior Manifest input

Actual v0.16 Manifest build ID: 8385514a-6d14-4b7b-bd1c-de8d8829a0c6.
Its 75 tracked paths equal the prior Profile list; all are covered below.
46 source paths moved, 28 paths remain, and one CMakeCache path belongs to the
new independent tree. No old input was dropped. Two current public include
wrappers were added. All 77 current paths exist and are fingerprinted.

Existing paths remain relevant as selected case sources, API/configuration/
initialization infrastructure, shared grid/physics interfaces or build inputs.
This is a reviewed partial list, not a complete dependency claim.

| Previous input | Current input | Decision |
| --- | --- | --- |
| simulation/Sod/Sod.cpp | simulation/Sod/Sod.cpp | retain |
| src/main.cpp | src/main.cpp | retain |
| src/core/ProblemRegistry.h | src/core/problem/ProblemRegistry.h | upstream migration |
| CMakeLists.txt | CMakeLists.txt | retain |
| CMakePresets.json | CMakePresets.json | retain |
| cmake/Application.cmake | cmake/project/Application.cmake | upstream migration |
| cmake/BuildOptions.cmake | cmake/project/BuildOptions.cmake | upstream migration |
| cmake/CudaBackend.cmake | cmake/cuda/CudaBackend.cmake | upstream migration |
| cmake/Dependencies.cmake | cmake/dependencies/Dependencies.cmake | upstream migration |
| cmake/CustomNetworks.cmake | cmake/CustomNetworks.cmake | retain |
| build-preview-audit/CMakeCache.txt | build-phase2h-cpu/CMakeCache.txt | new configured tree |
| src/core/StandardParameters.h | src/core/config/StandardParameters.h | upstream migration |
| src/api/Configuration.h | src/api/Configuration.h | retain |
| src/api/Configuration.cpp | src/api/configuration/Configuration.cpp | upstream migration |
| src/api/PresentationMetadata.cpp | src/api/configuration/PresentationMetadata.cpp | upstream migration |
| src/api/LogCapture.h | src/api/protocol/LogCapture.h | upstream migration |
| src/driver/dispatch/PolicyDescriptor.h | src/driver/dispatch/PolicyDescriptor.h | retain |
| src/driver/dispatch/ResolvedExecutionPlan.h | src/driver/dispatch/capability/ResolvedExecutionPlan.h | upstream migration |
| src/api/Preview.cpp | src/api/preview/Preview.cpp | upstream migration |
| src/api/PreviewCommand.cpp | src/api/preview/PreviewCommand.cpp | upstream migration |
| src/api/Preview.h | src/api/Preview.h | retain |
| src/api/Json.h | src/api/protocol/Json.h | upstream migration |
| src/core/InitialStateConversion.h | src/core/problem/InitialStateConversion.h | upstream migration |
| src/core/ProblemHelper.cpp | src/core/problem/ProblemHelper.cpp | upstream migration |
| src/core/RuntimeParams.h | src/core/config/RuntimeParams.h | upstream migration |
| src/core/FileFingerprint.cpp | src/core/files/FileFingerprint.cpp | upstream migration |
| src/core/FileFingerprint.h | src/core/files/FileFingerprint.h | upstream migration |
| src/interface/GenericProblem.h | src/interface/GenericProblem.h | retain |
| src/interface/ProblemGenerator.h | src/interface/ProblemGenerator.h | retain |
| src/data/UserTypes.h | src/data/UserTypes.h | retain |
| src/io/ConfigParser.h | src/io/ConfigParser.h | retain |
| cmake/tests/HostTests.cmake | cmake/tests/HostTests.cmake | retain |
| src/api/ParameterMetadata.cpp | src/api/configuration/ParameterMetadata.cpp | upstream migration |
| src/api/ParameterMetadata.h | src/api/configuration/ParameterMetadata.h | upstream migration |
| src/interface/PreviewMetadata.h | src/interface/PreviewMetadata.h | retain |
| src/data/GlobalDefs.h | src/data/GlobalDefs.h | retain |
| src/api/Sampling.h | src/api/preview/Sampling.h | upstream migration |
| src/api/Response.h | src/api/protocol/Response.h | upstream migration |
| src/core/ProblemHelper.h | src/core/problem/ProblemHelper.h | upstream migration |
| src/grid/Grid.h | src/grid/Grid.h | retain |
| src/physics/constant/PhysicalConstants.h | src/physics/constant/PhysicalConstants.h | retain |
| src/amr/AmrDefines.h | src/amr/topology/AmrDefines.h | upstream migration |
| simulation/Cellular/Cellular.cpp | simulation/Cellular/Cellular.cpp | retain |
| src/amr/RefinementThermodynamics.h | src/amr/refinement/RefinementThermodynamics.h | upstream migration |
| src/api/ApplicationContract.h | src/api/ApplicationContract.h | retain |
| src/api/CaseInspection.cpp | src/api/inspection/CaseInspection.cpp | upstream migration |
| src/api/CaseInspection.h | src/api/CaseInspection.h | retain |
| src/api/CaseUnitEvidence.cpp | src/api/inspection/CaseUnitEvidence.cpp | upstream migration |
| src/api/Discovery.cpp | src/api/inspection/Discovery.cpp | upstream migration |
| src/api/InitialMesh.h | src/api/preview/InitialMesh.h | upstream migration |
| src/api/InitialSampleCache.h | src/api/session/InitialSampleCache.h | upstream migration |
| src/api/ParameterPresentation.h | src/api/configuration/ParameterPresentation.h | upstream migration |
| src/api/PreviewSession.cpp | src/api/session/PreviewSession.cpp | upstream migration |
| src/api/PreviewSession.h | src/api/PreviewSession.h | retain |
| src/api/Progress.h | src/api/protocol/Progress.h | upstream migration |
| src/api/RequestInput.h | src/api/protocol/RequestInput.h | upstream migration |
| src/api/ResourceEstimates.cpp | src/api/preview/ResourceEstimates.cpp | upstream migration |
| src/api/ResourceEstimates.h | src/api/preview/ResourceEstimates.h | upstream migration |
| src/api/SessionInput.h | src/api/session/SessionInput.h | upstream migration |
| src/api/StateSnapshot.cpp | src/api/preview/StateSnapshot.cpp | upstream migration |
| src/api/StateSnapshot.h | src/api/preview/StateSnapshot.h | upstream migration |
| src/api/ValueDomain.h | src/api/configuration/ValueDomain.h | upstream migration |
| src/api/WorkerLimits.cpp | src/api/resources/WorkerLimits.cpp | upstream migration |
| src/api/WorkerLimits.h | src/api/resources/WorkerLimits.h | upstream migration |
| src/core/InspectionSources.h | src/core/files/InspectionSources.h | upstream migration |
| src/core/UserInterface.h | src/core/config/UserInterface.h | upstream migration |
| src/core/VerifiedFileCache.h | src/core/files/VerifiedFileCache.h | upstream migration |
| src/driver/Driver.h | src/driver/Driver.h | retain |
| src/driver/InitialMesh.h | src/driver/initialization/InitialMesh.h | upstream migration |
| src/driver/SolverDispatch.cpp | src/driver/SolverDispatch.cpp | retain |
| src/physics/eos/IdealGas.h | src/physics/eos/IdealGas.h | retain |
| src/physics/eos/InspectionEosCache.h | src/physics/eos/InspectionEosCache.h | retain |
| src/physics/eos/eosdispatch.h | src/physics/eos/eosdispatch.h | retain |
| src/physics/network/timmes_common/TimmesNetworkSupport.h | src/physics/network/timmes_common/TimmesNetworkSupport.h | retain |
| src/physics/species/Species.h | src/physics/species/Species.h | retain |
| — | include/UserInterface.h | public include wrapper |
| — | include/GlobalDefs.h | public include wrapper |

## Studio checks and evidence

- node --test tests/host-build*.test.ts: **10/10 PASS**, including missing input
  rejection before spawning, source/cache bindings, command ownership,
  success/failure Manifest retention and truthful freshness.
- npm run lint: PASS.
- npm run typecheck: PASS.
- git diff --check: PASS at checkpoint review.
- Independent npm ci from lockfile; Electron binary download skipped because A
  does not package or perform desktop UAT.
- Full Studio/Host regression belongs to B, and desktop UAT to C; neither is
  claimed by this A-only report.

Ignored evidence includes configure.log, cpu-build.log, core-tests.log,
test-registry.txt, audit-verification.json, upstream-core-changes.tsv,
input-migration.json, old-manifest-path-verification.json,
final-verification.json, host-build-tests.log, lint.log and typecheck.log.
Build outputs, node_modules and local evidence are not committed.

## Checkpoint / STOP

Local annotated checkpoint: studio-phase2h-a-v0.17.0. No automatic push.
No B schema/retired-key/gravity/source-view semantic migration, no C desktop
requalification, and no Phase 3. **STOP pending B authorization.**
