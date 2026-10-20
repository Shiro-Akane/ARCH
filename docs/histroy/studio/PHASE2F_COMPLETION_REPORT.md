# Phase 2F completion report

Date: 2026-09-21. Scope: authorized A → B → C, ending before Phase 3.

## Checkpoints and scope

- A: `studio-phase2f-a-v0.11.0` / `7b62bbe758bfee2d0dcebe4ba9fbbe2dbc01df59`.
- B: `studio-phase2f-b-v0.12.0` / `6b0b10b8d4a1a4fd473853f2784a7d59cdfbdce5`.
- C branch: `studio/phase2f-plot-presentation`, created from clean B.
- Final checkpoint: `studio-phase2f-v0.13.0`. Resolve `git rev-parse studio-phase2f-v0.13.0^{commit}` for the final commit containing this report.
- Core UI contract remains `5e96d4f004c9bd320fb232853d59b90006cdb0f2`, already integrated in A. No main merge, duplicated Core integration, A/B tag movement, or automatic push.

C changes are confined to `studio/`: real plot interaction/presentation, tests, README and status/reports. No scientific Core, parser/round-trip, Host execution contract or root STATUS changes. No ARCH rebuild, CUDA/WSL baseline repeat, simulation, output generation or Phase 3.

## UI-06 reproduction and resolution before UI-07

Initial B production checks exposed a line figure occupying only a 150px inner canvas inside a larger container, and 2D domain-exterior hits incorrectly selecting the nearest boundary sample. Source inspection also showed the Sod marker subscribing to the same mutable camera object, so its own visible-domain calculation did not reliably re-render. These were corrected and validated first with 9 focused tests, a production build, Sod and Cellular 64×32 / shock_dir 0 and 1. The milestone boundary is recorded in `studio/STATUS.md`.

Installed HeatmapVis 17.0.0 does not consume spatial axis `scaleType`: simply adding a Log selector would relabel a linear image incorrectly. The final real-only renderer therefore uses one explicit forward/inverse physical projection for Canvas data, SVG axes, Sod marker, selection and hit testing. Camera-derived mixed coordinate paths are removed from Real IC rendering. Plotfile and Mock keep their existing renderers.

Final implementation:

- `PhysicalPlot.tsx`: full-size real rendering, pointer-relative coordinates, bounded zoom/pan, exact Core region Fit, raw selected sample marker and physical inverse hit testing. Zoom does not edit Working Copy or issue Preview.
- `plotPresentation.ts`: independently tested linear/log projection, domain validation, view transforms, physical ranges and clipping.
- `RealInitPreviewProvider.ts`: reject nonfinite/domain-exterior grid hits; preserve `index=j*Nx+i` and sample arrays.
- Marker drag uses the same inverse projection even on Log X; candidate movement is local, mouseup commits once, Escape/blur/cancel discard. Out-of-Core-range candidates do not commit and show a message. One Undo restores the prior value.

## UI-07 and UI-11

Coordinate Axes and Field Values are separate settings groups. X and 2D Y have independent Linear/Log and Auto/Manual bounds. Field scale/range, lower/upper clipping and Viridis/Hot are display-only. Manual values are original physical values. Invalid/overflowing bounds and nonpositive Log data produce an explicit message and withhold the plot; Linear/Reset recovers. No abs, epsilon or silent invalid-value filtering.

Viridis uses the existing library interpolator. Hot is explicitly implemented as black→red→yellow→white because the installed library does not publish Hot. Both have gradient previews. 2D clipping saturates at colorbar endpoints; 1D clipped portions have edge indicators and persistent threshold text. Inspector still reads original Core field arrays. Selected markers use a contrasting outline on either light or dark colors.

Fit returns the full authoritative coordinate domain and Linear spatial axes; field manual range returns Auto, while intentional field clipping remains until Field Reset. Settings are local to the displayed field and reset on field change. These are presentation semantics, not scientific edits.

README now leads with current Sod/binding/Cellular/config-schema/inspect-config/Build/Save behavior, the actual fixed CPU profile, Node/npm environment and startup commands. Host Preview timeout is 120s, UI polling 135s, individual HTTP timeout 15s. Historical phase limitations are explicitly below the current instructions.

## Final automated verification

| Check | Result |
|---|---|
| Core scoped groups | 8/8 PASS, 207.88s; `ARCH_PREVIEW_SIMULATION_ORACLE=0` |
| npm test | 128/128 PASS |
| npm run test:host | 46/46 PASS |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS |
| git diff --check / staged diff | PASS |

Core groups: configuration_api_contract, mainline_authority, preview_initial_conversion, preview_api_contract, preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits, preview_cellular_2d. Existing CPU binary was reused: SHA-256 `8fc913ee9d588d71de8b8b6b52c1039f56e0abb0f1579a72d64e87e61c2c362e`, 30543912 bytes. No Core recompile was required by C.

New tests cover all four 1D scale combinations, both real Core non-square fixtures under independent spatial transforms, forward/inverse mapping after pan/zoom, exterior hits, exact authoritative domains, invalid/overflow ranges, Log rejection, independent clipping and unchanged raw arrays. Existing save lifecycle, round-trip, security, Build, cancel/race/failure retention, metadata isolation, catalog, path, Mock and Plotfile tests remain enabled. The pre-existing large-bundle advisory remains; no bundling refactor was added.

## Desktop production UAT

Agent-operated production UAT used a dedicated 127.0.0.1:4188 frontend and Host 4180, with disposable ignored `.local/uat` config only. This records agent verification, not an additional claim of user sign-off.

| Scenario | Evidence / outcome |
|---|---|
| Sod discontinuity / zoom / pan / Fit | Same physical projection for curve and x_pos; Fit restores x1 [0,1]. Fixed sample 200 remains x1=0.39160156, DENS=1. |
| Four 1D scale combinations | Linear/Linear, Linear/Log, Log/Linear and Log/Log exercised. Log X uses explicit display range [0.001,1]; zero-inclusive Auto range visibly rejects. |
| Log marker / Undo | Drag x_pos 0.5→0.600275358047756 on Log X; pending Working Copy only. One Ctrl+Z restored 0.5 and the matching current preview after inspection. No Save or Preview during drag. |
| Invalid Log | Zero X velocity shows sample-0 zero-value error; Reset/Linear restores the plot. |
| Range / clipping | Manual bounds, independent lower/upper thresholds (Sod .2/.8; Cellular 2e7/3e7), Hot/Viridis and resets exercised. Inspector retained DENS=1 / original Cellular values. |
| Cellular non-square | 64×32, shape [32,64], x1-fastest, shock_dir=0 and 1. index 500=(52,7), coordinates (21,3); index 200=(8,3), coordinates (3.4,1.4). Direction-specific velocity/field orientation retained. |
| 2D independent scales | X Log / Y Log / field Log combined; positive manual spatial bounds. Axis and color scale labels independent. Selected point survives transform; clicking its displayed center reselects the same index. |
| Final 2D Fit | x1 [0,25.6], x2 [0,12.8] restored exactly; same selected raw sample. No Cellular editable marker. |
| Default→explicit→Preview→Save/reopen | Omitted ode_rtol default .0001 edited to .0002; successful unsaved Sod Preview, explicit Save and disk reload retained only the inserted key. |
| Grid layout | Rechecked 1D→2D→3D→2D→1D; original 0/off values restore Saved/current state. No 3D Preview performed. |
| Model identity | Switching Sod/Cellular updates source/model and removes incompatible binding while prior success is retained honestly. |
| Legacy views | Archived Cellular result and Mock generation checked; Plotfile parser/data/render regressions PASS and legacy renderers unchanged. |
| Layout | 1920×1080 and 1280×720 desktop; 800×720 narrow window has scrollable accessible settings and document width equal to viewport. No mobile-specific UX scope. |

A/B detailed UAT evidence remains in `PHASE2F_A_COMPLETION_REPORT.md` and `PHASE2F_B_COMPLETION_REPORT.md`; accepted catalog/identity/path baselines were not unnecessarily repeated. Their automatic regressions were run in the final suite. In particular all path states, strict validation, three-layer value semantics, stale inspection rejection and byte-safe insertion remain covered.

## Delivery / STOP

See `PHASE2F_UI_REVIEW_CLOSURE_REPORT.md` for UI-01–UI-13 closure. Runtime dist, node_modules, build trees, `.local`, screenshots and temporary configs are excluded from Git. Dedicated UAT services are stopped after verification. Local checkpoint only; no push requested for C.

Deferred boundaries remain: Cellular editable marker, real AMR hierarchy, 3D Preview, new models, full dependency authority, arbitrary C++ auto-UI, SSH/cluster/scheduler, simulation monitoring, native Windows Host qualification and generic external editor launch. **STOP after the final Phase 2F checkpoint. No Phase 3.**
