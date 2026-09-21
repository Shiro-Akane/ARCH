# Phase 2F UI review closure — UI-01 through UI-13

Date: 2026-09-21. Final checkpoint: `studio-phase2f-v0.13.0`. Branch: `studio/phase2f-plot-presentation`.

All thirteen items are **Implemented** within the authorized desktop, Sod 1D / CellularDet 2D scope. This is implementation and agent UAT evidence; it does not expand Core scientific capabilities. A/B accepted checkpoints remain unchanged.

| ID | Status | Implementation evidence | Automated evidence | UAT evidence | Remaining limit |
|---|---|---|---|---|---|
| UI-01 Network ODE / Advanced | Implemented | StandardCatalog Core groups and Advanced ODE entries | parameter-catalog, configuration-contract | B: all requested ODE keys/defaults; C: omitted ode_rtol edit/Preview/save/reopen | Applicability is Core-provided; no new solver |
| UI-02 90-key catalog | Implemented | config-schema + Working Copy + inspect-config + matching metadata; aliases share destination | 90-key coverage and byte-preserving insertion | B: all 90 searched individually; C single-key insertion confirmed | 89 editors because alias shares one; custom completeness not claimed |
| UI-03 Gravity / Diffusion | Implemented | Core-defined groups/options/applicability/field diagnostics | catalog/configuration/validation suites | B: unavailable self gravity, Helmholtz diffusion error retained | Does not enable unavailable physics |
| UI-04 Coordinates | Implemented | Core coordinate catalog and valid-layout state | all nine Core conventions | B all geometries; C dimension transitions | No inferred coordinate conventions |
| UI-05 Units | Implemented | authoritative status/coordinate units and matching Preview units | unit contracts, catalog and real-grid fixtures | B CGS/code_length/rad/custom unknown; C real Inspector values | Unknown/mixed/model-dependent remain explicit |
| UI-06 Zoom / Pan / Fit | Implemented | PhysicalPlot shared Canvas/SVG forward/inverse projection; exterior-hit rejection; Core full-domain Fit | plot-presentation + real-grid + line | C Sod and 64×32 Cellular shock_dir 0/1, fixed point, click after transforms, Fit | Uniform init-samples; no real AMR hierarchy |
| UI-07 Plot controls | Implemented | PlotControls, plotPresentation, plotColors; independent Coordinate Axes and Field Values | four 1D combos, independent 2D transforms, Log/range/clipping invalid cases, arrays unchanged | C four scale combos, manual ranges, Hot/Viridis, clipping, zero-value error/recovery, raw Inspector retained | Real IC only; field change resets settings; Log requires positive displayed domain/data |
| UI-08 Persistent identity | Implemented | current model/config/source/path identity and separate pairing/overwrite flows | identity, config lifecycle, host-save-as | A mismatched Sod filename with Cellular, generic neutral file and independent overwrite confirmation; C identity retained during display controls | Filename never verifies scientific pairing |
| UI-09 Old-model isolation | Implemented | project/case/build/revision matching before metadata/binding use | core-parameters, request generation and retention | A Sod→Cellular→Sod and late result; C switch removes incompatible marker | Previous successful plot remains history, not current authority |
| UI-10 Source / Model | Implemented | registered model→source association | source/configuration identity tests | A/C model switch updates Sod.cpp / Cellular.cpp | configured association, not arbitrary registry verification |
| UI-11 Current docs | Implemented | README current startup first; historical instructions explicitly separated | startup command/profile/timeout inspected against Host code; production build launched | C Node24/npm11, loopback Host and production preview | Fixed local deployment root; no automatic configure |
| UI-12 Stable axis blocks | Implemented | peer x1/x2/x3 blocks; valid topology snapshot | all conventions and transient invalid states | B full invalid-input matrix; C 1D→2D→3D→2D→1D | 3D config editing does not enable 3D Preview |
| UI-13 Validation / path preflight | Implemented | single schema-aware validation and Host schema-path checks with revision isolation | strict int/float/expression, host-path-preflight, lifecycle/security | A/B invalid existing/new values and all path states; C saved explicit standard key | Before-Setup inspection is not simulation validation; Host-offline is Not checked |

## Final verification

- Core: 8/8 scoped groups PASS, simulation oracle disabled, no rebuild in C.
- Studio: 128/128 PASS; Host: 46/46 PASS.
- lint, typecheck, production build and diff checks PASS.
- Desktop production UAT: 1920×1080 / 1280×720; narrow 800×720 only for severe overflow/access issues.
- A/B reports are retained as accepted detailed evidence. C report records fresh display, config lifecycle and layout checks and the exact raw samples used.
- Existing large bundle warning remains advisory. No test disabling or baseline reconfiguration used to obtain PASS.

## Semantic and scope boundaries

Schema Default ≠ Inspection Parsed Value ≠ Preview Effective / Model-read Value. Display scaling/clipping never writes `.par`, changes configRevision, triggers Preview or modifies response/Inspector values. Only an intentional Sod marker release edits the Working Copy; it remains a separate parameter operation with one Undo.

Deferred features are unchanged: Cellular marker, real AMR hierarchy, 3D Preview, new models, full dependency graph, generic C++ UI, SSH/scheduler/cluster, simulation monitoring, native Windows Host qualification and external editor launch. None are silently represented as implemented by these thirteen closure entries.

Final checkpoint is local. **STOP; do not enter Phase 3 or automatically push.**
