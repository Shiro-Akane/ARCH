# Phase 3A main alignment report

Status: PASS WITH NON-BLOCKING ISSUES. Phase 3A only; STOP before Phase 3B.
Validation date: 2026-09-28T14:43:22.936458+00:00

## Source identity

- Main: 25adec4224497981a0c124a3485f786194975be4; final boundary fetch unchanged.
- Reviewed Studio: studio-phase2h-v0.19.0 / 840ab538f676f9b898a4680acaabc80201b13647.
- Branch: studio/phase3a-main-integration.
- Integration: new branch from main, exact studio-only import, then compatibility changes.
- All non-studio tracked paths/modes/blob identities match main. No scientific Core modifications.
- Historical worktrees and Phase 2 checkpoints are unchanged.
- Audit: PHASE3A_MAIN_BASED_INTEGRATION_AUDIT.md.

## CPU binary and Build provenance

- Workspace: /home/arch/projects/ARCH-mainline.
- Independent build: build-phase3a-cpu, Ninja, GNU 13.3.0, Debug.
- CUDA OFF; KLU OFF (approved local profile); OpenMP ON; BUILD_TESTING ON.
- Explicit runtime output: build-phase3a-cpu/bin/ARCH.
- Binary SHA-256: fb4f9de20f6ca152bf016d56182b15a6760a83224fbec6ab8643a4f0c2b5d9a4.
- Binary size: 72387504 bytes.
- Profile: arch-phase3a-cpu-integration.
- Successful Host Build ID: 0dd5efd2-d0c6-4923-ac1b-5c52184e413d.
- Build source Git HEAD recorded at execution: 25adec4224497981a0c124a3485f786194975be4.
- Repository dirty: True; Studio import/compatibility work was present.
- All 94 explicit tracked inputs stable during the successful Build.
- Full dependency coverage remains unknown (dependenciesComplete=false); this is not a full-dependency freshness claim.
- 77 old inputs individually classified; cache path migrated, 17 explicit mainline inputs added.
- Raw Manifest and before/after binary/input fingerprints: ignored studio/.local/phase3a/build-snapshot.json.
- Fresh CPU compile completed, then real Host BuildRunner generated its Manifest.
- Standard CMake configure creates its normal output directory; no simulation or scientific output was generated.

## Current Core compatibility

Runtime schema contains 95 keys; binary registry contains 14 models.
All registry source paths and compiled source SHA values matched this workspace.
Production discovery remains dynamic; tests include smaller/larger schema catalogs.
Full field/initial AMR capabilities remain Sod 1D and CellularDet 2D only.
GravityBox initialization inspection succeeded, without claiming field/AMR support.

The new eos_coulomb_mult, hll_wave_speed and dt_max controls consume runtime schema
defaults, options, units, applicability and descriptions. Defaults remain virtual until edited.
Schema Default, Inspection Parsed Value and Preview Effective Value remain separate.
Five retired keys retain Core diagnostics and explicit reversible removal; no custom/alias bypass.

Both local expression validation routes now share the restricted Core validator.
Real --inspect-config accepted exp(0), exp(1), exp(-1), exp(1e-3), and rejected
exp(), exp(foo), exp(1)junk, exp(NaN), exp(1000000), exp(exp(1)).
Regression also rejects exp(pi) and underflowed numeric arguments.
Custom x_pos remains a float; this change does not add expression support to it.

The deleted Core A fallback fixture was replaced in the test by current invalid-number
and nonfinite error fixtures. Invalid explicit custom values are rejected, not defaulted.

## Automated validation

- npm ci: PASS, lockfile unchanged.
- npm test: 172 passed, 0 failed, 0 skipped.
- npm run test:host: 70 passed, 0 failed, 0 skipped (included in npm test as well).
- npm run lint: PASS.
- npm run typecheck: PASS.
- npm run build: PASS; existing large JS chunk warning retained.
- Node 24.21.0 / npm 11.19.0.
- Current Core scoped registry: 11 tests passed in aggregate, 0 remaining Not Run.
  preview_initial_conversion, preview_api_contract, configuration_api_contract,
  preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits,
  case_inspection_contract, preview_session_contract, preview_verified_resources,
  preview_exact_sample_cache, preview_cellular_2d.
- The first Core run found two not-yet-built test executables. They were built and
  both tests passed; the nine successful tests were not needlessly rerun.
- Simulation oracle was not enabled. No simulation, CUDA validation, Run or Restart.
- One concurrent Studio run exposed the existing 150 ms queue-fixture timing sensitivity.
  After the heavy Core test ended, full Studio and Host runs passed without code changes
  or relaxed assertions. This remains a non-blocking test robustness observation.
- git diff --check for Phase 3A compatibility changes: PASS.
  The exact imported historical documents/license/fixture contain pre-existing whitespace
  warnings when compared directly to main. These bytes were preserved, not silently rewritten.

## UAT and real integration evidence

Production build tested in a fresh browser against the new local Host.

| Check | Evidence / outcome |
|---|---|
| Catalog/new keys | UI shows 95 keys / 95 controls; all three new keys searchable, Core defaults/description/options/applicability visible |
| exp expression | UI accepted x1_max=exp(1), inspected and generated real Sod Preview |
| Default insertion/Undo | dt_max first edit becomes explicit; one Ctrl+Z removes assignment and restores virtual default |
| Save As / Save / Reopen | UI wrote studio/.local/phase3a/uat-sod.par, reopened exp(1) and hll_wave_speed=davis; subsequent Save persisted dt_max=0.01; disk bytes independently checked |
| Sod | Real curve, authoritative x_pos marker, cold/warm Host requests, initial AMR and matching overlay |
| CellularDet | Real 20×12 UI grid, shape [12,20], x1-fastest, pressure field selection and matching initial AMR; direct Host regression also 20×12 |
| Model change | Previous field and AMR visibly stale/separate; never Current on a changed model/config |
| Inspection-only | GravityBox registry/source association and real initialization inspection; UI explicitly denies full field/AMR |
| Retired keys | Real Core inspection rejects all five; explicit removal re-inspects successfully; Undo/raw safety covered by regression |
| Session/cancel/race | Current real Core session/Cellular contracts plus Host queue/cancel/stale/failure-retention suite PASS |
| Limited AMR | Real Host bounded Sod request retains limited semantics; hierarchy remains separate from Init samples |
| Desktop | 1280×720 and 1920×1080 exercised; existing scrollable panels; no mobile adaptation added |

Ignored runtime evidence: studio/.local/phase3a/ (logs, raw capability responses,
runtime/runtime.json, expressions.json, build-snapshot.json, UAT config).
No generated binary, node_modules, dist or .local files are committed.

## Remaining limits

Full transitive Build freshness is unknown. Large frontend bundle warning and timing-sensitive
test fixture remain non-blocking. Existing verbose presentation/not-applicable unit text and
workspace layout are deferred to 3B. Linux independent launcher and Run/Restart are 3C;
full registered-model fields and large Plotfile workflows require later capability gates.
No full-Core workflow completion claim. No automatic push.
