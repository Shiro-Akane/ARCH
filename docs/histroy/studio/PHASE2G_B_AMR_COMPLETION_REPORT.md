# Phase 2G-B — Case Discovery + Initial AMR completion report

## Checkpoint and scope
- Baseline: studio-phase2g-a-v0.14.0, 13345ba4d1b7379ff5481bf9ca4f2fc9156aee7c.
- Branch: studio/phase2g-initial-amr.
- Completion checkpoint: studio-phase2g-b-v0.15.0 (resolve annotated tag for exact final commit).
- Target: PHASE2G_TARGET.md, B1–B11, rechecked at implementation and final validation boundaries.
- All delivery changes are under studio/. A tag and scientific Core unchanged. No ARCH rebuild, simulation, scientific output, main merge, remote push or Desktop Launcher work.
- Existing A CPU Debug binary and successful Build eaea1d3d are reused. 75 explicit tracked inputs match; complete dependency freshness remains unknown.

## Implementation / acceptance matrix
| Target | Result and evidence |
|---|---|
| B1 discovery | PASS. Host executes selected binary --list-cases and capabilities with fixed executable/cwd/env. Frontend lists 11 registered models, source association, distinct field/AMR capabilities and build identity. No filename-derived registry. |
| B2 inspect-case | PASS. Same owned session handles inspection, actual reads/default/explicit/effective/source, unit evidence, raw Init probes and diagnostics. All 11 real models passed Host smoke. Gaussian UI explicitly disallows full field Preview while allowing inspection and editing. |
| B3 AMR UX | PASS. Stable Grid AMR group consumes Core presentation; regrid_interval stays in AMR Advanced. Indicator availability/species resolution is Core data. No defaults are written until edited. |
| B4 resources | PASS. Full-domain resource table displays levels, blocks, active cells, base/species storage, pool capacity, overflow and assumptions. Null is unavailable, not zero; unsafe browser integers are visibly approximate. No OOM guarantee. |
| B5 actual AMR | PASS. Explicit unsaved Sod1D / CellularDet2D Cartesian requests through the owned session. Preview budget is separate from max_blocks, bounded to Core v1 1–1024 blocks/16–256 MiB; defaults512/128. |
| B6 response | PASS. Strict validators check logical keys/indexes, bounds/shape/spacing/counts, balanced-snapshot status and request identity. Inspector uses logicalKey, never pool index. |
| B7 limited | PASS. Real Sod budget6: limited last-balanced snapshot5leaves, completedPasses1. Budget1: snapshot none, root-grid-exceeds-working-capacity, no misleading zero-block completion or overlay. |
| B8 rendering | PASS. 1D intervals/ticks, 2D outlines/cells, level colors and visibility. Existing physical projection is shared by field and hierarchy; pixel density affects drawing only. Zoom/pan/Fit do not request AMR or change data. |
| B9 Inspector | PASS. Logical key, level/index, bounds, cellShape and spacing displayed. Explicitly no AMR cell field arrays. Existing field Inspector remains Init sample Inspector. |
| B10 identity | PASS. Overlay requires project/case/configRevision/build/binary/dimension plus authoritative EOS match. Null non-ideal EOS fingerprint cannot match. Previous mismatched hierarchy appears separately with stale label. |
| B11 UAT/regression | PASS. Real production UI + real CPU Core evidence and automated fault-injection cases below. |

## Real execution and desktop UAT
Production preview: http://127.0.0.1:4190/, Host127.0.0.1:4180. Node24.21.0/npm11.19.0.
Agent-operated desktop UAT, not a claim of separate user acceptance. 1280×720 and1920×1080 checked; no document horizontal overflow. Final bundle index-DGs8xCf7.js; resource tables and metadata remain accessible through panel scrolling.

- Actual registry: BurnGradient, BurnOneZone, CellularDet, CooperativeHotspots, DiffusionMode, ExternalGravity, Gaussian, RT, Sedov, SmoothAdvection, Sod.
- Real Host smoke inspected all11 against model-appropriate configurations from Core scoped tests; each completed and validated. Local evidence: ignored .local/b-host-smoke.log and .local/b-*.json.
- Sod actual AMR: complete12leaves, L0:1/L1:3/L2:4/L3:4. Selected3:13:0:0: lower0.40625, upper0.4375, cellShape16, spacing0.001953125cm. Limited and no-snapshot cases as above.
- Cellular shock_dir0: real field96×48, shape[48,96], x1-fastest; physical domain25.6×12.8. AMR20leaves L1:4/L2:16. Click selected1:2:0:0: lower[12.8,0], upper[19.2,6.4], shape16×16, spacing[.4,.4]cm. Fine/coarse boundaries align with physical axes. Geometry regression verifies the authoritative reference's neighboring 2:1 transitions.
- Level2 off hides its16 blocks while total hierarchy remains20. Zoom transforms field/outline/cells together; Fit restores full domain. Geometry tests verify visible-cell LOD and physical hit testing without mutation.
- Changing shock_dir0→1 immediately detached old AMR. New96×48 field became Current with zero old AMR overlays on that field; prior hierarchy was separately labeled. Explicit new AMR returned32L2leaves with matching identity.
- Cancel clicked during a new real AMR request; UI reported cancellation and retained previous32leaf success. Controlled process tests additionally verify worker termination/reap and new-process recovery, including late-result and timeout cases.
- Gaussian, an inspection-only model, returned actual default amp0.5 with explicit unavailable/source default-missing-key; amp stayed absent until UI edit0.6. Working Copy became Dirty, inspection stale, and explicit new inspection returned explicit/effective0.6. No Save and no full field request.
- Resource-only 3D configuration(root4×1×1, maxLevel15) returned levels0–15 without constructing hierarchy. L14/L15 reported overflow with unavailable storage; high integers displayed approximation; species-not-initialized stayed unknown. Invalid maxLevel60 was rejected by existing schema before request.
- Three-layer semantics remain distinct. Editing inspection fields does not turn last observed values into current evidence.
- Runtime UAT used only initialization/resource operations; no simulation execution or scientific outputs.

## Final automated checks
- npm test: **160/160 PASS**, 0 skipped/failed.
- npm run test:host: **67/67 PASS** (subset of full suite, not added to160).
- npm run lint: PASS.
- npm run typecheck: PASS.
- npm run build: PASS; existing large-bundle advisory remains.
- git diff --check: PASS.
- Core scoped groups: **18/18 PASS**, 215.99s, ARCH_PREVIEW_SIMULATION_ORACLE=0; existing binary, no rebuild.
- Logs: ignored studio/.local/b-final-{test,host,lint,typecheck,build,core}.log.

Core groups: preview_session_contract, preview_verified_resources, preview_exact_sample_cache, tabular_eos_ideal_gas, preview_initial_conversion, preview_api_contract, configuration_api_contract, preview_parameter_reads, preview_parameter_metadata, preview_sampling_limits, preview_cellular_2d, mainline_authority, ui_expansion_contract, refinement_indicator_math, amr_operation_plans, topology_transaction, initialization_probe, case_inspection_contract.

Added B tests cover discovery capability distinctions, real Core example validation, malformed geometry/counts, limited/no-snapshot, resource overflow, physical hit testing/LOD/2:1, identity and EOS mismatch, same-worker reuse, cancel/reap, timeout/late identity rejection, build exclusion and exact HTTP request/origin/protocol boundaries. Timeout/malformed/EOS-mismatch faults use controlled automated fixtures; they are not misrepresented as a manual real-EOS failure.

Regression found and corrected the no-discovery adapter fallback allowing unknown model past request validation. It now permits only its Host profile case; production uses selected-binary registry. Final full suite passed after the fix. Final whitespace-only correction followed build; diff check passed.

## Limitations retained honestly
- Registered case does not imply complete field/AMR support. Only Sod1D/CellularDet2D have actual initial hierarchy.
- Init probes and independent sampled fields are not AMR cell field values.
- Limited means incomplete. Resource estimates are not simulation feasibility or OOM predictions.
- Build tracks explicit inputs; complete dependency coverage remains unknown.
- Browser numbers beyond safe integer precision are approximate. Core int64 overflow remains unavailable.
- No new numerical/scientific logic or arbitrary browser commands; no AMR evolution, Cellular editing marker, MPI layout or launcher.

## STOP
B complete. Preserve A checkpoint. Create only studio-phase2g-b-v0.15.0 and stop. Phase2G-C requires new user authorization.
