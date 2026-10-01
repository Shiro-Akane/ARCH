# Phase 2F-A completion report

Date: 2026-09-21. Scope: Core Configuration Contract + Identity Safety only.

## Baseline and integration

- Baseline: `studio-phase2e-b-v0.10.0` / `fb22178fe578b17120597633f427e38d2a2be582`.
- Branch: `studio/phase2f-ui-contract-integration`.
- Worktree: `/home/arch/projects/ARCH-phase2f-ui-contract-integration`.
- Only Core UI increment `5e96d4f004c9bd320fb232853d59b90006cdb0f2` was applied with no-commit cherry-pick; no conflict. No main merge/rebase or repeat A/B integration.
- Core files equal the approved increment. `cmake/tests/HostTests.cmake` retains the Studio baseline tests and adds exactly the upstream five-line configuration test registration. No other scientific edits.
- Governing documents are preserved as `PHASE2F_TARGET.md` and `PHASE2F_REQUIREMENTS.md`. A1–A7 were reviewed at implementation and closure boundaries.

## Core verification gate

CPU Debug (`-O0 -g1`), CUDA OFF, KLU OFF, independent `build-preview-audit`. Simulation oracle disabled with `ARCH_PREVIEW_SIMULATION_ORACLE=0`.

All eight scoped groups passed, total 213.10 seconds:

| Group | Result |
| --- | --- |
| configuration_api_contract | PASS |
| mainline_authority | PASS |
| preview_initial_conversion | PASS |
| preview_api_contract | PASS |
| preview_parameter_reads | PASS |
| preview_parameter_metadata | PASS |
| preview_sampling_limits | PASS |
| preview_cellular_2d | PASS |

Actual CLI checks confirmed 90 standard schema keys, Sod and CellularDet inspection, exact request ID / SHA-256 config revision, 1D code-unit and 2D CGS coordinate metadata, and field diagnostics for invalid integer fractions/exponents/overflow and float NaN/suffix/overflow. Inspection explicitly reports no Setup, EOS load, filesystem access or CUDA initialization. No simulation or CUDA baseline was run.

The final fixed CPU profile tracks 43 inputs, including the five new required configuration sources, policy descriptor/resolution definitions, Grid, physical constants and AMR definitions. `dependenciesComplete` remains false. Normal fixed-profile Build generated final provenance after the input list expansion:

- Build ID: `f8893d05-721a-4ba4-acf4-50e113d8ddbd`.
- Executable SHA-256: `8fc913ee9d588d71de8b8b6b52c1039f56e0abb0f1579a72d64e87e61c2c362e`.
- Size: 30,543,912 bytes; mtime: `2026-09-21T03:24:01.840Z`.
- Source HEAD during build remains the baseline; repository dirty state and explicit fingerprints record the integrated increment before checkpoint commit. No claim of complete dependency authority is made.

## A1–A7 delivery

| Requirement | Implementation / evidence |
| --- | --- |
| A1 controlled configuration adapter | `host/configuration.ts`, exact GET schema and POST inspection routes. Host owns binary, cwd, argv and environment. Browser sends only projectId, approved caseId, text and revision. Bounded process/output, SHA checks, pre/post Build checks and shared envelope validation. |
| A2 metadata isolation | Shared model/build scope in `state/coreParameters.tsx`; `configurationIdentity.ts` restricts defaults and bindings to project/case/build/binary. Effective Inspector values also require matching Working Copy text. Old graphs remain explicitly historical. |
| A3 source association | Persistent Sod/Cellular mapping changes with model. Existing read-only source viewer is explicitly labelled Project build source and explains that it is not necessarily the Preview model source. |
| A4 persistent identity | Model, full configuration name, source path and trusted Host parameter path remain visible above the workspace. Browser imports state no trusted Host path. |
| A5 advisory pairing | Sod/Cellular filename suspicion is a warning, never verification. Generic filenames remain association-unconfirmed. Preview and overwrite have separate dialogs. Overwrite shows exact target and Save As / Overwrite / Cancel, with text/model/project/path revalidation and existing external-file fingerprint protection. |
| A6 unified validation | `standardValidation.ts` is used through `parState.ts` for existing and newly inserted changes, including typed/pasted/scrubbed/programmatic edits. Invalid raw text stays editable and blocks validated actions. A compact omitted-key entry is available for insertion testing; complete grouping/catalog UX is reserved for B. |
| Three value layers | Inspector shows Working Copy text, Schema Default, Inspection Parsed Value before Setup, and Model effective value separately. Missing current Preview metadata is unavailable; inspection is not labelled simulation readiness or model-effective. |
| Late response protection | `inspectionRequests.ts` generation tickets plus exact response identity reject obsolete project/model/text/build responses, including returning to an earlier model/text. |

An approved additive Core change now supplies a unit for the fixed x3 coordinate. The existing 2D validator accepts null or a unit string and continues rejecting malformed coordinates/shapes/values; Inspector displays that unit. This is compatibility with the integrated contract, not new scientific scope.

## Automated regression

- `npm test`: **116/116 PASS**.
- `npm run test:host`: **45/45 PASS** (a subset of the full suite, not 45 additional distinct tests).
- `npm run lint`: PASS.
- `npm run typecheck`: PASS.
- `npm run build`: PASS.
- `git diff --check`: PASS; staged check also performed before checkpoint.

New tests cover actual upstream schema/inspection examples, malformed and stale identity, late generations, all required strict numeric token cases, existing versus inserted standard validation, byte-preserving single-key insertion, pairing neutrality, Host command injection/origin/protocol rejection, exact stdin identity, structured errors and build changes during inspection. Existing Mock, Plotfile, config lifecycle, Undo/Redo, preview cancellation/race and Build suites remain enabled. The build retains its existing large-bundle advisory; no bundling refactor was added.

## Desktop UAT

Agent-operated UAT used the actual production build on `127.0.0.1:4188`, fixed Host on loopback 4180, and only disposable `.local/uat` configurations. This is not a claim of user sign-off.

- Real Sod Preview: current result, x_pos metadata and marker present. Switching to Cellular removes Sod binding and model-read Inspector metadata; switching back restores only matching authority.
- Missing x_pos: default entry and marker appear after Sod success, both disappear for Cellular; safe insertion of 0.35 produces a dirty Working Copy without auto Save or Preview. Drag to approximately 0.449934 and one Undo restore 0.35.
- Existing `nblockx1=1.5` and inserted `ode_max_substeps=1e2` are retained as invalid text and block actions. Corrected `ode_max_substeps=42` is saved alone to ignored `1.par`; disk bytes verified. Schema Default 1 / parsed nblockx1 8 / model effective unavailable are visibly distinct.
- `CellularDet + Sod.par` produces a warning, exact overwrite target and independent Preview dialog. Generic `1.par` remains neutral.
- Full confirmation flow: valid Cellular config saved as disposable `Sod_pairing.par`, changed nblockx1 from 2 to 3, explicitly continued Preview, then Save still required a separate overwrite confirmation. Only that test target was overwritten; original Cellular fixture bytes remained unchanged.
- Rapid model changes do not restore old metadata. Deterministic request-generation tests additionally prove late inspection discard.
- Real new-Core Cellular 5x3 result remains supported: index 11 is i=1,j=2; x1=7.68, x2=10.666667. DENS/PRES/TEMP and x3 units display correctly. No Cellular marker.
- 1280x720 and 1920x1080 layouts inspected. At 800x720, document width equals viewport width, with scrollable panes and controls accessible. No dedicated mobile UX work.
- Found and fixed during UAT: Save As retained an omitted-parameter selector showing a default instead of the newly loaded explicit value. Loading a document now clears that selection; the entry also prefers actual Working Copy values.

## Checkpoint and explicit STOP

Checkpoint tag: `studio-phase2f-a-v0.11.0` (resolve the tag for the commit hash). No automatic push.

2F-A is complete. **STOP before 2F-B.** Phase 2F as a whole is not complete. B catalog/grouping/axis/unit/path-preflight work and C zoom/settings/README/overall UI-01–13 closure reports remain pending. The final `PHASE2F_COMPLETION_REPORT.md` and `PHASE2F_UI_REVIEW_CLOSURE_REPORT.md` must be produced only after those stages are actually completed.

No Phase 3, SSH, new scientific model, 3D Preview, actual AMR hierarchy, simulation monitoring, or Cellular performance optimization was implemented. Root `STATUS.md` is unchanged. Runtime caches, dist, node_modules, UAT files and screenshots are excluded from delivery.
