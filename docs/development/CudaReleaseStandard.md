# CUDA release standard

Audience: contributors and release reviewers. This is the single maintained
acceptance plan, updated in place. User-facing capabilities and the combined
result summary belong to [Validation](../../validation/README.md), the
[Reference](../Reference.md) and the [backend guide](../CudaBackendStatus.md).
Git retains the execution history; this document is not a chronological diary.

Last scope update: 2026-09-08 JST. Preserve the shared worktree and unrelated
changes. Commit, push, publication and external contact require the owner's
explicit authorization for those actions.

## Active execution contract

The required scientific release profile is completely verified on its recorded source.
Specifically, the [acceptance index](../../validation/backend/results/final-acceptance-20260907/release-73a9cf50/index-final-930/README.md)
shows 41 required passes, zero required failures or pending entries, and only two
owner-deferred large-network workloads. The exact source identity for this milestone is
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`.

The current work prepares the verified CUDA branch for integration into `main`,
the recommended checkout for users. Remove unused dispatch entry points, correct
coordinate labels and capability descriptions, and align build presets and
linker checks with the supported build workflow. Mathematical and physical
implementations remain unchanged. Keep one implementation per responsibility
and a short source-download instruction for new users. The existing
[maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md)
and [source-publication record](../../validation/backend/results/source-publication-20260908/README.md)
are updated in place.

Verified on the current integration cleanup:

- All 289 Python tooling and architecture controls with zero skips, including
  the configure-helper controls; the shared architecture audit also passes.
- The optimized local CUDA build with cuDSS, KLU and representative generated
  packages; all 98 configured Release tests with zero skips and ten
  development-smoke lanes. The regression inventory includes the shared
  checkpoint, dispatch, scheduler, boundary, AMR-plan and residency controls.
- Normal smooth and burning restart: each completes twelve application runs
  and nine strict comparisons across all four backend directions.
- Burning-restart memcheck and racecheck: each completes the same twelve runs
  and nine comparisons, including six instrumented CUDA processes with complete
  clean reports. Final source, binary and test-artifact identity checks pass.

The completed runtime campaign records source
`abd02d1eab0887e366ebf24506882efb1a49166e2607ff47ba1a89937107cedf`.
It checks the complete ARCH checkpoint reader, unified policy-ID factories,
required hydro launch context and supported setup APIs on the current build.
All 281 normalized compiler commands retain their previous arguments; no
optimization flag or mathematical implementation changes. The 59.005-second
incremental build is not the separately recorded 2,213.445-second cold build.
These maintenance checks do not rerun the full
independent scientific validation profile or change its recorded identity.

Complete these checks on the final changes before integrating `main`:

- [x] Remove unused selection chains and their nullable compatibility defaults;
  verify required dispatch context and shared coordinate naming. Update presets,
  final-linker IPO checks and user-facing instructions without changing physics.
- [x] Configure and build the optimized CUDA application and its configured
  regression targets from the final maintained source. Include cuDSS and the
  representative generated packages in the tested profile; retain actual
  compiler, dependency and artifact identities.
- [x] Run the complete configured CUDA-enabled regression inventory, with no
  unexplained skips. Also rerun the unified Python tooling suite and architecture
  audit after the directory changes. A CPU-only or partial build is not this gate.
- [x] Run the existing CPU/CUDA AMR development smoke and strict smooth/burning
  restart checks on that same build. Preserve all four backend directions,
  native fields, controller/output phase checks and the existing budgets.
- [x] Complete burning-restart memcheck and racecheck with the same strict
  comparisons, then verify the campaign's final source, executable and input
  identities. Preserve incomplete or failed attempts rather than inferring a pass.
- [x] Observe local CPU/CUDA speed with AMR enabled on the same two-dimensional
  Sedov configuration at two declared problem sizes. Repeat both backend runs,
  check field consistency and record the mesh sizes and AMR activity alongside
  end-to-end wall time and the existing runtime AMR regrid-transaction wall
  measurements. The application has no separate solver timer in this profile;
  do not report solver/kernel time or label end-to-end minus regrid time as
  hydrodynamics time. This measures the tested workloads;
  a speedup is not an acceptance threshold, and physics, tolerances or case
  selection must not be tuned to guarantee one. It does not replace scientific
  acceptance. The completed two-scale comparison passes its consistency,
  conservation, dynamic-topology and identity checks. CUDA takes 3.30 and 3.43
  times the eight-thread CPU end-to-end time on the measured workloads; the
  owner accepts functional-parity integration with these results disclosed.
- Update the effective maintenance and affected Validation records in
  place with the actual new results, then check user commands, local links and
  source-delivery inventory. This gate reviews the delivery selection; actual
  review outcomes are established by the maintenance `freeze-review.json` and
  source-publication archive manifest. Git integration is established by refs
  and command receipts, not a document checkbox.

The previous numerical reports retain their actual source and binary identities.
The owner has authorized the verified commit and main-branch integration. No
large-machine campaign, audit150/audit200 build or third-party contact is part
of this work. Required independent fixtures
stay with their test owners; do not add another report tree or broaden the
refactor while completing the local checks.

The reviewed technical results are ready for owner-authorized fast-forward
integration. Complete the final document/inventory checks and verify remote
ancestry before committing and pushing the selected main/frozen branch updates.
Runtime performance optimization is a separate follow-up; the measured CUDA
slowdown does not reopen the passed functional acceptance or authorize kernel
changes in this integration.

## Non-negotiable contract

- Maintain one mathematical and physical implementation for CPU and CUDA.
  Backend storage, views, kernels, launches and native linear providers may
  differ when their execution responsibilities require it.
- Preserve registry, factory and resolved-policy selection. No case-name,
  isotope-count exception, GPU-model branch or copied backend formula may be
  used to repair an individual validation case.
- Supported production physics must be available on both backends within the
  documented interface requirements. Built-in NSE, external gravity and real
  generated networks are in scope. Self-gravity, Jeans indicators, WENO5,
  custom-network NSE, MPI and multi-GPU execution are separate extensions,
  not incomplete promises of this release profile.
- Reject unsupported explicit selections. Do not hide a CUDA failure by
  continuing on CPU after construction or by changing a requested solver.
  CPU KLU and GPU cuDSS remain distinct native providers.
- Auto selects by the total ODE equation count, including temperature and
  optional auxiliary state: at most 31 equations use the compact DenseLU
  route; larger Auto selections use the supported sparse
  provider. Explicit incompatible backend/provider requests must fail clearly.
- Preserve physical budgets, field coverage, independent references and failed
  stage no-commit behavior. Do not widen tolerances, mask fields or sample new
  expected values from the implementation merely to obtain a pass.
- Diagnose the first CPU/CUDA divergence before changing shared physics.
  Independently proven shared defects are fixed once and checked independently;
  CPU output is a reference path, not proof that every shared formula is exact.
- Use common geometric measures, physical CFL lengths and source definitions.
  Preserve the documented two-dimensional spherical polar convention.
  Coordinate corrections must cover all affected consumers on both backends.
- Central constants are organized by discipline, with units and provenance.
  Derived values reuse their fundamentals; named mathematical constants use
  the standard facility where available. Network fits, masses and table data
  retain their declared data conventions rather than being silently rescaled.
- Split by readable responsibility, following the existing policy-specific
  dispatch structure. Review headers and implementation units together.
  Generated instantiation wrappers bind types; they do not contain copied
  reaction, EOS or ODE mathematics.
- The shared reader and writer implement the ARCH checkpoint contract. Required
  scientific identity, ENUC, controller state and native composition must be
  present and consistent. Fresh-start `RunState` initialization is a separate
  operation, not incomplete-checkpoint recovery.

## Five ordered work packages

Keep this order when a changed contract reopens work: shared correctness first,
then implementation/build structure, complete validation, sustained/resource
checks, and final optimization/documentation. A smoke pass or a document edit
cannot close a scientific gate.

The statuses below describe the accepted release profile. Current maintenance
checks and remaining delivery actions are identified separately above; no
checkbox substitutes one source identity for another.

### 1. Shared correctness and actual feature gaps

Status: complete for the accepted profile. Current interface cleanup also passes
its focused controls.

Acceptance conditions:

- [x] Shared refinement/noise policy covers zero fields, composition closure
  roundoff, real trace gradients, selected fields and unit scaling.
- [x] Common spherical metrics, CFL lengths, geometric sources, diffusion,
  diagnostics, AMR transfers and reflux pass independent geometry, equilibrium
  and convergence checks.
- [x] CUDA external gravity uses the CPU source-term authority.
- [x] Generated weak networks use explicit shared mathematical views and
  backend-specific immutable table owners, without copied interpolation.
- [x] Weak-loss derivatives and accepted source energy obey the shared first
  law before physical trajectories are qualified.
- [x] Built-in NSE preserves independently certified equilibria and responds
  to changed density, temperature and composition.
- [x] Constants and eligible non-network consumers use the central disciplinary
  definitions, with explicit data-contract exceptions.
- [x] The [ownership map](ImplementationOwnership.md) covers each maintained
  mathematical, control, memory and evidence responsibility.

Evidence is organized by the [AMR](../../validation/amr/README.md),
[hydro](../../validation/hydro/README.md), [burn](../../validation/burn/README.md),
[EOS](../../validation/eos/README.md), [gravity](../../validation/gravity/README.md)
and [generated-network](../../validation/network/README.md) summaries.
Backend parity and independent scientific accuracy remain separate criteria.

### 2. Generated networks, build structure and evidence tooling

Status: complete for representative local workloads. Full audit150/audit200
qualification is owner-deferred to a larger validation system.

Acceptance conditions:

- [x] Costly translation units and include dependencies have been reviewed
  together. Generic ODE/math headers do not import unrelated network catalogues;
  direct consumers include their own definitions. Include guards do not excuse
  unnecessary template expansion.
- [x] Representative real generated CUDA packages compile and pass RHS,
  Jacobian, temperature-derivative, physical-trajectory and application checks.
  Local coverage includes audit31 and a compact weak network.
- [x] All three ODEs exercise real CPU KLU / GPU cuDSS production routes.
  Manufactured matrices alone do not replace generated-network trajectories.
- [x] Dense/sparse equation boundaries, original-matrix residuals, factor reuse,
  singular/EOS failures, rejected-step rollback and no-commit controls pass.
- [x] Canonical Cartesian, curved, uniform, generated-network and restart
  manifests use the shared execution and qualification helpers.
  Missing cases, inconsistent policies or identities fail the gate.
- [x] Registered external math, package metadata and actually linked KLU/cuDSS
  dependencies are fingerprinted, not identified only by path.
- [x] Optimized cold/no-op/incremental core builds and safe concurrency have
  measured time, owned-process RSS, available RAM and swap observations.

The [generated-network summary](../../validation/network/README.md) links the
accepted application, weak and real sparse measurements. CMake checks the
automatically generated [package contract](../../src/physics/network/custom/README.md)
before registration. Accepted host-only packages remain CPU-eligible; CUDA also
requires the device-math contract and `device_callable_math=true`. These are
interface requirements, not a promise that every arbitrary network has been
physically qualified.

The [core-build reference](../../validation/backend/results/cold-core-first-law-20260907/release-909/README.md)
retains equivalent optimized commands across five phases:

| Measured work | Wall time |
| --- | ---: |
| Cold core `ARCH` target | 2,213.445 s |
| No-op build | 1.012 s |
| One compact Ideal/iso7 route plus optimized relink | 27.323 s |
| Identical Ideal/Helm route pair, serial | 45.320 s |
| The same pair with the measured parallel configuration | 31.312 s |

This core target includes built-ins, audit31, the weak package and required
dependencies; test executables are outside the cold-target measurement.
The compact incremental result is not a shared-mathematical-header rebuild.
The pair includes its original optimized link and does not establish identical
speedups for every translation unit.

### 3. Freeze artifacts and produce complete validation

Status: the recorded scientific/profile and source-readiness gates passed.
Current delivery inventories must reflect the actual maintained files.

Acceptance conditions:

- [x] Record source, compiler, flags, dependency, binary, comparator and input
  identities. Include maintained untracked/generated inputs explicitly; a
  HEAD-only archive is not necessarily the source that was tested.
- [x] CPU/CUDA Release and Debug regressions run on the identified artifacts,
  without inheriting a pass from another GPU or a smoke run.
- [x] Hydro covers registered flux/reconstruction/limiter/time/boundary routes,
  smooth convergence, independent Sedov similarity and temporal accuracy.
- [x] Species, thermal and viscous diffusion cover RKL1/RKL2, AMR coupling and
  Cartesian/curved geometric operators.
- [x] Ideal, Helmholtz, Tabular3D and Tabular4D cover stated domains,
  derivatives, inversion, invalid-query behavior and coupled hydro/burn.
- [x] Built-in and representative generated burn networks cover BE_NR/BD/ROS4,
  composition and energy, NSE, tolerance/time refinement and sustained behavior.
- [x] Cartesian 1D/2D/3D and curved matrices cover indicators, ghosts, reflux
  and physical-volume conservation. Complete runtime refine/coarsen lifecycles
  are measured separately: Cartesian 3D and cylindrical/spherical 2D.
- [x] All four CPU/CUDA restart directions preserve native state, ENUC,
  controller and output continuity at intermediate post-regrid and terminal
  checkpoints. Strict restoration is distinct from continued physical evolution.
- [x] Independent analytic, manufactured or converged references accompany
  backend parity, with fixed declared budgets and reproducible inputs.

The accepted [Release regression](../../validation/backend/results/final-first-law-20260907/release-regression-895/evidence.json)
and [Debug regression](../../validation/backend/results/final-first-law-20260907/debug-regression-910/evidence.json)
each pass 98/98 tests with no skips; the
[CPU-only regression](../../validation/backend/results/final-first-law-20260907/cpu-regression-897/evidence.json)
passes 31/31. These identified scientific-profile artifacts retain their
original executions and are not relabeled as current maintenance runs.

The ARCH checkpoint reader passes targeted complete-state and rejection tests.
The current local CUDA build also passes all 98 configured Release tests,
both normal restart suites and burning-restart memcheck/racecheck, with final
identity checks complete. Required fields, shape consistency and scientific
identity are checked by the same reader on both backends.

### 4. Sustained reliability and capacity

Status: complete for the declared accepted workloads.

Acceptance conditions:

- [x] Relevant full application paths complete both memcheck and racecheck,
  including actual CUDA launches and complete clean per-process reports.
- [x] Repeated regrid, restart, burning and diffusion retain state validity,
  error rollback and dynamic allocation lifetime checks.
- [x] Mesh/species sizes and host RAM, swap, device allocation requests and
  whole-device telemetry are reported separately.
- [x] Capacity includes overlapping stores, migration scratch, typed ODE
  workspaces and sparse-factor storage; opaque provider/driver memory is not
  reconstructed from assumed formulas.
- [x] Expensive local commands use the shared memory guard and preserve
  failure/timeout outcomes rather than relabeling them as passes.

The final focused
[memcheck](../../validation/backend/results/final-first-law-20260907/memcheck-929/evidence.json)
and [racecheck](../../validation/backend/results/final-first-law-20260907/racecheck-903/evidence.json)
each pass all 23 routes. Additional application instrumentation separately
covers seven Cartesian 3D lanes, two curved coupled lanes and six native
burning-restart lanes per tool. Ordinary runtime coverage is not evidence of
sanitizer execution.

Owner-approved sparse observation rule: ordinary scientific runs and memcheck
retain `1e-10 s`; focused racecheck uses `1e-12 s`. Both preserve all three
ODEs, both storage sizes, four external steps, real cuDSS execution and the
original numerical/error controls. The shorter race interval is an
instrumentation observation, not the full scientific trajectory.

The [capacity record](../../validation/backend/results/device-memory-first-law-20260907/README.md)
covers regrid with 4/41 species, a 16,384-row real cuDSS solve, all-three-ODE
audit31 evolution and AMR application endpoints. Dynamic allocation lifetimes,
static module allocations and whole-device VRAM have distinct classifications.
This declared workload evidence is not a maximum supported matrix or mesh size.

### 5. Final optimization, documentation and release decision

Status: required numerical, runtime, build and capacity measurements are
complete for their recorded sources. The current local build, regression and
restart recheck also pass. The additional local AMR performance observation is
complete with CPU faster at both sizes. The technical selection is ready for
owner-authorized integration, with final delivery checks tracked above.
Third-party confirmation remains separate.

Acceptance conditions:

- [x] Measure whole-regrid time and transfers before further plan, metadata,
  survivor-arena or factor-cache optimization. Preserve transaction publication
  and rollback. Nested traces overlap and must not be added as independent work.
- [x] Record the build split, cold/incremental timings and resource envelope.
  Usable local core compilation is required; `--parallel 6` itself is not.
- [x] Preserve optimized runtime behavior while measuring compile improvements.
  Do not blanket-disable LTO/IPO or hot-helper inlining to make compilation
  appear cheaper. Floating-point correctness is not a compile-time tradeoff.
- [x] User-facing English/Chinese guides describe actual capabilities, setup,
  input conventions and consequential limitations in natural sentences.
- [x] Separate user guides, contributor ownership and quantitative Validation.
  Public summaries link the effective results, not a sequence of old attempts.
- Refresh the existing maintenance/source-publication inventories and
  complete final local-link review after the authorized cleanup; their records
  establish the actual delivery-check outcomes.
- Source selection, commit, push and publication require explicit owner
  authorization. The owner has authorized this main-branch integration;
  technical passes do not perform Git actions or establish their completion.
- [ ] Complete the acknowledged Timmes author contact/redistribution review
  under the owner's direction; attribution does not establish permission.

AMR transfer/setup measurements are available in the accepted runtime records.
They do not prove that CPU/GPU communication is free or no longer a performance
cost. Additional metadata/plan caching is a measured follow-up, not a release
correctness repair.

## Performance and memory policy

Use WSL2, an i7-10700-class CPU, 16 GB physical RAM and an RTX 3060 Ti-class
8 GB GPU as the measured mid-range reference for the declared local workloads,
not a universal minimum. Record WSL-visible RAM separately from physical host
RAM. A resource observation is not permission to change physics for that machine.

The measured two-heavy/four-total build configuration is a useful reference,
not a universal optimum. Keep generic controls configurable so larger machines
can use higher parallelism. Binary CPU ISA and CUDA SASS/PTX images must be
recorded separately from source-build compatibility; the local optimization
target is not a model-name capability floor.

Maximize optimized CPU/GPU throughput within the available memory envelope.
A smaller compiler memory peak that materially slows the executable is not
automatically an acceptable optimization. Inspect actual commands and link
settings rather than assuming a CMake property disabled or enabled IPO.

Modest productive swap use is allowed. Protect available memory, retain an
explicit swap-growth ceiling and observe sustained memory/I/O pressure through
the existing guard. Linux PSI measures system stalls, not Windows disk
utilization. Stop only the owned command tree on guard failure; do not
interfere with unrelated work. Avoid concurrent heavyweight jobs when their
combined footprint would turn useful swap into thrashing.

## Owner-approved follow-up and administrative boundaries

Audit150/audit200 full compilation, long trajectories and large-workload
scaling/capacity remain deferred to a suitable larger system. Keep their recipes,
test support and required evidence; do not repeatedly launch hour-scale local
compilations to turn this approved deferral into a nominal pass. pynucastro,
representative generated physics and real sparse execution remain supported
and verified in the local profile.

Other follow-up work is bounded by [implementation ownership](ImplementationOwnership.md):
small AMR scalar-helper consolidation needs exceptional-value review; unused
Timmes hand-Jacobian fragments must not become a separate backend derivative.
Custom-network NSE, native nuclear-table conversion and new physics/distributed
execution are separate proposals rather than unfinished release-profile gates.

The [third-party notices](../../THIRD_PARTY_NOTICES.md) explicitly attribute
Timmes-derived code and data. The author has not yet been contacted, and the
owner has acknowledged that contact/redistribution confirmation remains to be
completed. Passing tests, a source archive and attribution do not resolve that
administrative item or authorize external communication.

## Progress/evidence rules

Maintain one effective record per validation responsibility. Update the existing
module summary and report in place after a successful rerun, using the actual
source/build/input identity and newly measured values. Do not accumulate V1/V2
report sets or change an old pass's identity to fit a later source.

Preserve independent scientific fixtures, required failed-attempt evidence and
protocol negative controls under their proper owners. Superseded result data
may be retired only after its reference/dependency closure is checked and the
owner has authorized the exact recoverable operation. Never delete a needed
reference merely because a newer test passed.

A build or smoke result establishes only its stated scope. Distinguish
independent physical accuracy, backend parity, strict restore, continued
evolution, instrumentation and capacity. Physical comparisons use the same
prescribed time; internal step counts and controller rounding remain
reproducibility diagnostics, not substitute physical invariants.

After a behavior or numerical change, identify affected gates and run those
checks before reasserting support. Source-path/comment-only comparisons cannot
justify a changed reader or factory contract; focused API tests cannot stand in
for an unperformed physics campaign.

Anti-drift rule: reread this active contract, the five work packages and affected
ownership rows at least every 20 minutes of active work, after resumption, at
phase changes and before final qualification or a completion claim. Update the
current checkpoint in place rather than appending another execution diary.

Current checkpoint: the optimized local CUDA build, all 98 configured Release
tests, ten development-smoke lanes, normal smooth/burning restart, burning-restart
memcheck/racecheck and all 289 Python controls have passed on the current source.
Final runtime identity verification also passes. Both matched AMR timing scales
pass the original field, conservation, workload and identity checks; CUDA is
slower on these local workloads. The owner accepts the functionally equivalent
version for main integration with performance documented. Final document and
inventory checks guard the reviewed source selection; Git refs and command
receipts record actual integration. The scientific profile stays attached to
its measured source. Large-system work and author contact remain outside this
execution scope.

## Efficient navigation

### Navigation practice

Start with [ImplementationOwnership.md](ImplementationOwnership.md) and the
[contributor guide](README.md). Read the affected owner and its direct consumers;
use targeted symbol searches rather than repeatedly loading unrelated logs.
The [build guide](../guides/Build.md) explains normal configuration and generic
memory-protected builds. The [test guide](../../tests/README.md) maps responsibility
to focused controls.

Use [Validation](../../validation/README.md) for effective measurements and the
existing [maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md)
for current delivery checks. Do not create another release plan, numerical
library, process supervisor or evidence tree when an existing owner serves the
task. Keep the worktree intact until an explicitly authorized delivery action.
