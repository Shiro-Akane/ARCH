# Phase 2H-B — Capability Migration Completion Report

Date: 2026-09-27. **PASS. Local B checkpoint; STOP before 2H-C.**

## Baseline and scope

- Branch: studio/phase2h-mainline-capability-sync.
- Immutable A tag: studio-phase2h-a-v0.17.0.
- A commit: 01860f291d5348531eb84cbb6a505ca7a21a9d1b.
- Core authority remains 502eadcb33a9e2d3c8bd079a10dbdc8af208ef10.
- Active target: PHASE2H_TARGET.md, B sections 12–27; reread at start and closure.
- No Core tree sync, main merge, scientific changes, configure/build of ARCH,
  simulation, new field model or new AMR model in this phase.
- A binary SHA-256 unchanged: 53fdf7939cf4d68c806b206f1e7cf2f4d0e6d3724682a203af6e01250375fcf7.
- Existing successful Host Build ID: 819e0395-cf95-4138-856d-0fa489386973.
- A's 77 tracked inputs/Profile/Manifest remain authoritative for this unchanged
  binary; dependenciesComplete=false. No unnecessary rebuild for Studio-only edits.

## Delivered migration

### Dynamic catalog and data layers

Removed fixed 90-item schema/inspection/path-check acceptance rules. Payloads are
bounded for safety (4096 entries) rather than tied to a permanent parameter count.
Coordinate catalogs no longer require exactly nine entries. Duplicate identities
and malformed types/units remain rejected. The actual current binary supplies
configuration version "2", protocol envelope schemaVersion "1.0", 92 parameters.

Catalog, groups/search, field types, defaults, descriptions, applicability, units
and controls consume runtime schema. Current binary fixture is stored under
tests/fixtures/mainline-config-schema.json solely as test evidence. Production
imports no fixture and has no fixed 92/14 truth table. Tests also accept changed
catalog sizes and prove missing defaults serialize only on first explicit edit.

Schema Default, Inspection Parsed Value and matching Preview Model-read Value
remain separate. Existing buildId/binarySha256/project caches and revision gates
are retained and exercised by identity regression and real Host requests.

### Gravity and timing

The four keys gravity_boundary, gravity_rtol, gravity_atol and gravity_max_cycles
appear from the binary schema, including descriptions, units and applicability.
dt_init, dt_min and tstep_change_factor are searchable Runtime controls.
No frontend copy of their default values was added.

gravity_type=self is offered by current Core choices without the old global
unavailable assertion. The legitimate periodic GravityBox configuration passes
real inspection. A preliminary Sod+self test was correctly rejected because its
outflow boundaries were incompatible; the test was corrected to the upstream
GravityBox reference, with no product/Core workaround.

Self-gravity production support does not enable gravity potential/acceleration
plots. JENS remains unavailable: actual inspection rejects the unchanged JENS
token, the UI shows the Core error, and neither parser nor UI replaces it.

### Retired parameters and document safety

All five names are reserved migration identifiers from the current Core contract:

- enforce_mass_conservation
- burn_verbose_level
- ode_use_numerical_jac
- ode_freeze_jacobian
- timeintegrator

They are excluded from Custom editors, standard/alias destinations and old static
fallback controls. Canonical time_integrator is independent. Shared editPar rejects
retired edits/insertion, so programmatic GUI edit routes cannot bypass retirement.

Load preserves the exact raw document. Core RETIRED_PARAMETER errors are retained
and shown in an independent migration section; Save/Preview remain blocked while
configuration is invalid. Core may report only the first retired key, so remaining
rows honestly say they await a matching Core diagnostic.

Each explicit Remove retired parameter operation stores a removal intent in the
Working Copy and one existing EditHistory entry. It removes all assignments of that
key (including duplicates) only when serializing the edited copy. It preserves
unrelated text, comments and original newline conventions; original document.raw
remains unchanged. Undo restores the whole operation; Revert clears removals.
There is no load-time or save-time silent migration. User-triggered Save/Save As
persists the already-reviewed Working Copy. Continuous Preview behavior from 2G
remains unchanged after configuration becomes valid; no automatic Save was added.

### Runtime discovery, sources and Build

The selected binary supplies all 14 models through --list-cases. Existing dynamic
registry handling required no new model whitelist. Every model was inspected
through the real Studio Host. Each advertised sourceFile was resolved inside the
managed project, read through the bounded source reader, and matched to its
compiledSourceSha256 (14/14).

The UI updates selected-model source association from discovery. The separate
Project build source viewer keeps its explicit fixed-Build-Profile label; it
does not pretend that source is the selected model. Static fallback contract
evidence now points to src/core/config/RuntimeParams.h.

A's tracked input migration and missing-input rejection remain intact. Full
regression includes fixed Build security, stale detection and desktop project
discovery/path tests. Packaged Windows desktop requalification belongs to C.

## Real protocol / Session / AMR validation

All requested commands exercised using the unchanged A binary:

| Interface | Result |
| --- | --- |
| --config-schema | version 2 / 92 keys, real ConfigurationAdapter accepted |
| --inspect-config | valid Sod; all five retired keys rejected individually; each explicit removal valid; GravityBox self valid; JENS rejected |
| --list-cases | 14 cases, all source fingerprints verified |
| --inspect-case | all 14 via Studio WorkflowRunner/session transport, no static whitelist |
| --preview-capabilities | current runtime field/AMR extensions accepted |
| --preview-session | real ready/cold/warm, latest-only replacement, cancellation/restart |
| --preview-amr | real Sod complete + limited and CellularDet complete |

Sod cold/warm requests used the same process token and sequence 1→2.
Cellular non-square field sampling used shape [12,20].
Real queued requests finished with the latest request identity; cancelling an
active 256×256 request retained the previous success and a subsequent request
restarted successfully. Core request-limit closure/recycle was exercised by the
current targeted suite at the published limit; Host recycle/cleanup is also in
the Host regression.

Sod initial AMR: 8 leaves, complete.
Sod constrained budget: status limited, never called complete.
CellularDet initial AMR: 20 leaves, complete.
Resource tables succeeded for both. Matching actual field/mesh responses passed;
a changed configRevision and changed EOS source fingerprint were rejected.
No AMR cell field array was invented, and ordinary init samples retain their label.

Registered models: BurnGradient, BurnOneZone, CellularDet, CooperativeHotspots, DiffusionMode, ExternalGravity, Gaussian, GravityBox, JeansWave, RT, SNIaCoupled, Sedov, SmoothAdvection, Sod.
Only Sod and CellularDet retain full field/AMR capability.

## B browser UAT

Production build at localhost:4193, isolated Host at localhost:4180, using the
current worktree and A binary. This is B browser UAT, not C packaged desktop UAT.

- timeintegrator import: raw key/value retained, dedicated retired diagnostic,
  excluded from Custom/alias, Save/Preview blocked.
- Explicit removal → dirty; one Undo restored the original key; removal again
  restored valid inspection. User action Save As wrote an ignored disposable file.
- Remaining four retired keys loaded in that disposable file: separate rows,
  explicit removal and single Undo verified for each; subsequent explicit Save
  wrote no retired assignment while retaining all trailing comments.
- Reload disk version retained the migrated result; no retired region reappeared.
- Gravity panel displayed all four new schema controls with virtual defaults,
  descriptions, units and current applicability.
- GravityBox selection showed the runtime source path and inspection-only status;
  no initial AMR button and no new field Preview were enabled. Prior Sod output
  remained explicitly previous/stale, not a current GravityBox result.
- JENS input remained JENS with a visible Core validation issue, without fallback.
- 1280×720 and 1920×1080 viewport widths matched document scrollWidth; no horizontal
  overflow observed. No mobile-specific adaptation was implemented.
- Browser file chooser incurred an unusually long tool round trip; later cases
  used explicit reload of a disposable Host file. No product wait loop was added.

Original project .par files were not edited. UAT files and screenshots are ignored.
Screenshot: E:/.Codex/.ShiroAkane/.local/phase2h-b-uat/phase2h-b-uat.jpg.
Temporary browser tab and this phase's Host/preview servers were closed after UAT.

## Final regression

| Check | Result |
| --- | --- |
| npm test | 171/171 PASS |
| npm run test:host | 70/70 PASS |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS |
| Current Core targeted suite, -j1 | 13/13 PASS, 231.18 seconds |
| git diff --check | PASS |
| Core-owned tree vs upstream | unchanged / exact |
| A tag | unchanged |

Build retains the existing Vite large-chunk advisory; it does not fail the build
and was not expanded into a bundling/performance task. Core production-simulation
oracle remains opt-in and disabled. No CUDA or simulation claim is made.

Ignored evidence under studio/.local/phase2h:
b-tests.log, b-host-tests.log, b-lint.log, b-typecheck.log, b-build.log,
b-core-tests.log, b-runtime.log, b-extra.log, b/runtime.json,
b/extra-runtime.json and b/identity-check.json.

## Checkpoint and remaining boundary

Local annotated checkpoint: studio-phase2h-b-v0.18.0.
Resolve its peeled commit for exact delivery identity; A remains untouched.
No automatic push. **STOP.** Packaged Windows+WSL startup, full desktop smoke and
close/relaunch process qualification remain 2H-C, requiring separate authorization.
