# Phase 2D upstream Preview API audit

Can Stop Gate be cleared? **YES**

## Git integration
Upstream: origin/review/studio-v0.4.2, commit 4c0fd5c1242d9a48d2a75d7a2c546abb34f83627.
Parent and merge-base with requested baseline: defc0ff7ccbf5304b63e90cd4792575f49a74006.
Fetched remote ref agrees with the supplied GitHub identity. Integration branch studio/phase2d-api-integration starts at Phase 2C / Phase 2D HEAD 332af5675768123cf23fbdf5e3dc3cb6f44a327d.
No-commit cherry-pick conflicted only in studio/README.md. Current Studio documentation retained; upstream API description added. No Core/API/CMake conflicts. No main integration, reset, stash, or modification of the original managed project or Phase 2D worktree.

## Actual implementation
This is an implemented CLI, not merely declarations. src/main.cpp branches to api/PreviewCommand.cpp before output/log/backend/Driver initialization. GeneratePreview in src/api/Preview.cpp resolves RuntimeParams::LoadText using the same ConfigParser; creates registered Sod, calls Setup, then SampleInitialPrimitive -> TypedProblemGenerator<SodProblem>::user_model.Init. Both mesh population and preview use core/InitialStateConversion.h and authoritative EOS dispatch. No copied frontend physics.

## Input / output
ARCH --preview-capabilities; ARCH --preview Sod --config-stdin --samples 512 --request-id ID.
Raw unsaved UTF-8 stdin <=1 MiB, no NUL. Samples 2..4096; default512. One process per request, cwd resolves EOS files. Config revision is SHA-256 of exact bytes.
Schema1.0: identity/requestId/caseId/configRevision; execution flags; state snapshots; data dimension1, x1 bin centers, uniform init-sample; DENS/PRES/TEMP/VELX/ENER/EINT, finite arrays and min/max. Units null, never inferred. Diagnostics structured. stdout one JSON, <=8 MiB; captured Core logs bounded16KiB/channel. Host must add project/profile/build/binary provenance.
Errors exit2 invalid request,3 invalid config,4 unsupported case/dimension/geometry/restart,5 setup/EOS failure,6 initialization failure. No partial curves on errors. Core parser defaults are not full simulation validation.

## Verification
Fresh isolated CPU Debug build in build-preview-audit, GCC12.4, CUDA/KLU OFF, HighFive existing headers read-only. Existing managed CUDA tree untouched. Unified executable SHA256 71d39b59f04400739a3b9d3027c4d0db2e403dd50f4315e31e1844f8c683537d, 44512624 bytes.
preview_initial_conversion: PASS (CTest).
preview_api_contract: 10 PASS, 1 explicitly SKIPPED. The optional test_matches_production_initial_output_at_root_cell_centers invokes ordinary simulation and writes Plotfiles, so this audit suppressed h5dump discovery in the Python test launcher only; upstream test source is unchanged. No simulation was run.
Passed: capabilities; real Sod field/identity assertions; exact configRevision; unsaved values/defaults with disk config unchanged; CPU preview despite requested CUDA; configured AMR metadata without hierarchy; invalid config/case/EOS errors; bounds/encoding; process termination on incomplete stdin without outputs; UTF-8 log truncation; actual Helmholtz load fingerprint. Each Preview test compares cwd file inventory before/after.
No API-created scientific outputs. CMake configure itself creates the repository's empty output directory per CMakeLists.txt; this is not Preview output and contains no simulation results.
Cancellation is OS process termination, not a dedicated Core cancel API. Host must implement timeout, process-group ownership and stale-result rejection.

## Build impact / gaps
New build is required. Old 11 tracked inputs omit src/api/**, InitialStateConversion.h, RuntimeParams.h, ProblemHelper.cpp, interface headers, ConfigParser and UserTypes. Some existing tracked files also changed. Old binary and Phase2C manifest cannot represent this API. A new fixed CPU integration profile will explicitly track these additional inputs and produce its own manifest; original CUDA profile remains available. Full dependency completeness stays unknown.
README matches reviewed call paths and tests. Positive tabular EOS tables were not qualified; required Sod ideal EOS is verified. No parameter metadata/marker contract; defer to Phase2E. Core schema1.0 is separate from Studio protocol1.3.

## Next step
P2D-M1 protocol1.3 and fixed PreviewProfile/schema, then M2-M10 in target order. No frontend integration before the above Core gate passed. No questions blocking Sod 1D. Core owner may separately extend scientific validation/tabular coverage; this report does not certify theoretical solutions.

## Upstream changed files
- cmake/Application.cmake
- cmake/tests/HostTests.cmake
- src/api/Json.h
- src/api/Preview.cpp
- src/api/Preview.h
- src/api/PreviewCommand.cpp
- src/api/README.md
- src/api/examples/missing-eos.json
- src/api/examples/sod.json
- src/core/InitialStateConversion.h
- src/core/ProblemHelper.cpp
- src/core/RuntimeParams.h
- src/data/UserTypes.h
- src/interface/GenericProblem.h
- src/interface/ProblemGenerator.h
- src/io/ConfigParser.h
- src/main.cpp
- studio/README.md
- tests/api/test_initial_conversion.cpp
- tests/api/test_preview.py
