# Phase 1C UI completion report

Date: 2026-09-13
Branch: studio/phase1c-parameter-ui
Baseline: studio-phase1b-v0.3.0 / 9609c9d4e5f8ea289b19119a503c223f7d88d8a9
Checkpoint tag: studio-phase1c-v0.4.0 (resolve with git rev-parse).

## Delivered scope

M0-M7 implemented in target order: persistent four-block navigation, one selected Core editor, dimension-aware Grid presentation, paired bounds and boundaries, source metadata in tooltips, collapsed searchable Advanced/Custom fields, and dependency-free read-only loaded source. Mock and Real Config use the same navigation component. No scientific functionality added.

M8 automated gates passed: npm test (37/37), npm run lint, npm run typecheck, npm run build. The 34 existing tests remain, with three presentation tests added. Round-trip, custom preservation, invalid input, export and state regressions pass. The existing Vite large-chunk warning remains; bundle optimization was not part of this stage.

## Desktop before / after

Screenshots before and after were captured and inspected in the task conversation at 1280x720 and 1920x1080. They are conversation evidence, not image files committed to this repository.

| Measurement | Before | After |
| --- | --- | --- |
| Sod default parameter scroll content | 3280 px | 1063 px |
| Sidebar width at 1280x720 | 268 px | 268 px |
| Preview region at 1280x720 | 788x656 px | 788x656 px |
| Preview region at 1920x1080 | 1384x1016 px | 1384x1016 px |

Default scroll content fell approximately 68%; Preview area did not shrink. Browser checks at 2560x1440 and a 900 px narrow desktop window found no horizontal document overflow. No mobile-specific UX was added.

## Regression evidence

- Real Config: block switches and search do not dirty the document; source tooltip retains raw key, line and type; Raw view retains original source. Inactive-axis search works. Explicit 1D/2D/3D topology changes visibility without inserting absent keys.
- Working copy: edits, Invalid, disabled invalid export and Revert verified. Exact numeric input 0.7654321 synchronized with the slider; keyboard adjustment produced 0.766. Invalid 1.2 was not silently clamped.
- Mock: Preview generation and point Inspector verified in browser; existing state/Save/Revert tests pass.
- Plotfile: real sod-1d.h5, PRES enumeration, LineVis and sample 33 Inspector verified (time 0.15, x 0.5078125, value 0.3054751636143017). Existing error-handling tests pass; no Plotfile implementation changes.
- Save As: export tests pass and the unchanged handler displays Download requested. This session's in-app browser did not expose a download event or a new file in Downloads, so end-to-end disk delivery is NOT verified for this checkpoint. Phase 1B records a successful byte-for-byte download check; that historical evidence is not presented as a new Phase 1C check. Manual browser download confirmation remains outstanding.

## Scope and round-trip audit

All staged changes are under studio/. ARCH Core, parser, serialization, parameter schema, working-copy state, slider control, Plotfile implementation, package.json and lockfile are unchanged. The protected baseline STATUS record was not modified. No ARCH build, WSL baseline rerun, Setup/Init, Build/Run, AMR, SSH, 3D viewer or new dependency work occurred. Ignored node_modules, .local and dist are excluded.

## Limitations / skipped work

No implementation milestone was skipped; optional M7 needed no new dependency. Raw source shows the loaded original, not a second editable form or live export. Unknown/invalid dimensional topology keeps axes accessible conservatively; friendly labels and pairs are limited to verified keys. New browser download-to-disk confirmation remains unverified as described above. The checkpoint records implemented UI and successful automated gates, not unconditional external acceptance.

Stop at Phase 1C. No automatic push or next-stage work.
