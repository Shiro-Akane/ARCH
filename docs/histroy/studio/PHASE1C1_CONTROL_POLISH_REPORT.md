# Phase 1C.1 Control Polish Report

Baseline: studio-phase1c-v0.4.0 / d621ad9ae7c69e2dc565b1c84e712bbf0cbdf319
Branch: studio/phase1c1-control-polish
Checkpoint: studio-phase1c1-v0.4.1 (resolve tag for commit hash).

## Before / after

| Area | Phase 1C | Phase 1C.1 |
| --- | --- | --- |
| A. Open Config | Native file input and localized browser text | English button invokes hidden native picker; filename / no-file label |
| B. Runtime numeric | Native spinner and implicit integer stepping | Spinner hidden; direct exact entry, no implicit keyboard stepping; scientific notation retained |
| C. Enum fields | Geometry, solver, reconstruction, limiter, integrator, boundary, EOS and backend text | Source-backed selects, aliases included; original spelling or unknown raw option retained |
| D. Actions / status | Persistent request text, raw-line count, implementation footer and unavailable Save button | Saved/Dirty/Invalid; 6-second nonblocking feedback; Revert / Save As; raw counts in Raw view |

The source audit supplies the before comparison; after screenshots were inspected in the conversation at 1280x720 and 1920x1080. Preview dimensions remain 788x656 and 1384x1016 respectively. 2560x1440 and 900x720 DOM checks found no horizontal overflow. No dedicated mobile work.

## Contract evidence

- src/driver/dispatch/PolicyDescriptor.h: policy registrations and parse_geometry / parse_boundary / parse_compute_backend. Dropdowns use accepted input tokens and aliases. EOS accepts tabular; internal tabular3d/4d identifiers are not invented as input options.
- src/core/RuntimeParams.h: existing typed accessors and explicit refine_threshold [0,1]. Existing slider plus exact numeric retained; no other sliders inferred.
- src/io/ConfigParser.h GetBool: true / false, case-insensitive. Existing bool controls retained; use_nse remains true/false/auto select. Invalid bool text stays editable.
- network_name remains text because generated custom network registrations make a guessed closed list unsafe. Paths, expression coordinates and unknown Custom keys retain generic text controls. No guessed units, defaults, roles or ranges.
- UI-only controlContract.ts does not change parSchema or validation. Unknown tokens are not coerced; selection alone does not normalize loaded spelling.

## Validation

Final npm test: 38/38 pass (37 retained plus enum/raw round-trip regression). npm run lint, npm run typecheck and npm run build pass. Expected negative HDF5 diagnostic and existing Vite large-chunk warning remain.

Phase 0 Mock/state/Inspector tests, Phase 1A real Plotfile/LineVis/error handling tests, Phase 1B exact round-trip/custom/range/export/Invalid/Revert tests and Phase 1C presentation tests all pass. No need to rerun ARCH baseline or rebuild ARCH.

Browser checks: custom Open Config loads sod.par; Geometry/boundary dropdowns, HLLC/RK2 loaded spelling, selected Runtime editor, scientific notation 1.23456789e-8 unchanged by ArrowUp, Revert restores saved, refine_threshold 1.2 produces Invalid / one issue and disables Save As. Save As displays transient request feedback. Existing source tooltip and Advanced / Custom / Raw structure retained.

## Limits

Save As download-to-disk remains unconfirmed in this in-app browser, as at Phase 1C. The handler is unchanged except feedback text; export tests pass, but request feedback is not proof of a delivered file. This is an outstanding manual acceptance item, not a new claimed pass.

No full scientific compatibility validation is added by dropdowns: values describe accepted tokens, not whether a combination can run. Numeric controls have no guessed stepping; integer validity remains the existing validator. Raw view shows loaded source. No new dependencies, schema changes or application layout redesign. Before/after is documented above; no new baseline screenshot run was performed.

All changes limited to studio/. Parser, round-trip serializer, state validation, ARCH Core and Plotfile implementation unchanged. No node_modules, .local or dist in checkpoint. No implementation milestone skipped. Stop after local checkpoint; no push or next phase.
