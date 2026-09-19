# Phase 2E M0 — Core Contract Gap

## Decision

A: STOP — runtime Parameter Metadata and authoritative Sod binding are not published.
B: STOP — CellularDet 2D Preview is not published.
Neither implementation is started. Approved requirements are not evidence of an implemented API. Phase 2E is not complete; no completion report or success checkpoint is claimed.

## Baseline and remote evidence

- Baseline HEAD / studio-phase2d-v0.8.0: 43b381c3068824a373dd5477a92dc41627efca49.
- Original studio/phase2d-api-integration worktree was clean before audit.
- Fresh git fetch origin succeeded.
- origin/review/studio-v0.4.2: 4c0fd5c1242d9a48d2a75d7a2c546abb34f83627.
- origin/studio/phase2d-api-integration: 43b381c3068824a373dd5477a92dc41627efca49.
- No differences between these two refs in src/, simulation/, cmake/, CMakeLists.txt or tests/api/.
- New independent branch: studio/phase2e-metadata-binding-2d.
- Worktree: /home/arch/projects/ARCH-phase2e-metadata-binding-2d.
- New worktree verified clean at baseline before writing these audit documents.
- Phase 2D tag unchanged; no merge/reset/stash/discard/push.

## A. Parameter Metadata + Sod Binding

src/api/README.md explicitly states no parameter read tracing, position marker or drag binding. Preview.cpp:277-287 publishes parameterTracing=false and markers=false. The full response builder has no parameter metadata or graphical binding extension. Preview.h exposes only case/config/request ID and a single sample count. PreviewCommand.cpp accepts no metadata/binding request mode.

src/core/ProblemRegistry.h is a case factory, not a parameter registry. src/interface/ProblemGenerator.h exposes Setup, SampleInitialPrimitive and InitializeData, not a metadata/binding API. Network CPU bindings in ProblemHelper.cpp are unrelated to graphical binding.

simulation/Sod/Sod.cpp:36 reads x_pos with default 0.5; lines44-49 validate the strict interior x1 interval. This is authoritative scientific implementation evidence, but it is NOT a published runtime metadata/binding response. Studio must not extract defaults or constraints from C++ text. src/data/GlobalDefs.h:390-409 returns typed custom values or the supplied default without publishing per-read source provenance. Current JSON cannot distinguish explicit value, missing-key default and parse-failure fallback.

Required Core delivery: versioned optional metadata and graphical binding capabilities; actual read/default/source information including fallback reason; current-domain constraints; authoritative x1 axis-position binding; README, examples, scoped test results and exact commit ref. Unknown units/descriptions/constraints remain unknown. Studio TS types are not Core truth.

## B. CellularDet 2D Preview

The actual model lives under simulation/Cellular/, not simulation/CellularDet/. Cellular.cpp:114 registers case ID CellularDet. Setup/Init existing in that model does not make it callable through current Preview.

Preview.cpp:175-176 rejects any case other than Sod, any dimension other than 1, and non-Cartesian geometry. Lines257-261 construct only dimension=1 / kind=line / shape=[count] / one x1 axis. Lines277-287 advertise only Sod, dimension1 and six fields without VELY. PreviewCommand.cpp accepts only the existing one-dimensional --samples argument; no independent Nx/Ny request exists.

Cellular.cpp:46-48 reads radiusPerturb (default0.5), noiseAmplitude (default0), shock_dir (default0). Lines68-72 select x/y/z by shock_dir. Lines91-97 compare that coordinate to radiusPerturb and modify density/pressure inside the region. Thus radiusPerturb is an interface coordinate, not a circular radius; noiseAmplitude changes interior field values, not interface displacement. These observations do not authorize frontend physics or inferred graphical bindings.

Confirmed future requirements: Cartesian x1-x2; x3=0 recorded in response; shock_dir0/1 supported and2 rejected; shape=[Ny,Nx], x1-fastest, index=j*Nx+i; default128x128; tested per-axis/total limits and8MiB response bound published in capabilities; current fields plus planned VELY subject to published list. No runtime 2D schema/axes/sampling contract implementing these requirements is present yet.

Required delivery: implementation plus README, complete examples, exact commit, scoped tests, separate authoritative 2D reference .par and EOS table setup instructions. Shared state conversion/EOS must preserve field meanings. Current Preview.cpp:237 reuses InitialConservedState for 1D; this alone does not prove future2D correctness. Current oversize response at line273 calls PreviewInputError, which clears identity/state at lines289-293: the requested retention on oversize errors also remains a Core delivery requirement.

## Target precedence and Studio requirements

The complete attachment is copied byte-for-byte to studio/PHASE2E_TARGET.md and is the sole active target.

Its sections12/17 explicitly require NO automatic Preview after drag. This supersedes the older draft flow that requested Preview on release. Drag commits one Working Copy edit/Undo, never Save or Preview; the user invokes Update Preview. Source/default insertion, real coordinate marker, invalid input retention and stale/current semantics remain required.

A and B retain independent Stop Gates. Both are missing now, so no order ambiguity arises. Normal execution is A then independent checkpoint/stop, then B. No missing capability is guessed or implemented inside Studio.

The agreed compact preview status area must show domain, EOS loading, species and AMR configuration synchronized with the displayed preview identity; stale and failed-request snapshots must not be mixed. It will be implemented with the first available approved interface, not fabricated during M0.

## Verification scope and next action

Read-only source/document audit completed against freshly fetched refs; no ARCH executable, Preview, configure, Build, npm install, simulation or baseline tests ran. No scientific/Host/frontend code changed. Documentation-only diff validation is sufficient for this gate; implementation regression/UAT remains pending.

Next: Core provides A's exact ref, README, example responses and scoped test results. Re-audit that commit, integrate without overwriting user work, then execute A1-A7. B remains gated until its own delivery. Final PHASE2E_COMPLETION_REPORT.md and ORIGINAL_REQUIREMENT_CLOSURE_REPORT.md are deferred until actual requirement completion; do not represent this M0 audit as Phase2E acceptance.

Audit UTC: 2026-09-19T04:42:52.977386+00:00
Target SHA-256: c3dcb1d07c048947800ba696eb46cf396a816f38f82391698c7af2f60f2c9c6b

## Docs-only handoff

This copy preserves the historical M0 audit above. The referenced PHASE2E_TARGET.md remains in the separate M0 worktree and is not included in this three-file handoff. This branch starts at43b381c3 and contains only this gap report and the two contracts.

- [Contract A](ARCH_STUDIO_CORE_CONTRACT_A_PARAMETER_METADATA_SOD_BINDING.md)
- [Contract B](ARCH_STUDIO_CORE_CONTRACT_B_CELLULARDET_2D_PREVIEW.md)

The handoff documents requirements, not an implemented API. Both Stop Gates remain active. Core may adjust field names and publish the final README/examples/scoped results/exact ref. No implementation, Build or Preview was performed for this handoff.
