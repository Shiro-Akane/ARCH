# CUDA release standard and execution ledger

Audience: contributors and release auditors. This is an internal working record,
not the user-facing capability guide. Preserve raw evidence and failed attempts
here / under `validation/**/results/`; do not copy this execution diary or
machine-specific observations into the public feature description.

Established 2026-09-06 at the project owner's request. This is the acceptance
contract for the current refactor, not a claim that its unchecked gates pass.
Baseline: local `codex/gpu-amr-complete`, CPU reference `main` at `8c76be8`.
Preserve the existing dirty worktree and problem modules; no automatic push.

## Active execution contract — renewed 2026-09-06

### Post-acceptance maintenance freeze — owner request, 2026-09-08 JST

The technical acceptance below remains the historical source73 result. The owner
now requests one bounded maintainability pass before freezing the worktree:
review inline policy, include boundaries and patch-like code; group the mixed
CUDA runtime by responsibility; give maintained module directories short README
entry points and separate user, contributor and validation documentation.

This pass may move files and update path references, comments and indentation.
It must not change mathematical/physical bodies, constants, runtime controls,
inline annotations, compile optimization flags, solver selection or budgets.
Keep current factories and shared mathematical authorities. Do not consolidate
superficially similar helpers without proving their contracts identical.

Preserve the complete pre-maintenance source snapshot and original evidence.
Path changes produce a new source identity; never relabel source73's scientific
records as new-source runs. Record a complete relocation/content comparison,
unchanged compilation controls, targeted build/tests and the remaining evidence
boundary. Do not start another full scientific campaign unless a real behavior
change is found; do not stage, commit, push or publish automatically.

Maintained module directories get purposeful README navigation. Dense directories
with one coherent responsibility may use a functional index instead of artificial
subdivision. CUDA runtime gets control/hydro/burn/amr/diffusion groups, with thin
built-in burn bindings under burn/routes. Generated output, third-party code,
build caches and individual historical result runs are excluded from README
proliferation and bulk rewriting. Historical reports and signatures stay intact.

Maintenance completion, 2026-09-07 23:22 UTC: the bounded pass is complete and
the worktree is ready to freeze. The isolated optimized build passes; all 98
Release tests run and pass without skips, and all ten CPU/CUDA development-smoke
lanes pass. Source, configured tests and observed artifacts remain unchanged
across execution. The build guard did not stop either phase, and neither phase
grew swap. Source/content equivalence and the original scientific evidence are
preserved separately. The [maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md)
collects the actual results and final file identities. No source, optimization,
budget or additional cleanup change remains in this pass; no commit, push or
publication is performed. The checkpoints below retain their execution context.

Maintenance checkpoint, 2026-09-07 22:45 UTC: the 44-file runtime relocation and
four comment/indentation repairs are complete. The independent comparison covers
all 452 existing source inputs, ordered include targets, all 280 configured
compiler commands, linker controls and all 98 CTest registrations. It passes
with no mathematical/physical or optimization-policy changes. Architecture
checks and their 95 regression controls pass. Maintained module README indexes
are in place and undergoing a cross-review.

The pre-maintenance source and reviewed documentation have recoverable archives
under `build/maintenance-freeze-20260908/`. A separate optimized build is in
progress; configure and the CUDA compile-probe target pass. Do not modify source
during this build. Finish its regression and representative application checks,
then record the resulting source, artifact and documentation identities in the
[maintenance record](../../validation/backend/results/maintenance-freeze-20260908/README.md).
The scientific source73 reports remain unchanged and are not new-source runs.

Maintenance scope recheck, 2026-09-07 23:03 UTC: reread this contract and the
affected ownership boundaries. Source and configured-build identities remain
unchanged. Navigation review passes for 138 Markdown files, 1,318 local links,
59 anchors and 99 functional directories. The original four critical acceptance
artifacts and both baseline archives pass preservation review. The isolated
build continues without an error; no additional source cleanup is authorized by
this checkpoint. Next: complete the configured regression and development smoke.

While this bounded phase is active, reread this maintenance contract and affected
rows of ImplementationOwnership.md at least every 20 minutes and before any
scope expansion. The older checkpoints below describe the completed scientific
campaign, not the progress of this maintenance pass.

### Completed release-profile execution contract

Owner request: finish the entire in-scope release repair and Validation fill in
this round, including the compile/resource metrics. A smoke-only milestone or
documentation-only update is not the completion condition. The order is:

1. Close shared burn correctness and independent scientific acceptance, including
   the built-in historical-reference reconciliation and weak energy handoff.
2. Finish safe core build optimization on built-ins plus representative generated
   networks; do not repeatedly instantiate large audit networks on this machine.
3. Complete independent geometry and the release-profile physical coverage.
4. Measure cold/incremental core builds and bounded 16 GB capacity, select the
   measured concurrency, then freeze reproducible candidate assets.
5. Run final CPU/CUDA Debug/Release Validation, sanitizer and sustained tests;
   archive metrics/inputs/provenance and synchronize the status tables. Only
   final-identity passing evidence closes a release gate.

### Owner-approved scope adjustment — 2026-09-06 13:19 UTC

Large generated-network compilation, long trajectories and scaling/capacity
qualification (including audit150/audit200) are DEFERRED from the local release
critical path. Preserve their sources, runnable tests and all passing/failed/
timeout evidence for a suitable larger machine. Do not start more hour-scale
local builds merely to complete these optional workload records. This explicit
owner decision supersedes earlier entries that make every large case a local
release blocker; it does NOT turn deferred tests into passes.

pynucastro remains a supported feature: the local release profile still requires
real generated-network math/trajectories, weak storage and energy, the exact
DenseLU/sparse equation boundary, real KLU/cuDSS calls, provider residual/reuse/
failure controls, registration, explicit rejection and representative end-to-end
use. Retain audit31 and a compact weak network rather than deleting generator or
sparse coverage. Resource exhaustion and unsupported selections must fail
clearly. Do not advertise untested arbitrary sizes, performance or multi-GPU/MPI
execution; a larger validation machine does not confer those capabilities.

After burn acceptance closes, move directly to core compilation and complete
release-profile Validation. Freeze nonessential refactors. The requested
48-hour sprint remains a target, not permission to waive scientific budgets or
publish known-broken supported cases. Final docs separate tested workload scope
from deferred large-model qualification, without hiding the latter.

Owner runtime-performance direction (2026-09-06): maximize CPU/GPU throughput
within the declared host RAM / WSL RAM / GPU VRAM envelope. Debug diagnostic
speed is not a production performance target; measure optimized builds.
Prefer inlining of small hot
helpers and effective parallel execution where memory permits. A compile-cost
candidate that materially slows execution is not automatically acceptable.
The one-math authority, numerical accuracy, finite/error checks and no-hidden-
fallback contracts remain unchanged; this is not authorization for unsafe
fast-math or for resuming deferred large workloads.

Owner renewal (2026-09-07 JST): continue through complete Validation to the
release threshold. Do not shorten compilation by accepting runtime regression
or by blanket removal of optimization features such as LTO. Retain and measure
optimized execution; distinguish compiler/linker optimization from relaxed
floating-point semantics. Current Release application IPO remains enabled.
The large CPU dispatch target inherits O1 and an intended no-IPO property from
main, but configure-553's actual commands still include -flto=auto because the
configuration-specific IPO setting takes precedence. Preserve those actual
optimized flags during build-555; do not describe that archive as no-LTO.
CUDA object/provider boundaries have explicit IPO settings. Audit those actual
boundaries before changing them, and require memory/runtime evidence for a new
tradeoff. Strict finite checks, compensated arithmetic and the shared CPU/CUDA
numerical contract are correctness requirements, not compile-speed shortcuts.

Owner compatibility direction (2026-09-07 JST): use WSL2, an i7-10700-class
CPU, 16 GB host RAM and an RTX 3060 Ti-class 8 GB GPU as the mid-range
reference configuration for the declared local release workloads,
not as an inferred universal minimum. The final core-build and capacity records
qualify the declared reference workloads. Record
the WSL-visible RAM separately from physical host RAM. No CPU/GPU model-name
branches, machine-specific mathematical tolerances, reduced physics, or hidden
small-workload modes are allowed. Build concurrency and backend workspaces may
respond to measured available resources through the existing generic controls.
Keep optimized runtime settings; document the CPU ISA and CUDA code images of
each binary separately from source-build compatibility.

The compatibility repair now uses the configured SASS/PTX image set instead of
an unconditional compute-capability 8.6 floor. Retain genuine kernel/provider
requirements and strict explicit-CUDA rejection; the local sm_86 target is not
itself a physical-feature minimum. Distinguish the inherited `-march=native`
local optimization from a redistributable CPU ISA baseline. Final artifact
qualification must record both CPU ISA flags and CUDA images; passing here
does not establish performance on every eligible device.

Owner build-safety renewal (2026-09-07 JST): modest swap use is acceptable.
Protect available RAM and detect sustained swapping / I/O stalls rather than
requiring zero swap. Linux PSI describes WSL-visible stalls, not Windows disk
utilization; do not equate a busy disk with thrashing. Use the existing process
guard, retain an explicit swap-growth ceiling, and stop only its owned command
tree on sustained pressure. Measure heavy-pool / total-job combinations locally
to choose a mid-range reference; preserve configurable higher parallelism for
larger machines. Server measurements can follow later. No hardware-specific
physics or numerical tolerances are introduced by this build policy.

Owner documentation renewal: write complete, natural sentences for users.
Explain features, setup and usage directly; put the current release status in
one clear place and investigations in contributor records. Avoid repetitive
disclaimers, defensive contrasts, unexplained noun clusters and raw local logs.
State consequential limitations clearly without turning every feature into a
qualification footnote. A readable guide must still describe the actual code.

Owner administrative decision (2026-09-07): attribute the Timmes-derived
network, NSE, Helmholtz code and table data explicitly. Record that the author
has not yet been contacted and that contact / redistribution confirmation will
be supplemented. Keep this pending administrative release item separate from
technical acceptance; attribution and passing tests do not imply permission.

Owner instrumentation decision (2026-09-07): the sparse racecheck observation
may use a shorter physical interval than scientific trajectories. Ordinary and
memcheck retain their full interval; racecheck must still exercise all three
ODEs, both storage sizes, real cuDSS execution and the original error controls.
Record the explicit interval and purpose in evidence. No production algorithm,
tolerance or scientific-acceptance criterion changes under this decision.

For BD, the owner explicitly identifies CPU as the working reference. Trace the
first GPU divergence (input, EOS, ODE, energy handoff, reduction, persistence);
do not presume the CPU definition is wrong or change it just to make parity
pass. Independently proven shared mathematical defects must be documented as
such and fixed once, without a GPU-only numerical fork or relaxed acceptance.
The previous speculative request about changing CPU burn semantics is not an
authorization and is not a reason to pause ordinary GPU diagnosis.

Anti-drift checkpoints: reread this active contract, the five work packages,
and the affected ownership-map entries at least every 20 minutes of active
work, after any context resumption, at each phase change, and before a final
qualification run or completion claim. Record the UTC reread time, active gate,
evidence and immediate next action below. Do not repeatedly reread unrelated
historical logs or create another competing release-plan document.

Owner temporal-acceptance decision (2026-09-06 23:48 UTC): physical acceptance
compares solutions at the same prescribed physical time and retains independent
accuracy/convergence, composition and source-aware energy checks. Internal step
counts and controller roundoff are reproducibility diagnostics, not physical
invariants. Do not copy field tolerances to time or relabel checkpoint times.
Keep the existing strict restart/restore contract. A step-diagnostic comparison
cannot replace a required fixed-time scientific run. Shared Helm derivatives
must be checked against independent thermodynamics; improve the common EOS if
needed, never add a backend-specific compensation. This supersedes the earlier
pending-owner notes without turning any failed or unrun case into a pass.

Last checkpoint: **2026-09-07 19:54 UTC**. Active contract, all five packages
and affected owners were reread before final review/index execution; no source
or bound-document changes followed. Final index-930 completed with exit 0:
41 required gates pass, zero pending, zero failed, and the two owner-deferred
audit150/audit200 entries. It rechecked 1517 referenced file identities and
preserved ten historical attempts. Source remains
73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171.
Index SHA 2af13844b90959a57e4479fc269641cd87ce1b8c6ef2404f62f59c5f8f690b85;
manual review SHA 9bf9f7de57fd4616bbf7dfe6ebeda508132df2162ed510e3fc4e96a483904857.
The manual gates cover 38 current documents and 172 asset identities, including
all 139 untracked maintained inputs. The isolated index guard completed in
126.407 s, RSS 49412 KiB, minimum available 7094952 KiB, swap 234496 to
237056 KiB; no guard stop. The owner-approved technical profile is complete.
Independent final-index review confirms all 43 unique entries, their actual
primary report identities and the retained historical outcomes. The new bilingual
result summaries have passing local-link checks; all 210 manual file entries
were rehashed after writing those summaries and still match, with the complete
source identity unchanged. No further production refactor or GPU campaign is
needed for this profile. Source publication/commit has not been performed. Timmes
author contact and redistribution confirmation remain pending administrative
work. Do not convert the audit's release_qualified=false into a certificate.

Final result: [candidate acceptance and source handoff](../../validation/backend/results/final-acceptance-20260907/release-73a9cf50/README.md).
This ledger is the sole plan; that results note records the completed evidence
and publication boundary, not a new work programme.

Prior checkpoint: **2026-09-07 19:46 UTC**. Active contract, all five packages
and affected ownership entries reread after resumption and before final delivery
review. Focused memcheck-929 and racecheck-903 both PASS all 23 routes, with
complete clean per-route reports and independent file/command/parser reviews.
Racecheck alone uses the owner-approved 1e-12 sparse interval; ordinary and
memcheck science retain 1e-10, all three ODEs, both storage sizes and unchanged
budgets. All GPU campaigns have finished. English/Chinese entry documents and
module summaries now describe the tested shared CPU/CUDA feature scope and
link the central validation status. Finish the explicit documentation and frozen
worktree source-delivery review, then run the final evidence index alone.
Source73 and recipe a4c505dc remain unchanged. Preserve historical failed and
preflight records. No staging, commit, push or author contact; redistribution
confirmation remains a separate pending administrative item.

Prior checkpoint: **2026-09-07 19:23 UTC**. Active contract, all five packages
and instrumentation/resource owners reread on schedule. Racecheck-903 has
completed 19 of 23 routes, including manufactured 32/151/201-equation sparse
ODE/provider controls, singular retry, EOS failures, NSE, generated math and
the three built-in ODE routes. The four explicit weak/generated trajectories
remain in progress; no complete race report exists yet. The earlier 18-route
campaign covered the same manufactured sparse inventory, but its record lacks
per-route duration and has different source/binary identities; it supplies no
current pass or reliable ETA. Memcheck-929 independently passes all original
inventory, identity, clean-report and full-interval sparse-parser checks; review
SHA cf2e30b20b7906158d0a0f2f1a522bf632461fa7c55b0192f4ca48a5e1831294.
Six module-summary pairs now link to the central validation index for overall
status; their numbers, budgets and commands are unchanged. The focused results
guide documents both observation profiles and guarded reproduction. Continue
the sole GPU campaign, then finish final document/source-delivery declarations
and run the index alone. Source73 and recipe a4c505dc remain frozen. No staging,
commit, push or author contact; administrative confirmation remains pending.

Prior checkpoint: **2026-09-07 19:04 UTC**. Active contract, all five packages
and instrumentation/resource owners reread at the final racecheck phase change.
Focused memcheck-929 PASS: all 23 routes, including the full 1e-10 audit31
trajectory with all three ODEs, both storage sizes and four subdivisions.
Evidence SHA d570c858212f4e0990f8ff3bd85f6a2a28b10967721d5193aa5cd5553a9644e2.
Sparse field/limiter maxima are 1.881217683662624e-14 /
1.8305061193690554e-14, within the original 2e-10 / 2e-8 budgets.
The guard completed in 1943.872 s with peak owned RSS 569852 KiB,
minimum available 6439388 KiB, swap unchanged at 242176 KiB, and no stop.
Whole-device usage ranged from baseline 1545 to peak 4346 MiB, minimum free
3679 MiB; it is not an allocator or production-speed measurement. Independent
pure-file review is underway. Racecheck-903 now runs alone with seven routes
complete; only its sparse interval changes explicitly to 1e-12. Finish that
campaign, then finalize manual documentation/source-delivery review and run the
full index alone. Source73 and recipe a4c505dc remain frozen; failed 902/916
records remain intact. No staging, commit or push; author contact stays pending.

Prior checkpoint: **2026-09-07 19:01 UTC**. Active contract, all five work
packages and validation/resource ownership entries reread on schedule. Focused
memcheck-929 remains on its final full-interval sparse route after 22 clean
routes; racecheck-903 is still queued on success. Keep its 2400-second per-route
allowance, full 1e-10 interval and existing guard unchanged. No simultaneous
GPU work or full index. The results-only delivery-assets inventory records all
139 untracked maintained inputs present/unignored, exactly three retired
headers without active consumers, the matching Helm LFS object and 50 matching
installed/recovery package files. This is not a commit or final manual pass.
Both Reference translations now describe provider/network registration in
plain language; no numerical behavior or current validation-status label changed.
An intermittent tool-process descriptor error recurred, but an actual read-only
check observed only 107 descriptors for Codex, 8 for the guard, 5 for its runner,
19 for the sanitizer and 32 for the workload, against soft limits of 10240.
System file-nr was 1536. These observations do not identify the tool error's
cause; the live test remains active with bounded memory and no sustained PSI
pressure. Source73/recipe a4c505dc stay frozen. Finish focused instrumentation,
then curate actual reports, record manual delivery review and run the final
index alone. No staging, commit or push; upstream contact remains pending.

Prior checkpoint: **2026-09-07 18:43 UTC**. Active contract, all five work
packages and affected mathematical, instrumentation and delivery owners
reread after context resumption. Focused memcheck-929 has completed 22 of
23 routes, including both weak thermodynamic trajectories and weak owner
cells. Its final audit31/cuDSS route retains the full 1e-10 interval and
original controls. Racecheck-903 remains queued only after success and
alone selects the owner-approved 1e-12 sparse observation interval. No other
GPU campaign or full index runs concurrently. Independent final documentation
review continues with no source edits. Complete both focused reports, then
close the explicit documentation/worktree-delivery review and run the final
index alone. Source73 and recipe a4c505dc remain frozen; attribution/contact
status is separate from technical acceptance. No staging, commit or push.

Prior checkpoint: **2026-09-07 18:29 UTC**. Focused memcheck-916 was
interrupted after three completed routes by its GPU telemetry monitor: the
`nvidia-smi` query returned nonzero with empty stderr. The guard reported
`monitor_error`, not memory pressure or a completed scientific/sanitizer result.
No overall evidence was produced, and the queued racecheck did not start.
Guard elapsed 10.027 s, RSS peak 500476 KiB, minimum available 6747692 KiB,
swap unchanged at 242944 KiB. Preserve the original partial fourth route,
logs and frozen recipe. Exact telemetry queries now return normal values;
no driver cause is inferred because that failed query's stdout/code were not
retained. Identical successor memcheck-929 now runs, then racecheck-903 only
on success. Do not disable telemetry, shorten memcheck or change the guard.

One subsequent tool-process launch also returned a file-descriptor error.
A read-only retry succeeds: the observed tool process has 106 descriptors
against limit 10240, with only the expected guarded test tree active. This
does not establish the cause of the earlier telemetry or tool error. Continue
watching the existing test; no production/tool-source edit is justified yet.
Dynamic-3D racecheck-915 has meanwhile passed independent pure-file review,
including exact same-parent eight-child coarsening and re-refinement. Both
root build examples now preserve default KLU alongside cuDSS, matching the
tested configuration. Source73 and the a4c505dc sanitizer recipe remain frozen.
No staging, commit or push.

Prior checkpoint: **2026-09-07 18:26 UTC**. All five work packages and the
active instrumentation / numerical-identity rules and affected owners reread
before the final focused campaigns. Dynamic-3D racecheck-915 PASS: the
original seven checkpoints and complete eight-child topology lifecycle finish
with no reported race hazards. Guard elapsed 682.959 s, peak owned RSS
1122496 KiB, minimum available 5843868 KiB, swap 239616 to 247552 KiB,
no stop. An independent pure-file review will curate the complete record.
Focused memcheck-916 now runs, followed serially by racecheck-903 only if it
succeeds. Both use frozen recipe `a4c505dc...` and Python 3.11. Memcheck
retains 1e-10 / four subdivisions; race alone explicitly selects 1e-12 with
the same ODE/storage inventory and budgets. No other GPU campaign or full
index runs concurrently. The root CUDA build example now retains default
CPU KLU alongside enabled cuDSS, matching the tested build rather than
disabling one provider. Only documentation/result policies changed; source73
and binaries remain frozen. No staging, commit, publication or push.

Prior checkpoint: **2026-09-07 18:15 UTC**. Active contract, all five work
packages and validation/resource owners reread on schedule. The current
index has 36 passing / five pending / zero failed gates; three pending gates
are actual GPU safety campaigns, two are final manual delivery records.
Dynamic-3D racecheck-915 is running with its first three endpoints clean.
Do not run another GPU campaign concurrently. All completed scientific,
regression, compilation and capacity evidence remains on source73. The root
repository maps now name the existing CMake modules and CUDA backend directory;
the ownership map records the results-only sparse observation policy and its
reuse of the existing transcript/sanitizer authorities. Public summaries keep
runtime topology cycles distinct from initialization and instrumentation
timings distinct from performance. Complete remaining safety tests, then
finalize readable delivery documentation and the explicit review record.
No numerical/source changes, staging, commit or push; pending author contact
remains a separate administrative item.

Prior checkpoint: **2026-09-07 18:13 UTC**. Isolated index-preflight-928
completed: 36 passing gates, five pending, zero failed and two owner-deferred.
It rechecks capacity-926, curved racecheck-906, restart-905/907 and all earlier
scientific/regression evidence against the same final source and artifacts.
Guard elapsed 114.332 s, peak owned RSS 42680 KiB, minimum available
7126484 KiB, swap 229632 to 239872 KiB, no stop. Interrupted 927 remains
separately documented; no failed attempt is relabelled.
The five open gates are dynamic-3D racecheck-915, focused memcheck-916,
focused racecheck-903 and the two explicit manual delivery reviews. The
seven-checkpoint dynamic-3D race campaign now runs alone. Both focused
campaigns will use Python 3.11 for their shared-parser diagnostic sums;
memcheck keeps the full interval, racecheck alone explicitly selects 1e-12.
No further source changes or compiler reruns are needed absent new defects.
Pending author contact remains administrative, not an inferred technical pass
or license. No staging, commit or push.

Prior checkpoint: **2026-09-07 18:10 UTC**. Restart memcheck-905 and
racecheck-907 PASS and independent review confirms each original twelve
executions / nine strict comparisons, six clean CUDA reports, native fields,
controller and output continuity. Guards complete in 59.528 / 38.934 s with
swap unchanged at 200480 KiB. Curved coupled racecheck-906 also PASS after
268.559 s, without guard stop; its complete report remains to be independently
curated. Concurrent read-only index-preflight-927 was stopped by its own global
swap-growth guard after 109.555 s, with no index report. Its peak owned RSS was
39768 KiB and system swap 201316 to 464368 KiB; system counters do not assign
that growth to the index. Preserve the interruption note/log. Index-preflight-928
now runs alone with identical thresholds and a fresh directory; do not repeat
the successful GPU campaign or weaken the memory limit.

The results-only sparse instrumentation recipe `a4c505dc...` is frozen after
seven CPU controls. Its default commands remain byte-identical to the archived
full-interval recipe; only the explicit race option changes the sparse interval.
The shared sparse transcript parser verifies all ODE/storage/substep coverage.
The index now checks every route's independent sanitizer report, unique lane
binding, exact command/profile and sparse transcript; fifteen controls and
independent review pass. Remaining GPU work is dynamic-3D racecheck-915 and
focused memcheck-916 / shortened-observation racecheck-903. Final delivery
review and author-contact administration remain separate. Source73 and all
science/build binaries are unchanged; no staging, commit or push.

Prior checkpoint: **2026-09-07 18:00 UTC**. Capacity-926 PASS on all four
original workloads: regrid transaction, real 16384-row cuDSS capacity,
audit31 scientific trajectory and AMR application. Every dynamic allocation
lifetime closes; static module symbols remain honestly classified separately.
The pinned launcher / workload association passes without prefix matching.
Guard elapsed 293.606 s, peak owned RSS 1903980 KiB, minimum available
5354144 KiB, swap 197664 to 200480 KiB, no guard stop. Whole-device baseline
1527 / peak 2306 / minimum free 5719 MiB is separate from allocator requests.
An independent resource review is curating the scoped evidence. Coupled-curved
memcheck-904's complete report also passes independent integrity and original
numerical checks; both CUDA reports have zero errors and leaks. Restart
memcheck-905 is next, without concurrent GPU work.

The owner now explicitly permits different sparse scientific-trajectory and
race-observation intervals. Ordinary and memcheck retain the original total
interval 1e-10. Only the audit31 race route will explicitly request 1e-12,
retaining four subdivisions, all three ODEs, both storage sizes, positive
real provider activity, error controls and unchanged numerical budgets. This
is results-only validation policy, not a production or scientific-acceptance
change. Record it in the recipe/evidence and independently recheck coverage;
do not count the shorter race interval as the complete scientific trajectory.
Source73 and binaries remain unchanged; no staging, commit or push.

Prior checkpoint: **2026-09-07 17:56 UTC**. Active contract, all five packages
and affected resource/validation owners reread after resumption. Coupled-curved
memcheck-904 completed with exit zero; both original CUDA endpoints have clean
error/leak summaries and the complete report is undergoing independent
integrity/coverage review. Guard elapsed 997.696 s, peak owned RSS 1973688 KiB,
minimum available 5182788 KiB, swap 189296 to 222752 KiB, no guard stop.
Capacity-926 now runs alone with frozen recipe `36437cb8...` and the explicitly
pinned Nsight launcher. Regrid and sparse-capacity profiles have passed; the
unchanged audit31 trajectory is in progress. No overall capacity pass yet.
The owner's latest reply is reflected in both third-party notices: Timmes's
work is attributed, the author has not yet been contacted, and contact /
redistribution confirmation is a separate pending administrative item. This
does not claim permission. Remaining safety campaigns retain their original
physical inputs. Source73, binaries and budgets remain frozen; no staging,
commit, publication or push.

Prior checkpoint: **2026-09-07 17:45 UTC**. Active contract, all five packages
and affected validation/resource owners reread on schedule. Source73 and all
scientific binaries remain unchanged. Capacity recipe `36437cb8...` is frozen:
explicit optional profiler-launcher path/hash/stat, known nonoverlapping
launcher-before-workload order at one PID/start time, and unchanged ordinary
workloads, observer, event reader and lifetime policy. Thirty-nine identity
controls and nineteen lifetime controls pass; the index forwards the declared
launcher and checks tool identities, with all thirteen controls passing.
Original 922/925 failures and selected diagnostic captures are retained in
the index history. Capacity-926 is queued with the observed installed launcher;
do not run it concurrently with the current coupled-curved memcheck-904.
That campaign's two-step CUDA lane has a complete clean error/leak report and
the five-step lane is running. Remaining focused, restart and race campaigns
retain their original inputs and declared wall-time allowances. No new
mathematical defect, tolerance change, source edit, staging or push. Final
documentation/assets review and pending author-contact administration remain.

Prior checkpoint: **2026-09-07 17:35 UTC**. Capacity-925 retains a complete
selected-PID observation and fails its unchanged strict identity check after
6.439 s, without a guard stop. The actual PID has one start time and two
ordered executable images: the installed Nsight launcher, then the exact
frozen regrid test. This diagnoses 925's normal profiler exec handoff; 922's
missing capture is not retrospectively reconstructed. Prepare a result-only
explicit launcher identity parameter, pin its full path/hash/stat, and permit
only that known image exclusively before the unique workload image. Unknown,
overlapping, returning or changed images must still fail. Preserve both failed
recipes and evidence. Complete capacity-926 will run alone after review; no
workload, physics, memory-pool or numerical setting changes. Curved coupled
application memcheck-904 is now the sole GPU campaign while that repair is
prepared. Functional/scientific coverage remains complete on source73, with
remaining instrumentation, capacity, delivery and author-contact items open.
No staging, commit, publication or push.

Prior checkpoint: **2026-09-07 17:31 UTC**. Curved lifecycle-923 PASS on
source73: both cylindrical and spherical 2D cases at 2/5/20/80/160 steps,
ten CPU/CUDA comparisons and complete parent/four-child runtime refinement
and coarsening. Each final lane records nine runtime topology changes,
separately from initialization, with at most 64 leaves under the unchanged
128-block capacity. Maximum field difference is 8.881784197001252e-16;
all original RKL4, physical-volume conservation and field budgets pass.
Guard elapsed 178.164 s, peak owned RSS 326692 KiB, minimum available
6882668 KiB, swap unchanged at 189296 KiB; no guard stop. Whole-device
baseline 1527/peak1714/minimum-free6311 MiB is not per-process capacity.
The active functional/scientific coverage is now complete for the release
profile; final instrumentation, capacity and delivery still must close.
Capacity-925 is prepared with the same strict identity classifier plus
selected-CUDA-PID diagnostic retention before failure. Review its small
results-only diff, then run it alone; do not silently discard startup images
or claim that missing observations passed. Original 922 remains failed.
No numerical/source/binary changes, staging or push.

Prior checkpoint: **2026-09-07 17:28 UTC**. Capacity-922 failed after 7.668 s
at the first regrid process-identity join, before any complete capacity record.
The guard did not stop; the observed CUDA PID lacks exactly one captured image.
The failed observer capture was not archived, so missing sampling versus
multiple startup images is not yet distinguished. Preserve the frozen recipe
and failure; add selected-PID diagnostic retention before choosing a repair.
Do not relax identity or use a short-name prefix. Curved-923 now runs alone
under the unchanged guard while the capacity diagnosis remains read-only.
Dynamic-3D memcheck-914 is complete and linked from the bilingual validation
summaries; the remaining safety and capacity gates are still open. No production
or numerical changes, staging, commit, publication or push.

Prior checkpoint: **2026-09-07 17:25 UTC**. Resource-phase contract and owners
checked after the complete reread at 17:23. Dynamic-3D memcheck-914 PASS on
source73: all seven instrumented CUDA lanes, original field/conservation
budgets and explicit complete parent/eight-child refine/coarsen/refine cycle.
All seven reports have zero errors and zero leaked bytes. Guard elapsed
1026.115 s, peak owned RSS 914368 KiB, minimum available 6281584 KiB,
swap 169576 to 233320 KiB and no guard stop; whole-device baseline 1545,
peak 2691 and minimum free 5334 MiB. These instrumented timings are not
runtime-performance measurements. Index-preflight-924 also completed:
29 pass, 12 pending, zero failed, two owner-deferred gates. It sampled 914
before the final report existed, so that gate remains pending in this immutable
snapshot; later indexing will include the new pass. Capacity-922 now runs
alone with the reviewed frozen recipe and original four-workload/science
inventory. Then run curved-923 and the remaining instrumentation, including
3D racecheck. No production changes, budget relaxation, staging or push.

Prior checkpoint: **2026-09-07 17:23 UTC**. Active contract, all five packages
and resource/validation owners reread before the next capacity phase. The
results-only capacity identity repair is frozen at recipe `22297332...` after
21 identity controls, 19 unchanged lifetime controls, five shared-reader tests
and independent code review. It reuses owned PID/start-time discovery, exact
SQLite PID association and observed executable inode/stat binding to frozen
artifact hashes; no prefix matching or profiler bit-layout assumption remains.
Scientific workloads and their commands are unchanged. Successor capacity-922
must run alone after current jobs finish. Curved-920 remains failed, with its
byte-identical recipe and complete postmortem retained. Read-only checks of all
six cylindrical snapshots and twelve physical-volume balances pass; stage 320
still fails its fixed-four-stage witness. The same parent/four-child round trip
is already observed through step 160. Successor curved-923 therefore uses
2/5/20/80/160 observations with all inputs, budgets, topology rules, RKL4 and
timeouts unchanged; no adaptive parser or production edit is introduced.
Spherical execution remains pending. Dynamic-3D memcheck-914 is in its final
80-step lane with the earlier six instrumented endpoints clean. Read-only
index-preflight-924 runs alongside it; neither is a completed gate yet. The
updated index rechecks executable associations, preserves 920/921 histories and
passes 13 controls. Network documentation now links completed cold-909.
Delivery review confirms 139 necessary untracked maintained inputs and three
intentional deletions; installed/durable generated packages and the Helm LFS
object match. Future staging/commit requires a truthful final identity mapping,
not rewriting existing source73 evidence. No source/binary changes or push.

Prior checkpoint: **2026-09-07 17:08 UTC**. Active contract, all five packages
and affected owners reread after resumption. Source `73a9cf50...` and the
reference executables remain unchanged. Capacity-921 completed its regrid,
real cuDSS capacity and 64-subdivision sparse scientific workloads, then failed
the selected-process identity check: Nsight recorded the Python-launched CUDA
child's truncated process name, not its complete executable path. The existing
database cannot securely recover that identity. Retain 921 as failed; prepare
a result-only observer using the existing owned-descendant/PID-start-time
authority, without accepting a name prefix or changing scientific workloads.
The final AMR capacity workload has not run. Curved-920 stopped at the original
fixed-four-stage witness in its cylindrical step-320 lane; the actual simulation
completed, but its final comparison and conservation checks have not yet run.
Read-only diagnosis checks whether coarse-grid dt changes legitimately select
three stages. Preserve the failure before deciding any lifecycle-specific
observation policy. Spherical lifecycle is still unrun. Dynamic-3D memcheck-914
now runs as the sole GPU campaign under the unchanged guard. Cold-909's five
measurements have been curated in bilingual docs; capacity, instrumentation,
curved lifecycle and final delivery remain open. The owner's attribution and
pending-author-contact instruction is recorded in both notices. No production
edits, numerical relaxation, staging, commit, publication or push.

Prior checkpoint: **2026-09-07 16:53 UTC**. Contract and resource/validation
owners checked at the phase transition. All five cold/concurrency-909 phases
PASS with final evidence: cold 2213.445 s, no-op 1.012 s, one compact route
plus optimized relink 27.323 s, identical-work serial pair 45.320 s and parallel
pair 31.312 s. Every phase retains command/scheduling equivalence and the
original optimized flags; compiler cache is disabled and all 280 configured
compile outputs were absent before the cold ARCH target. The cold target
compiled 207 translation units (including required dependencies), not the test
executables. Heavy CUDA pool 2 / total jobs 4 is a measured local reference,
not a universal optimum; one compact-route incremental result is not a promise
for edits to widely shared mathematical headers. Capacity-921 now runs alone
under its unchanged physical/time controls and reviewed lifetime classifier.
Then run curved-920 and the final instrumentation campaigns. Source `73a9cf50...`
and qualified numerical/reference executables remain unchanged. Build metrics
will be curated separately from raw machine records. No release or push;
curved coarsening, capacity, sanitizer, final delivery and author contact remain.

Prior checkpoint: **2026-09-07 16:51 UTC**. Active contract, all five packages
and resource/validation owners reread on schedule. Cold-909 and no-op phases
PASS, retaining identical optimized ARCH commands and heavy scheduling:
2213.445 s cold, peak owned RSS 3763288 KiB, minimum available 3439116 KiB,
swap 125264 to 202600 KiB; no guard stop. No-op takes 1.012 s. One compact
Ideal/iso7 route rebuild plus optimized relink passes in 27.323 s with peak
owned RSS 1093016 KiB. Paired serial/parallel phases remain running; do not
close the complete build/concurrency gate before its final evidence exists.
After that campaign ends, capacity-921 runs alone, followed by curved-920
and remaining instrumentation. Sparse-capacity budget preflight confirms
64 subdivisions cover the same total interval=1e-10 as the four-subdivision
reference; they do not multiply physical time by sixteen. Retain existing
1800/3600 s timeouts, all methods and original tolerances. No new production
failure, source edit, numerical relaxation, staging or push. Shared correctness
is complete for the local release profile; outstanding resource/coverage/
safety/delivery gates and pending author contact remain explicit.

Prior checkpoint: **2026-09-07 16:31 UTC**. Active contract, all five packages
and affected ownership entries reread on schedule. No production, source,
input or binary changes: final scientific and regression evidence remains
frozen at `73a9cf50...`. Cold/concurrency-909 is the sole heavy job, through
the built-in CUDA route instantiations without a guard stop. Keep all five
measurement phases and the exact optimized reference flags. Next run complete
capacity-921 alone using the reviewed result-only lifetime classifier, then
curved lifecycle-920 and the remaining focused/application/restart/3D
instrumentation campaigns. Do not substitute the 908 diagnosis for a complete
capacity pass. Three-dimensional ordinary lifecycle-919, ownership mapping
and whole-regrid measurement are complete, but curved runtime coarsening,
sanitizer, build/capacity and final documentation/assets review remain open.
No stage-witness, trajectory-length or science-budget relaxation has occurred.
Final acceptance index must use its sparse producer's Python 3.11 and retain
all failed histories; administrative author contact remains separately pending.
No staging, commit, publication or push.

Prior checkpoint: **2026-09-07 16:11 UTC**. Contract and AMR/resource owners
reread at the resource-phase transition. Capacity-908 stopped after its first
profile: the regrid transaction itself passes, but the allocator reader rejects
incomplete/unclosed allocation coverage. Preserve its logs and local Nsight
database; no capacity gate closes and no leak/profiler diagnosis is presumed.
Guard elapsed 8.555 s with no pressure stop. A bounded read-only CUDA-event
audit is running; raw profiler environment metadata stays out of the archive.
Cold/concurrency-909 now runs alone in its preflighted separate tree, retaining
the exact optimized reference command set, disabled compiler cache and the
existing per-phase resource guard. No other build/GPU campaign is running.
Resume capacity after resolving the observed failure; curved lifecycle-920
is only being prepared. The completed 919 and regression/science evidence
remain valid for unchanged source `73a9cf50...`. No production edits or push.

Capacity-policy diagnosis (2026-09-07 16:23 UTC): 908 contains 1321 successful
cudaMalloc/cudaFree pairs with no dynamic residual. Two device-static symbols
have no observed release; CUDA documents their context lifetime separately.
The shared event reader is correct and remains unchanged. Archive 908's exact
recipe and selected-event diagnosis under its attempt directory. The result
recipe now requires every dynamic memory-kind total to close and real dynamic
device/array coverage, retaining static residuals and the original
all_allocations_released flag. Nineteen new pure-data controls and the five
unchanged reader tests pass. Workloads, physical inputs and profiler commands
are unchanged. Complete successor capacity-921 remains unrun until isolated
cold/concurrency-909 finishes. The result index reuses this classifier and
retains 908 as failed; it also adds required curved lifecycle-920, still pending.
Thirteen index self-tests pass. No full-index qualification, source or binary
change is implied by these result-only repairs.

Prior checkpoint: **2026-09-07 16:09 UTC**. Active contract, five packages and
resource/AMR owners checked at the phase boundary. Cartesian 3D lifecycle-919
PASS: seven CPU/CUDA checkpoint pairs, complete parent/eight-child refinement
and coarsening, unchanged field and conservation budgets, source `73a9cf50...`.
At step 80 each backend records thirteen runtime topology changes, separately
from initialization; at most 76 leaves occupy the unchanged 128-block capacity.
Guard elapsed 434.612 s, peak owned RSS 843536 KiB, minimum available 6378776
KiB and swap unchanged at 127056 KiB; no pressure stop. Whole-device peak 2638
MiB includes the 1591 MiB baseline and is not a per-process allocation measure.
Preserve failed coverage-913. Instrumented 914/915 still remain open.
Capacity-908 now runs alone, then cold/concurrency-909 runs alone. Prepare two
existing curved 2D species-only RKL1 cases with fixed observations
2/5/20/80/160/320 as lifecycle-920, retaining all inputs, budgets and the original
four-stage witness initially. If adaptive stage selection changes after actual
coarsening, retain that failure and distinguish the original short-stage witness
from the long topology test; do not alter production dt growth or hide a failed
assertion. Ownership mapping is complete, with cleanup candidates explicit.
Technical work continues; author-contact administration remains separately
pending. No source changes, staging, commit, publication or push.

Prior checkpoint: **2026-09-07 16:00 UTC**. Active contract, all five packages
and affected ownership entries reread after resumption. Debug-910 is complete:
98/98 pass without skips. Three-dimensional Sedov-913 completed all six
CPU/CUDA checkpoint comparisons and conservation checks, but failed its
bidirectional topology coverage requirement: step 40 and step 80 miss the
intermediate coarsening recorded in both backends' regrid logs. Preserve that
failure and the byte-identical original recipe. New lifecycle-919 adds only
the step-41 observation, within the same 80-step window, and is running under
the guard; physics, thresholds, capacity and numerical budgets are unchanged.
In-memory shared-checker controls reject missing/interrupted child groups and
accept the explicitly sampled coarsening. Instrumented 914/915 will use the
same seven observations. Curved-873's full TSV audit finds runtime refinement
in four 2D RKL1 cases, but no curved runtime derefinement; assess the smallest
existing-case observation supplement rather than claiming initialization is
a full lifecycle. Next: isolated capacity-908 and cold/concurrency-909, then
remaining instrumentation. No production/source/input changes, staging or push.
The owner's attribution and pending-author-contact instruction is now in both
third-party notices; administrative confirmation is not a technical test gate
or evidence of permission.

Prior checkpoint: **2026-09-07 15:44 UTC**. Active contract, all five packages
and mathematical/backend/validation owners reread. Source `73a9cf50...` and
Release artifacts remain frozen. Debug-910 has passed its 927.30 s built-in
burn reference and is completing CUDA policy tests; do not call the suite
complete before its final report. Next: the prepared ordinary 3D Sedov
lifecycle-913, then isolated capacity-908 and cold/concurrency-909, followed
by the remaining complete instrumentation campaigns. Memcheck-902 remains a
retained timeout. Its byte-identical recipe is archived; new memcheck-916
uses 2400 s per complete route, and racecheck-903 uses a generous 86400 s
ceiling because same-provider race overhead can be much larger. These are
execution allowances, not ETAs or changed physical steps/error budgets.
Index preflight-917 detected its own JSON-decoding error for sparse-900:
sorted control keys and JSON key/list types must be restored at the archive
boundary. Two diagnostic timing sums also differ by one last bit between
Python 3.11 and 3.12. Reusing the producer's Python 3.11 preserves exact
comparison; no numerical budget is widened. Read-only controls pass, and
preflight-918 passes all 26 completed gates with 14 pending, zero failed and
two owner-deferred large-network gates. Preserve both preflight records.
Public network setup now pins the validated pynucastro 2.12.0 environment,
generates both packages and gives complete target/output/provider commands.
Necessary untracked source files must be included at eventual delivery, but
do not stage or commit during this frozen campaign. Per the owner's new reply,
notices will explicitly attribute Timmes-derived work and state that the author
has not yet been contacted; administrative confirmation will be supplemented
separately. Do not represent pending correspondence as permission or as a
technical test pass. Technical acceptance continues, with no release or push.

Prior checkpoint: **2026-09-07 15:24 UTC**. Active contract, all five packages
and affected AMR/validation/resource owners reread after resumption. Source
`73a9cf50...` remains frozen. Memcheck-902 is a retained TIMEOUT: 22 routes
passed, but the exact four-step audit31 sparse-cell run exceeded its 1200 s
instrumentation wall-time allowance while continuing BE_NR solves. No complete
sanitizer summary exists for that last route; this is not a passing suite.
The guard cleaned up its owned processes without a memory/pressure stop.
Measure its existing progress and provide an explicit longer instrumentation
wall-time allowance for a new complete run, preserving all inputs, ODE routes,
steps and numerical budgets. The serial 903--907 queue did not start. Run
Debug regression-910 while the instrumentation replay is prepared.
The proposed periodic-advection 3D supplement was rejected by preflight:
SmoothAdvection intentionally accepts only 1D; Gaussian has no equivalent
constant-advection input. Do not bypass either production contract. Prepare
an observation extension of the existing 3D Sedov case instead, retaining its
physical inputs, capacities, refinement thresholds and budgets. Check actual
time/domain implications before executing it; bidirectional runtime topology
coverage remains open. Capacity-908 and cold/concurrency-909 remain isolated.
No release, source edit, staging, push, or relaxation of scientific acceptance.

Prior checkpoint: **2026-09-07 15:00 UTC**. Active contract, all five packages
and validation/resource owners reread. Source `73a9cf50...` remains unchanged.
Runtime qualifier-912 PASS: all 62 canonical cases and both strict restart
suites. Burn cross-solver-878, time-879 and geometry-880 PASS. Independent
weak-cv-898, weak-Helm-899 and real sparse-900 PASS; BE_NR tolerance refinement
improves the weak errors by 100.035 and 88.702 times respectively. Sustained-901
PASS: 500-step hydro / 100-step diffusion matrices, three 12-cycle burn chains,
72 exact native Host/CUDA restores and seven fixed-time physical comparisons.
Debug-861 complete build PASS (4548.244 s, overlapping verification workload,
not a cold benchmark); catch-up-911 is a successful no-op. Debug regression-910
is still required and must wait for the serial GPU campaign. Memcheck-902 is
running the tight weak trajectory after its first nineteen routes passed;
partial instrumentation output is not a complete sanitizer pass. Continue
902--907, then Debug regression, isolated capacity-908 and cold/concurrency-909.
Public module summaries now use final-candidate data; fix the small documented
Python/CUDA-C++20 prerequisite omissions and stale reference status text.
The evidence index has read-only controls and explicit manual-review inputs;
do not close pending resource/diagnostic gates or declare public readiness yet.
No additional numerical changes, tolerance changes, Git staging or push.

Coverage audit decision (2026-09-07 15:15 UTC): whole-regrid measurements expose
an in-scope 3D application coverage gap, not a demonstrated production defect.
The existing 3D short matrices have initial refinement but no observed runtime
refine/derefine. CUDA migration tests cover 3D transfer mathematics; the full
CUDA transaction fixture is 1D, so these records must not be conflated. Add a
result-only 3D embedding of the canonical periodic-advection regrid-cycle case:
activate its two existing transverse unit domains, retain all original physical
inputs, refinement thresholds and error budgets, and require actual parent/
eight-child transitions in both directions. Reuse run_case, topology checks,
provenance and sanitizer facilities. Record normal-913 and instrumented-914/915
separately; do not alter the canonical manifests, production math or candidate
source. Preserve any failure and diagnose it before claiming completion.
The existing 902--907 queue continues; execute this supplement serially before
closing the release. Whole-regrid timing observations from overlapping runs
are measurements of complete transactions, not isolated speedup benchmarks.

Follow-on input review (2026-09-07 15:23 UTC): the proposed periodic-advection
embedding cannot run without changing production: SmoothAdvection::Setup
explicitly requires one-dimensional Cartesian geometry. Keep that guard and
the frozen source unchanged. Use the existing canonical hydro_amr_rk3_3d
Sedov case instead, with fixed observation steps 1, 5, 10, 20, 40 and 80 and
the same bidirectional parent/eight-child runtime checks. Retain its original
input, thresholds, capacity, field selection and numerical budgets; the
result-only recipe records the derived observation and does not guarantee a
pass. Existing steps 1 and 2 reach times 0.0010510504773499122 and
0.002063996860800448 with 34 leaves. The original max_blocks=128 does not
cover arbitrary full refinement of its 27 roots (216 leaves, plus transaction
staging); these short records do not establish capacity or outflow-boundary
safety at step 80. Preserve any capacity, conservation or coverage failure;
do not enlarge the pool or tune the physics implicitly.

Prior checkpoint: **2026-09-07 14:39 UTC**. Active contract, all five packages
and provenance/restart/sanitizer owners reread before runtime qualification.
Canonical curved-873 PASS 24 cases / 96 executions (1550.927 s guarded),
uniform-874 PASS 22 cases, generated-875 PASS six real audit31/weak cases,
smooth restart-876 and burn restart-877 PASS. Together with Cartesian-872,
these supply all four canonical matrices and both strict restart suites for
the unchanged source `73a9cf50...`. Run the existing full-runtime qualifier
through result-only wrapper-912 and retain its deliberately limited scope;
it is not the independent-physics/sanitizer/capacity release decision.
Burn application-878 has completed; time/geometry references-879/880 continue
in the existing queue. Then start weak/sparse/sustained/sanitizer-898--907.
Debug-861 is compiling the remaining test targets; its catch-up and full
regression remain required. No source or budget changes. Isolated cold and
capacity, complete evidence review and EN/ZH curation remain open.

Prior checkpoint: **2026-09-07 14:23 UTC**. Active contract, all five packages
and affected mathematical/backend owners reread after context resumption.
The final candidate remains source `73a9cf50...`, with Release-895 PASS 98/98
and CPU-only-897 PASS 31/31. Curved-873 and Debug-861 are still running under
their existing guards; WSL has about 4.8 GiB available and 147 MiB swap in the
latest snapshot, with no reported pressure stop. Continue the already running
serial application campaign, then the prepared weak/sparse/sustained/sanitizer
queue. Debug requires a catch-up build before its complete regression because
the policy-reference test changed during compilation. Cold/concurrency and
capacity measurements must remain isolated. Curate completed module evidence
in parallel and prepare an honest whole-release index; do not stage or commit
the frozen sources, since provenance includes Git state as well as file bytes.
All remaining in-scope gates stay open; no release or push is declared.

Prior checkpoint: **2026-09-07 14:10 UTC**. Active contract, all five packages
and AMR/geometry owners reread. Candidate source and application artifacts remain
unchanged. CPU-only-896 build PASS (214.622 s, peak owned RSS 1,062,176 KiB,
minimum available 3,324,804 KiB); CPU-only-897 complete regression PASS 31/31.
Release-895 remains PASS 98/98. The complete curved application matrix-873 is
still RUNNING with completed per-lane output; no failure or guard stop has been
reported. Debug-861 continues one-heavy/two-total compilation. Do not confuse
the lengthy three-dimensional application/Debug work with a dead process, or
declare partial output a passing matrix. Continue the already prepared serial
runtime and weak/sparse/sustained/sanitizer queues, then final Debug regression,
isolated cold/concurrency and allocation-capacity qualification. EOS, Sedov and
NSE reference descriptions have been curated in EN/ZH; final global evidence
aggregation and remaining module tables stay open. No numerical budget,
production model, large-network scope or release status is changed.

Prior checkpoint: **2026-09-07 14:00 UTC**. Five packages and AMR/geometry owners
reread at the transition to full application matrices; active contract remains
unchanged. Source `73a9cf50...` stays frozen. Full Release-895 PASS 98/98, with
no skipped tests (296.201 s under the guard). Sedov-889, NSE-890, EOS-891 and
all endpoint balances-892, gravity coupling-893/balances-894, independent burn
review-888 and probe build-887 PASS on this source. Gaussian oracle/activity-871
PASS; canonical Cartesian AMR-872 PASS all ten cases. Curved-873 is RUNNING,
followed serially by uniform/generated matrices, both strict restarts, burn
cross-solver and hydro/geometry references. CPU-only-896 and Debug-861 are
protected verification builds, not cold benchmarks. Final weak/sparse,
sustained/sanitizer, Debug/CPU suites, isolated build/capacity and complete
evidence/EN-ZH curation still remain; no in-scope gate is dropped. EOS and
built-in NSE/burn reference prose now distinguish scientific results from
overall release acceptance. No production source changes are planned absent
a reproduced failure. No release, commit, push or deferred large-network run.

Prior checkpoint: **2026-09-07 13:46 UTC**. Active contract and affected owners
reread; the five-package scope is unchanged. Release-869 is a retained FAIL,
97/98: only policy-resolution still expected pre-repair Roe-family fingerprints.
The same independent Euler/RH oracle confirms the original states/coefficient;
the test now keeps exact Host/Device equality plus independent physical checks,
rejecting old fingerprints and wrong bindings. No production formula changed.
Targeted build-885 PASS (41.200 s), actual hydro/policy tests-886 PASS (2/2).
New candidate source is
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`;
ARCH/comparator hashes remain those recorded below. Preserve earlier evidence
and repeat final-identity application/regression records. Debug-861 continues;
native probe-887 and independent burn review-888 update their source identity.
Next serial queue: Sedov/NSE/EOS and gravity endpoint checks, full Release suite,
Gaussian controls, canonical Cartesian/curved/uniform/generated matrices,
strict restarts, burn cross-solver and hydro/geometry references. Then complete
weak/sparse, sustained/sanitizer, CPU-only/Debug, isolated cold/capacity and
EN/ZH curation. No known production defect is newly identified, but all remaining
gates are still required. No release, commit, push or optional large-network run.

Build scheduling observation (2026-09-07 13:56 UTC): with about 6 GiB available
and Debug using one heavy compiler, start pure-CPU-896 with one total job and
a stricter 2 GiB available-memory floor. This bounds the aggregate heavy work
to two independent compilers, within the previously observed two-heavy regime;
both existing guards retain owned-tree cleanup and swap/PSI protection. It is
an overlapping verification build, not an isolated timing/capacity benchmark.
Cold/concurrency and allocation-capacity qualification still run alone later.

Prior checkpoint: **2026-09-07 13:32 UTC**. Contract, five packages and affected
owners reviewed at the transition from defect/application closure to full
regression. Candidate source is
`3ca4d804c3fac3970b217815106930b689232bbb26d4387e5345a8daa817a1a5`,
ARCH `9bba8f657a46ccfdf5b264387b9269566b6a7e724759027f63e4cd45aa7fe3ba`,
comparator `e5cc53d3d19a8ad547f1d990353cc55d55d884ce24dcd2e4f99621dd50825bbc`.
Three-resolution CPU/CUDA Sedov-862 PASS at unchanged budgets (59.379 s).
NSE-864 PASS: all sixteen cases / thirty-two physical endpoints, maximum
energy closure 2.270e-16 and charge drift 5.089e-13. Normalized EOS-865 PASS:
twelve cases / ninety-six executions; endpoint balance-866 PASS for all
twenty-four physical endpoints, maximum burn closure 3.713e-15. Supplemental
gravity/AMR/diffusion-867 and all six endpoint balances-868 PASS. Native probe
build-863 PASS. Complete Release regression-869 is RUNNING; Debug-861 continues
with its protected one-heavy/two-total build. Independent burn review-870 runs
CPU-only with one OpenMP thread. Explicitly add the existing Gaussian analytic/
thermal-activity recipe to the remaining queue; it is not covered by CTest.
Then finish all canonical matrices, weak/sparse science, strict restart and
sustained paths, sanitizer, CPU-only/Debug suites, isolated build/capacity and
evidence curation. Cold configuration has been aligned offline; its compile
measurement has not started. No new defect, waived gate, release or push claim.

Prior checkpoint: **2026-09-07 13:24 UTC**. Active contract, all five packages and
affected owners reread after context resumption and before final application
campaigns. Shared energy-secant negative/positive controls establish the
conditioning repair; CPU hydro leaf and unchanged independent fixtures PASS.
Release-860 PASS: 75 edges, 338.706 s, peak owned RSS 2,737,516 KiB, minimum
available 4,586,292 KiB, zero swap growth and no pressure stop. Regenerated
audit31/weak assets are restored, with durable copies and archived full-file
comparison: mathematical bytes match, audit31 generator metadata truthfully
records the current generator. A supplemental three-case gravity/AMR/diffusion
manifest and the existing comparator's AMR gravity dispatch close a coverage
gap; actual runs are pending. Capture this candidate identity, resume Debug
with one-heavy/two-total jobs, and execute Sedov/NSE/EOS before complete
regression, runtime matrices, restart/sustained, sanitizer, isolated cold/capacity
measurements and public-document curation. No tolerance or scope is reduced,
and no release, commit or push is declared.

Prior checkpoint: **2026-09-07 13:06 UTC**. Active contract, all five packages and
affected owners reread after user-requested resumption. WSL has restarted;
Debug-855 was interrupted (not a passing build), and no old jobs remain live.
Sedov-856 PASS on all three resolutions and both backends, including unchanged
independent profile/convergence/conservation/symmetry budgets. Real audit31-853
PASS for all three methods and both storage generations (535.559 s); native
restart probe-857 build PASS. The /tmp registered-network root was removed by
the restart. Restore only after comparing the complete archived package file
inventory; keep a durable copy, and do not treat an old backup as current.
Three bounded independent reviews cover documentation, recipe coverage and
shared math. The math review identifies an inherited scale-sensitive energy
secant: nextafter-separated high energies can produce incorrect Roe acoustics.
Confirm and repair its shared conditioning guard with focused negative/positive
controls before rebuilding the candidate. Preserve the passing/failed prior
evidence as prior identities. Then resume Debug/Release/CPU compilation and
the complete final campaigns; no tolerance or large-network scope is changed.
No release, commit or push is declared.

Prior checkpoint: **2026-09-07 10:37 UTC**. Active contract, all five packages and
affected ownership entries reread at the transition to final application and
regression acceptance. HLLC contact repair-848 eliminates the reproduced seed
without changing PPM. CPU hydro leaf-850 PASS with the unchanged independent
flux fixtures and new exact stationary-contact controls. Full Release-851 PASS
(38 edges, 459.562 s, peak owned RSS 2,713,668 KiB, no swap growth or pressure
stop); a no-op confirms no pending compilation. Actual CPU terminal controls-854
PASS: PCM and PPM have zero reflection error for Euler/RK2/RK3 at t=0.1; MUSCL
remains at roundoff. Three-resolution CPU/CUDA Sedov still must run. Candidate
source is `8ec4d490e4aeb66e14ca0fe25dbeabf44c38692dc08fc64ad22f80b06eab61bb`,
ARCH `126dbd48cb051e842e76536438e07afa51b401fd122009d5b931aceff2b09457`,
comparator `5773aad8f15fbe74815470e1da86e9c843070ea7639e181dc3d51513cf2d68b4`.
Architecture/header audit-852 PASS. Real audit31 sparse-853 is RUNNING on its
unchanged independent test artifact. Keep one GPU campaign; update Debug with
one-heavy/two-total jobs while small Release verification proceeds. Close
Sedov/NSE/EOS, complete suites and matrices, native restart/sustained paths,
sanitizers, isolated build/capacity measurements and EN/ZH evidence curation.
No source change is planned absent a reproduced defect. No release, commit,
push or deferred large-network build is declared.

Prior checkpoint: **2026-09-07 10:22 UTC**. Active contract, all five packages and
affected ownership entries reread after resumption. Sedov-839 FAIL: parity and
independent profile/conservation checks pass at N128, but the shared normalized
reflection error is 0.00452212. Preserve this failure and its budgets. Short-step
isolation-840 and terminal controls-843 locate the remaining amplification in
PPM across Euler/RK2/RK3; PCM/MUSCL remain at roundoff. Saved-face replay-842
finds exact reconstruction/flux reflection on those particular inputs, not a
proof for intermediate stages. Trace finite-precision symmetry in the shared
math before changing algorithms or rebuilding Debug. NSE-829 remains passing
transitional evidence. All final regression/resource/documentation gates below
stay open; no release, commit, push or large-network campaign is authorized.

Prior checkpoint: **2026-09-07 10:02 UTC**. Active contract, all five packages and
affected owners reread before consistent-artifact application acceptance.
Full Release build-806 PASS (152 edges, 3786.969 s, peak owned RSS 3,908,892 KiB,
no swap growth/pressure stop). Shared-hydro catch-up build-836 PASS (26 edges,
244.884 s, peak owned RSS 2,694,888 KiB), and no-op check-838 confirms no pending
compile work. CPU/CUDA hydro leaf-837 PASS, including all sixty policy routes
and independent thermodynamic/flux controls. NSE diagnostic-829 passes all
sixteen application cases and their original energy/charge/source budgets;
repeat it on this now-consistent artifact. Candidate source is
`7dd5deda948f4fd388a7f8911b2be3a8d291a27bafbdc67d1e74411f61d0bc61`,
ARCH `4c543d7c65ad3ec66aea4fe5cea053dfafa6d95417befbfedc0d0dc943bad3d6`,
comparator `5773aad8f15fbe74815470e1da86e9c843070ea7639e181dc3d51513cf2d68b4`.
Actual three-resolution Sedov-839 is RUNNING. Keep source/inputs fixed unless a
real gate fails. Next: NSE/EOS endpoints, complete Release/Debug/pure-CPU suites,
uniform/Cartesian/curved/generated matrices, strict restart/sustained tests,
complete sanitizer, isolated cold/incremental/concurrency and allocation/host
capacity measurements, and final EN/ZH records. Whole-device baseline is back
near 2.2 GiB in leaf-837; this is observed availability, not an allocation cap.
No release, commit or push is declared; large-network work stays deferred.

Prior checkpoint: **2026-09-07 09:45 UTC**. Active contract, all five packages and
affected NSE/burn/flux/validation ownership entries reread. Shared Roe reflection
and directional entropy-radius repairs pass the CPU hydro leaf-824 and
independent 70/90-digit fixture check-821; architecture audit-826 and all seven
validation-harness contracts-827 PASS. Build-806 has linked ARCH and the repaired
CUDA hydro leaf test; its remaining targets are still RUNNING. Early actual NSE
diagnostic-829 is running on that transitional binary, with six cases passed so
far. This burn-only diagnostic must not be relabeled as final full-build
evidence: catch up all changed hydro dependencies after -806, then repeat the
application profiles on one consistent artifact. No additional physics refactor
is planned absent a reproduced acceptance defect. Proceed through actual
NSE/Sedov/EOS, full regression and representative generated/weak routes, strict
restart/sustained paths, sanitizer, isolated cold/incremental and allocation
capacity measurements, then EN/ZH curation. The existing cold-build recipe now
also supports a same-source, cache-disabled two-route 1-job versus configured
parallel measurement; it is UNRUN, not a claimed sweet spot. About 113 MiB swap
remains, below the starting occupancy; no pressure stop. No gate is waived and
no release, commit or push is declared.

Prior checkpoint: **2026-09-07 09:25 UTC**. Active contract, all five packages and
affected ownership entries reread after resumption. NSE focused regression-805
passes; build-806 is still running (71/152 edges), without a compiler failure or
observed swap growth. Actual Sedov preflight-812 exposes a SHARED defect: CPU and
CUDA agree but both violate reflection symmetry by 1.3516% of the energy scale.
PCM first-step isolation-813 proves this is not specific to PPM. Leaf negative
control-815 rejects the directed thermodynamic average. One symmetric,
fixed-composition Roe-Glaister average now replaces it for HLL/HLLC/Roe on both
backends; focused CPU identities-817 pass (reflection 2.060e-16, ideal acoustic
identity 1.976e-16, pressure-jump residual zero). CUDA/application acceptance is
still pending. Preserve old snapshots; replace invalidated Roe-family snapshot
expectations only with independently derived ideal-gas references, retaining
the existing numerical budgets. After build-806, run a catch-up build for the
new hydro dependencies before any final candidate campaign. Then close actual
NSE/EOS/Sedov and the complete regression, sanitizer, sustained, isolated
resource/build and documentation gates. No release, commit or push is declared.

Prior checkpoint: **2026-09-07 09:05 UTC**. Active contract, all five packages and
NSE/burn/EOS/validation ownership entries reread. Full Release build-806 remains
RUNNING with the configured two-heavy/four-total pools, without a compiler error
or swap growth so far. The four-network NSE regression-805 remains focused
passing evidence; the real sixteen-case application must use the rebuilt ARCH.
Architecture/header audit-810 PASS. The remaining strong-shock similarity gate
now has one independent planar reference in the existing hydro validation
directory; oracle-808 and finite-volume quadrature-809 PASS. Its failed printed
normalization check-807 and original recipe are retained and reconciled by two
independent Euler-similarity integrations, not by changing production physics
or application budgets. Actual three-resolution CPU/CUDA Sedov remains UNRUN.
Keep the production candidate stable, then close NSE/EOS/application/regression
gates before sanitizer, sustained, isolated capacity/cold-build qualification
and final EN/ZH evidence curation. Whole-device available memory is fluctuating
below about 1.5 GiB with no ARCH GPU process observed; larger GPU work must wait
for suitable free capacity. No unrelated program is closed. No gate is waived,
and no release, commit or push is declared.

Prior checkpoint: **2026-09-07 08:48 UTC**. Active contract, all five packages and
NSE/burn/EOS ownership entries reread after resumption (also reread at 08:43:34).
The shared input-residual certification preserves an already converged NSE
state at the existing Saha/mass/charge tolerances. No energy cutoff or backend
branch was added. Shared-leaf replay-803 retains the original first projection
and removes the artificial repeated source. Four-network build-804 PASS
(312.206 s, peak owned RSS 1,875,588 KiB, no swap growth/pressure stop), and
regression-805 PASS: original independent references and negative controls,
exact repeated fixed points, and composition/temperature/density perturbations.
The application binary still predates this repair; rebuild it and the complete
Release test targets before rerunning the failed sixteen-case NSE profile.
Final regression, generated/weak application routes, strict restart/sustained
paths, sanitizers, isolated resource/build metrics and documentation remain open.
Whole-device memory baseline rose above 6 GiB; the owner was asked about other
GPU work. Do not terminate unrelated programs or confuse contention with a
numerical failure. Small regression-805 used a 6207 MiB whole-device peak, with
1818 MiB minimum free. No release, commit or push claim follows from this test.

Prior checkpoint: **2026-09-07 08:21:53 UTC**. The owner renewed execution through
final acceptance, starting with the NSE discrepancy. Active contract, all five
packages and affected NSE/burn/EOS ownership entries reread. Begin a bounded
same-input Host/Device replay of the shared preparation, NSE projection and
energy handoff to isolate the first differing substep before another full core
build. The failed application and original budgets remain unchanged. Subsequent
gates remain full candidate regression, representative generated/weak routes,
strict restart/sustained paths, complete sanitizers, isolated build/capacity
measurements, pinned assets and synchronized EN/ZH documentation. No release,
commit or push is authorized by this checkpoint; no large-network work resumes.

Prior checkpoint: **2026-09-07 08:03:38 UTC**. Active contract, all five packages
and affected burn/EOS/validation owners reread after resumption. Core build-792
PASS (107 edges, 2696.759 s, peak owned RSS 3,856,668 KiB; no swap growth or
pressure stop). EOS polynomial/derivative regression-794 PASS. Actual normalized
EOS application-795 PASS: all twelve rank-3/rank-4, direct/free-energy hydro and
BE_NR/BD/ROS4 burn cases, including prescribed-time acceptance. Independent
physical-endpoint conservation and nuclear-energy balance-796 PASS. These close
the reproduced EOS-783 handoff failure at the unchanged budgets; its failed
evidence remains archived. Candidate identity is source
`7672760743a3ac1ecb3ea7edbe3363a9806c686a9fe72e2a50b67ecbdb07d6fd`,
ARCH `fb15fff3b5aefdb0d80858953071649166999d675351516ea04c4cb2aab21a13`.
The corrected policy-fixture regression-797 PASS (16/16); its whole-device GPU
peak was 6484 MiB, again not a 2.4 GiB cap. Endpoint-796 has 24 CPU/CUDA records;
the largest independent burn-energy closure residual is 3.712513681948007e-15
against the existing 1e-12 budget.

NSE application-771 FAIL at `aprox21_be_nr_nse_True`, after the aprox13/aprox19
on/off and ODE cases and the aprox21 disabled control passed. The serial-798
launcher stops there; uniform-772 and generated-773 have NOT run. At the same
physical time 1e-16, saved CPU/CUDA density, momentum and total energy are exact;
the largest native-X difference is 4.440892098500626e-16. The reported final
burn-half ENUC values are -9.093229235909936e20 and -8.032352210135081e20,
an 11.66667% relative mismatch. Their difference integrated over the 5e-17
burn half is about 5304.4 erg/g, versus specific internal energy about 1.63e19
erg/g. This localizes a strongly amplified, near-equilibrium source diagnostic;
it is not proof of an 11.7% state/energy error, nor permission to mask ENUC or
relax its budget. The shared NSE solver already computes binding-energy changes
from composition differences. Trace its successive projection inputs and the
shared EOS-to-temperature handoff before selecting a numerical repair; do not
presume the previous reconstructed-EOS subtraction is still the source.

Next: diagnose/close actual built-in NSE, uniform/generated applications,
fresh complete Release/Debug/CPU suites,
strict restart/sustained paths, sanitizer, capacity and isolated build metrics.
Do not inherit the previous source's passing suites or declare release.
Whole-device GPU telemetry reached 3854 MiB in EOS-795 and 3931 MiB in policy-791;
the approximately 2.4 GiB baseline is not a fixed ARCH/WSL VRAM budget. This is
whole-device observation, not per-application allocation or a capacity result.

Prior checkpoint: **2026-09-07 07:50:35 UTC**. Active contract, all five packages
and affected burn/EOS owners reread. Core build-792 continues without a compiler
error or pressure stop. All built-in sparse EOS combinations and most registered
audit31/weak routes have compiled; the application/link/EOS replay remains open.
No source repair or acceptance change was added during this build. Keep this
candidate stable for the helper/EOS tests and the actual twelve-case normalized
EOS application campaign, then built-in NSE and the remaining final-profile
queue. The existing audit-793 passes; no release gate is closed by progress
through compilation alone.

Prior checkpoint: **2026-09-07 07:30:33 UTC**. Active contract, all five packages
and affected burn/EOS ownership entries reread during core build-792. Its
107-edge target set includes ARCH, the actual EOS test, checkpoint comparator
and corrected policy helper. Through edge 37, the built-in Tabular3D/4D routes,
registered table routes and aprox13/19/21 Ideal/Helm dense routes have compiled;
the build is still RUNNING, not passed. Architecture/header audit-793 PASS
(4.032 s). Focused regression-790 and the fifteen passing real-network/status
routes from -791 remain bounded evidence; the helper replay and full coupled
EOS/NSE applications are not yet complete. No physical budget, final-time
contract, provider selection or optimization setting changed. Continue the
existing serial validation/resource queue after the application is consistent;
do not start another broad refactor or optional giant-network build.

Prior checkpoint: **2026-09-07 07:10 UTC**. Active contract, all five packages
and affected burn/EOS ownership entries reread. Build-789 PASS; regression-790
PASS 4/4. Real-network policy/status regression-791 PASS 15/16: all twelve
built-in network/ODE routes and all three NSE/status routes pass unchanged.
The helper fixture still returned a default-zero energy report for its scripted
temperature/composition change. Its test-only solver now reports that prescribed
energy; original expected values/budgets remain unchanged. The full helper replay
is required. No production fallback to EOS energy subtraction was added.

The resource observation also disproves a 2.4 GB VRAM cap: regression-791
whole-device usage rose from 2449 to 3931 MiB out of 8192 MiB. This is not
application allocator peak accounting. The process guard monitors GPU memory,
but only host memory/swap/sustained stalls trigger its resource stop; sparse
device workspaces size against actual available memory, not a fixed 2.4 GB
limit. WSL VM RAM and device VRAM remain separate quantities.
Next: application/EOS/helper rebuild, real EOS/NSE replay, then complete serial
final-profile regression, sanitizer, resources and curated documentation.

Prior checkpoint: **2026-09-07 06:50:54 UTC**. Active contract, all five packages
and burn/EOS ownership entries reread after resumption. The accepted-energy
report and CPU/dense-CUDA/sparse handoff repair are implemented but UNVERIFIED.
Shared tabular interpolation now reuses Horner bases and removes its constant
background before compensated contraction; inversion reuses one energy/cv
query. Focused signed/sub-ULP source, rejection, erased-handle and independent
polynomial controls precede the next core build. No ODE extent, provider
boundary, physical budget or final-time acceptance changed. Reopen affected
final-identity gates; all previous passing and failed records keep their actual
identities. Next: focused compile/regression, actual EOS/NSE replay, then the
remaining serial qualification/resource/documentation queue.

Focused follow-up, 07:01 UTC: regression-790 PASS 4/4 (shared CPU continuation,
CPU reduction/handoff, CPU/CUDA thermal math and CPU normalized table EOS).
The exact constant sub-ULP species-transfer control first FAILS-788 because
closure still used rounded endpoint abundances, forcing spurious retries.
BE_NR/BD/ROS4 closure now consumes the same raw increment energy as handoff;
the original budgets are unchanged and the control passes without rejection.
Build-785 was deliberately stopped through its owned guard after the EOS test
pulled in 87 backend edges; it is NOT a build failure or completed build.
Build-789 excludes that broad dependency for focused validation; the full
core/EOS application rebuild follows. Neither phase has caused swap growth
or a pressure stop. The EOS application recipe changes only execution order
to check burn first; all twelve cases and their original budgets remain.

Prior checkpoint: **2026-09-07 06:35:11 UTC**. Active contract, all five packages
and burn/EOS ownership entries reread for the repair phase. EOS-783 FAIL after
the manufactured free-energy hydro/AMR fixed-time case passed. First BE_NR
burn step (1e-16 s) reports ENUC 4.466688e22 on CPU and 4.62848e22 on CUDA:
3.49558% relative difference, rejected under the original budget. C12 and O16
match exactly, trace-species differences are at most 1.4021e-23, and independent
binding-energy increments differ by only 1.2885e-4 erg/g. In contrast, subtracting
the two reconstructed 4.5e17 erg/g EOS states produces net energy changes
4,479,006.65625 and 4,520,989.96875 erg/g. DriverBurnPolicy currently forms ENUC
and its limiter from that small difference of large EOS energies.

Repair the shared energy handoff, not the acceptance budget: retain the signed
energy of actually accepted ODE increments (including the existing weak source)
in BurnOdeReport, propagate the same scalar through CPU erasure / dense CUDA /
sparse continuations, and update conserved energy, ENUC and its limiter from it.
Rejected trials contribute nothing; NSE supplies its already-computed accepted
projection energy. This adds no ODE equation, changes no DenseLU size boundary,
and must retain the existing EOS failure latch and atomic commit. Tiny-heating,
signed-loss, rejection and real tabular application controls must validate it.

The shared free-energy tensor also repeats general powers/basis evaluation in
each output/corner contraction. The small 30-step CUDA hydro case took about
fifteen minutes despite O3 and normal device clocks; it was advancing normally,
not out of memory. Review basis reuse/Horner evaluation and conditioning in the
existing TabularFreeEnergy owner, with independent polynomial/thermodynamic
tests and actual-program timing. Do not add a backend-specific interpolator.
EOS-783 used 1083.392 s for its completed and failed lanes; peak owned RSS
350,644 KiB, whole-device peak 2,977 MiB, no swap growth or pressure stop.
The failed recipe and all outputs are retained. Subsequent source changes reopen
affected final-identity evidence; the remaining serial queue has not run.

Prior checkpoint: **2026-09-07 06:11:54 UTC**. Active contract, all five packages
and affected ownership entries reread. Production sources and binaries remain
unchanged: source fingerprint `ab2e3d5a0faad7efa25975cf4a3cec074569fdd973ba117fbd3818e17aa7bfed`,
ARCH `e5c774cbf58c3690c38fdcd8234d6fdea68947a01eea64c65e297ca393b6a1ae`.
The complete Cartesian/curved and restart summaries now link to native-format
passing records; sustained exact restoration is documented separately from
forward adaptive-step diagnostics. Geometry, dispatch splitting, restart and
AMR coverage checkboxes cite those records and Release regression-754; this
does not close the remaining full-profile gates.

Serial launcher-770 first failed before execution because resolving the HDF
venv interpreter symlink selected system Python. The archived failed launcher
is retained; selecting the environment entry path fixes that setup. Actual
EOS-770 then failed its first forward-step clock comparison: relative delta
5.4326e-12 (28331 ULP), while native field differences pass original budgets
(maximum field-normalized delta 1.8928e-13). No clock budget was widened.
EOS-783 uses the owner's required same-physical-time acceptance and keeps
intermediate steps explicitly diagnostic. Its CPU first manufactured endpoint
passes pressure error 2.9758e-10 and density L1/mean 9.0287e-5 against the
declared 1e-3 budget. GPU fixed-time execution is still running, not passed.
The final conservation postprocessor reuses the common volume-integral checker
and independent nuclear data reader; it does not introduce another EOS/ODE.

Cold-tree configure-784 PASS (7.028 s, peak owned RSS 26,220 KiB, no additional
swap). The actual optimized compile commands retain O3, intended CPU dispatch
flags, strict floating-point semantics and Release IPO. No cold compilation has
run yet. Next: complete EOS/NSE and the remaining serial application/regression/
sanitizer queue, refresh independent weak/geometry/time records, run isolated
allocator and cold/incremental measurements, then final identity/docs closure.

Prior checkpoint: **2026-09-07 05:53:31 UTC**. Active contract, all five packages
and affected ownership entries reread after resumption. Curved-native-764 PASS:
all 24 canonical cases / 96 executions; 1529.080 s, peak owned RSS 1,912,976 KiB,
minimum available 2,704,996 KiB, no guard stop. Cartesian-native-768 PASS:
10 cases / 54 executions. Debug IO catch-up-765 PASS: 27 edges, 86.337 s;
all IO consumers now use the same schema-4 layout. Release-754 (98/98),
CPU-only-755 (31/31), both short restart suites and sustained-native-763 remain
passing records with their actual identities.

Uniform-766 failed at CUDA context initialization (`cudaSetDevice: out of
memory`) while multiple GPU campaigns overlapped. Preserve this failed run;
concurrent device pressure is a diagnosis to verify by isolated replay, not
yet a proven explanation or a numerical pass. Generated-767 and Debug-769
were deliberately canceled through their owned guards, not completed. From
this checkpoint, execute one GPU campaign at a time. Isolate capacity and cold
build measurements from other heavy workloads. Existing RAM/swap/PSI protection
remains enabled; do not introduce hardware-specific physical parameters.

Immediate action: normalized-table hydro/AMR and burn application coverage-770,
then built-in NSE activation/energy closure, isolated uniform/generated/Debug
refresh, full sanitizer paths, allocator capacity, cold/no-op/incremental core
timing and final English/Chinese curated summaries. New supplemental recipes
reuse the common execution, comparison and provenance infrastructure; independent
manufactured thermodynamics and source-aware balances are test-only oracles.
No release declaration or push is authorized by this checkpoint.

Prior checkpoint: **2026-09-07 05:28:16 UTC**. Active contract, all five packages
and affected owners reread. Sustained-native-763 PASS: 500-step hydro and
100-step diffusion, twelve strict alternating smooth restores, twelve burn
restores in each CPU/CUDA/alternating pattern, 72 exact native snapshot
comparisons, and seven mandatory same-prescribed-time burn comparisons at
1e-10 s. Shared read_chk, actual device upload/materialization and write_chk
preserve all fields, time/controller values and output metadata exactly. The
probe uses existing backend/IO/CFL implementations and hashes the candidate,
linked objects/libraries, recipe and its binary. It introduces no physics body.

The failed step-aligned recipes-740/753 remain retained. Probe-760 proves the
failure source's native transfer and initial CPU/CUDA CFL are exact; the later
time difference is generated during forward evolution. No clock threshold was
enlarged. The owner's temporal distinction is enforced explicitly: strict
restoration and same-backend continuity are separate from independently evolved
forward-step diagnostics; fixed-time scientific checks are mandatory. The
original strict four-direction/intermediate/terminal suites are unchanged and
both pass on the current candidate (burn-757 and smooth-758). A diagnostic-only
comparison never substitutes for physical-time acceptance.

Full Release regression-754 PASS 98/98; pure-CPU regression-755 PASS 31/31.
Fresh Debug build-737 PASS (4287.468 s, peak owned RSS 2,427,416 KiB, minimum
available 2,489,600 KiB; no pressure stop). It overlapped small campaigns, so
this is not an isolated speed/capacity measurement. All fourteen IO consumers
were recoverably backed up in `/tmp/arch-debug-io-objects.9oR9kD/manifest.json`
before catch-up build-765; Debug execution waits for that consistent layout.
Current canonical matrix refreshes: curved-764, uniform-766, generated-767,
Cartesian-768. Remaining gates: complete Debug/application and sanitizer
coverage, EOS/NSE application coverage review, isolated allocator/host capacity,
cold/no-op/incremental core measurements and final EN/ZH evidence summaries.

Prior checkpoint: **2026-09-07 05:07:23 UTC**. Active contract, all five packages
and affected IO/comparison owners reread. Native-composition repair is verified
by diagnostic-746: CPU and CUDA each reproduce 26-step fields, clock, controller
and output history exactly after twelve same-backend restores. Late-step four-
direction diagnostic-748 also passes the unchanged strict comparison. Current
full Release regression-754 PASS 98/98; pure CPU build-751 PASS (193 edges,
125.089 s, peak owned RSS 1,782,756 KiB; concurrent, not a cold timing), CPU-only
regression-755 PASS 31/31. Regression-749's one failed contract was an obsolete
unit-only field-list adaptation, fixed without changing historical evidence;
focused tooling-752 passes all five tests and the new invalid-time controls.

Sustained-753 passes 500-step hydro, 100-step diffusion and both same-backend
burn chains, including fixed-time continuation comparisons. Its alternating
chain fails the strict two-step forward-clock comparison at switch eight
(step 16 to 18): delta time 5.73e-24 s, 3547 ULP, about 1.57e-12 of the last
step. This is not declared a pass or silently assigned a larger clock budget.
Next: inspect the actual loaded native state and first forward CFL calculation
separately, then apply the owner's distinction between exact state restoration
and same-physical-time scientific evolution. No EOS/ODE formula has changed.
Fresh Debug build-737 continues; it needs IO-consumer catch-up. Final application
matrix refresh, sanitizer, isolated capacity and cold/no-op/incremental timing,
remaining EOS/NSE application coverage review and final documentation remain.

Prior checkpoint: **2026-09-07 04:47 UTC**. Contract, all five packages and
affected owners reread after resumption. Active gate: close the long burn
restart failure before final-identity qualification. Sustained-739 exposed an
invalid final-mesh assumption in the short-fixture wrapper; retained failure
and history-based topology checks distinguish it from a physics defect.
Sustained-740 passes 500-step hydro, 100-step diffusion and twelve alternating
SmoothAdvection restores, but strict long BurnGradient timing fails. Same-CPU
chained diagnostic-741 also fails, so this is not solely backend evolution.
Actual checkpoint/plot data prove that saving only rhoX loses native X by one
ULP on restore (rounded multiplication followed by division is not invertible).
The shared IO now writes schema 4 with native Data/X alongside conserved rhoX,
validates their consistency and reads schemas 1–3 through their existing path.
No physical formula, integration tolerance or strict time budget changed.
Focused compatibility/temporal/conservation tests-744 PASS (3/3), including the
actual lossy witness, old-file reads and missing/corrupt native-X rejection.
Release IO rebuild-745 PASS (66.628 s, peak owned RSS 1,090,608 KiB, no swap
growth). Long same-backend repair diagnostic-746 is running; its success is
not assumed. This source/binary change reopens affected application evidence.

Pre-schema-4 candidate evidence is retained, not relabeled as final: Release
regression-726 98/98; uniform-727, hydro time-728, generated runtime-729,
Cartesian-730, all 24 curved cases-731, real audit31-732, short restart-733/734,
independent geometry-735 and built-in cross-solver application-738 all PASS.
Memcheck-742 passed 19 configured routes and the tight constant-cv weak
trajectory, then was deliberately canceled during the Helm weak trajectory
before the IO edit; it is incomplete,
not a clean full sanitizer result. Fresh Debug build-737 is still running and
will need explicit IO-consumer catch-up before execution. Remaining gates are
strict sustained restart, final-version runtime/regression and sanitizer,
isolated device/host capacity, cold/no-op/incremental build measurements,
CPU-only build compatibility, and final EN/ZH evidence/document synchronization.

Prior checkpoint: **2026-09-07 04:06:56 UTC**. Contract, all five packages and
affected owners reread. Active gate: finish curved application/sustained and
sanitizer evidence, then isolated capacity/core-build measurements and fresh
Debug/CPU-only build compatibility. Current Release PASS: uniform-727 (22 cases,
180 executions), independent hydro temporal-728 (18 executions; Euler/RK2/RK3
orders approximately 1/2/3 on both backends), generated/Helm-729 (all six cases,
48 executions), Cartesian-730 (10 cases, 54 executions), audit31 trajectory-732
(all three ODEs and both storage generations), both restart suites-733/734
(12 executions/nine comparisons each), independent geometry-735. Curved-731
continues. Physics budgets and ordinary validation authorities are unchanged;
some small correctness campaigns overlap, so their timings/whole-device peaks
are not isolated performance/capacity measurements. Curated hydro/diffusion/
gravity CSV and EN/ZH summaries now use these actual observations; historical
records retain their original identities. Complete Release regression-726
PASS: all 98 configured tests, no skipped/missing cases; 285.228 s, peak owned
RSS 523,028 KiB, minimum available 6,207,688 KiB, no additional swap or pressure
stop. Before/after source, executable, tool and dependency identities match.
Uniform application campaign-727 is running on optimized rebuild-719 PASS
(93 edges, 2361.892 s, peak owned RSS 3,918,356 KiB, minimum available
2,801,832 KiB, 1,536 KiB additional swap, no pressure stop). Test-only catch-up
build-725 PASS (4 edges, 8.046 s, peak owned 631,500 KiB). Weak/Helm independent
gate-723 PASS: both coarse and tight science controls, 88.7023x BE refinement,
maximum tight normalized error 6.51e-13. Constant-cv Urca gate-724 PASS: the
coarse scientific failure is preserved, tight error 3.3544e-8 passes the original
1e-7 budget with 100.035x BE refinement. These observations retain their actual
binary/source identities. Both independent gates include the real typed factory.
The 97/98 result-704 isolated the unchanged ROS4
status gate. Independent negative-706 proves the shared factor-failure path
bypassed stall/NSE-reset handling. It now reuses the existing rejected-trial
handler; focused CPU/CUDA repair-718 PASS, including exact report/state checks.
All original scientific and status budgets remain. Build-719 uses two heavy /
four total jobs with RAM/swap/PSI protection. Prior first-law full build-681
PASS (166 edges, 2613.535 s, peak owned RSS 3,918,996 KiB, minimum available
2,827,136 KiB, no swap growth); it is not a cold-core benchmark. Canonical
weak-owner relink-702 PASS. Complete application matrix, sanitizer,
sustained, cold/incremental, capacity and fresh Debug qualification are still
open. The full contract, five packages and affected owners were reread.
Allocator-trace probe-721 PASS on the ROS4-independent migration unit: 199
allocations/frees, 20,070,404 peak requested device bytes. This is a small
instrumentation witness, not whole-program capacity. The same Nsight data
reader has five negative/schema/process-isolation controls; root Python
regression is 174 PASS. Raw profiler databases remain local because they can
contain unrelated environment metadata. The capacity recipe reuses ordinary
regrid, sparse and main-program validators, with optional 16,384-row provider
coverage. New report output/assertions in the existing provider/transaction
tests pass in build-725 and full regression-726. Cold-core configure-722 PASS
(14.104 s, peak owned 100,248 KiB, no swap growth); no cold build has started.

Previous checkpoint (2026-09-07 02:20:19 UTC): real weak/Helm
independent thermodynamics. Shared first-law repair passes analytic energy rate,
full Jacobian and fourth-order ROS4 convergence on CPU/CUDA (629), complete
Helm/Tabular analytic derivatives (653), independent built-in time/energy review
(658) and all twelve built-in CPU ODE routes (659). Weak/Helm independent
DOP853/Radau time/energy comparison-672 PASS. Restoring the original scalar
source-before-gradient ordering reproduces the NaN-674; switching only the
ordering passes-676. PTX-677 shows the input state and discarded total-energy
output using the SAME local address in that call, with no restore before the
gradient. The generated scalar accessor now retains the common heavy-entry
boundary (678/679); the original failing order, with NO extra state probes,
passes-680. No table/ODE/derivative formula or budget changes. Full archived
weak/Helm tolerance refinement and sanitizer replay remain next. Full optimized
rebuild-681 is running with two heavy / four total jobs and pressure protection.
The weak/Helm test now links the canonical EOS-owning backend archive instead
of importing its OBJECT targets; architecture regression passes after this
ownership repair. External-gravity RK2/RK3 exact acceleration checks are added
to the canonical uniform matrix at the original 1e-12 budget. They read the
actual run parameters rather than inventing a second resolved gravity policy.
The full audit also restores the nine canonical hydro reconstruction and six
diffusion fixed-time records to the uniform matrix (now 22 cases). Original
CPU final-pair L1 criteria and RKL1 Linf budget are retained; the existing
convergence checker now represents final-pair versus all-pair scope explicitly.
An independent semi-discrete Fourier contact reference is added for actual
Euler/RK2/RK3 temporal convergence. These new runtime checks are pending,
not replaced by earlier step-only comparisons. Runtime sanitizer integration
and four negative controls pass; root Python regression was 167 PASS before
these last convergence controls. The sustained recipe extends canonical AMR
cycles and chains intermediate restart states using the same readers/validators.
Debug profile configure-701 PASS; compilation is not yet started.
Added temporal
qualification negative controls PASS (39 provenance tests), EOS protocol controls
PASS (7 weak-reference tests). Whole-regrid timing/transfer recording is added
separately from nested backend traces; its build/runtime checks are pending.
The contract/five packages and affected burn/EOS/validation owners were reread.
Physical-time weak application parity-625 passes on its PRE-REPAIR binary;
do not substitute it for independent thermodynamic acceptance or the required
post-repair application refresh. Prior audit31-621, both focused 18-route
sanitizer campaigns and regression-613 retain their recorded identities.
Full curved matrix-601 PASS on its recorded pre-accounting candidate.
All 97 then-configured CTests passed on the PRE-FIRST-LAW-REPAIR candidate,
including exchange/composition/counter regressions. The temporal-control addition
brings the configured inventory to 98. Weak scientific/factory matrix-588 passes
on its prior candidate. Unchanged-budget uniform/Cartesian/restart verification
passed on the pre-repair identity (617–620, 192 executions).
Weak application time acceptance is now authorized as specified above; full generated,
sanitizer and final capacity gates remain open. The active contract, all five
packages and affected EOS/IO/validation/resource ownership entries were reread.
Controlled heavy-pool measurements select two heavy jobs; four total jobs are
the tested local ceiling, not a proven global optimum. Final isolated cold-build
and whole-regrid capacity/timing gates remain open. Large
generated workload qualification is deferred by the owner, not marked passed.

### Historical early checkpoint summary

This paragraph preserves the earlier investigation sequence. Its proposed next
actions are historical; the active contract and current checkpoint below govern
execution, including the owner's later large-network deferral.

BD local regression is CLOSED:
first amplification isolated to compact LU pivoting (trace-89, matrix-91),
shared fix passes independent controls and frozen CPU references, and the
actual program passes original `burn_bd_two_block` at steps 1/2/5/10 (log-101,
step-10 dt_burn relative difference zero). All 16 built-in policy tests PASS
(log-102). Full release identity and final Debug/Release reruns remain OPEN.
The independent mass reconversion has been removed. Shared fourth-order
complete-RHS temperature differences and compensated energy products pass
Host/CUDA analytic/unit controls; the regenerated audit31 full math comparison
passes the unchanged 2e-10 budget (log-113). Audit150 build-115 PASS (780.367 s,
peak 2,268,032 KiB, zero swap growth), but scalar energy derivative FAIL-119:
8.1183645199740207e19 Host versus 8.1183645272656445e19 Device; other checked
arrays pass. The precision-based fourth-order step alone is not a complete fix.
Separately, analytic reaction A -> B with cv=T proves the assembled thermal
Jacobian omits -enuc*dcv/dq/cv^2: ROS4 temperature converges only at first order
(probe-116), versus fourth order when ONLY the missing Jacobian entry is supplied
(control-118). Common assembly and analytic cv=T / cv=T(1+X_B) Host/CUDA
Jacobian/convergence tests PASS-124; all six trajectory errors are identical.
The unchanged frozen-main check FAILS at aprox19 BE_NR; fixtures remain intact.
Independent DOP853/Radau integrations of the production shared RHS agree at
roundoff (log-130), and temporal refinement of the actual built-in ODEs is now
being compared against them before deciding how to handle the historical
numerical baseline. BE_NR's Newton-only step controller has now been corrected
to use an independent local time-error estimate. The analytic tolerance control
passes Host/CUDA-147 (8.52x error improvement for a 100x tighter local budget);
original HEAD BE_NR fails the same control-150 (identical error, one step).
Generated energy conditioning is now corrected through a conserved-baryon
mass reference: diagnostic-131 gives 2.21e-12 relative derivative disagreement;
regenerated audit31 math PASS-141. Audit150 build PASS-143 (902.101 s,
peak 2,306,688 KiB, zero swap growth), actual Host/CUDA math PASS-155
(neq=151, nnz=2665, unchanged 2e-10 budget). Sandbox-only attempt-154 could
not see the GPU and returned 77; it is not a pass. Audit200 is configured-151.
Final packages: `/tmp/arch-network-conserved-energy.YihZtU`,
generator SHA-256 `682883b0931d5280c7292e85a66940b8fb1dc647a0143ac9435491cc62c9b252`.
Owner's subsequent constants direction supersedes relocation-only preservation:
one current disciplinary set using SI definitions / CODATA 2022, no per-consumer
or legacy-version profiles. Common analytic EOS/NSE/transport values are being
updated; deferred network/table data retain their explicit data authority.
Old EOS/NSE bit snapshots must be reviewed against independent current-constant
oracles, not overwritten from the implementation. Tiny constants/constexpr
checks accompany the ongoing affected Validation rather than a separate suite.
Next: finish actual 200 math and built-in independent trajectory review,
then weak ownership/losses and the remaining ordered release gates.
Weak-table/loss ownership and full production trajectories remain OPEN.
BD's algorithm and driver energy/controller definitions are unchanged; BE_NR's
internal accuracy controller changed for the independently demonstrated defect.
Historical burn fixtures are unchanged; constant-sensitive EOS/NSE references
were independently reconciled as explicitly described in the current checkpoint.
Completed header-dependency work remains protected by its existing tests; do
not restart an unbounded header split instead of closing numerical gates.

## Historical implementation checkpoint (before final acceptance)

- Shared ROS4 focused repair-718 PASS on CPU/CUDA: finite provider factor and
  stage-solve failures, actual nonfinite DenseLU rejection, max-substep ordering,
  exact accepted-state / recommended-step preservation and existing first-law,
  Jacobian, convergence, energy and bound-network controls. Tests record report
  fields explicitly: diagnostic-712 found correct live context values but a
  corrupted low word in an aggregate-copy report; PTX-714 loads that word from
  context padding (+340) instead of dt (+344). Memcheck-713 found zero memory
  errors/leaks but the assertion FAILED; it is not a qualifying sanitizer pass.
  Separating the assignment expression-716 verifies live context but still
  exposes the copy error. Logical-field recording-718 passes BOTH exact context
  and returned-report checks. This is test observation plumbing, not a second
  ODE implementation, relaxed precision or an extra production calculation.
  Diagnostic bit masks are removed from the maintained test in favor of named
  predicates; full optimized rebuild-719 and 98-test replay are next.
- ROS4 failure-path negative-706 confirms the same shared defect on CPU/CUDA:
  persistent factor rejection and an actually nonfinite rate matrix reach
  MaxSubsteps (11 attempts / 10 rejects), while stage-solve failure correctly
  reaches Stalled (4 / 4). The separate two-substep budget remains 3 / 2.
  FactorReady now calls the EXISTING rejected-trial handler with solve_failed;
  no stage values are consumed and no state/recommended-step commit occurs.
  Existing NSE/status expectations are unchanged. Focused repair and full
  final-identity regression remain to run.
- Shared thermodynamic leaf suite-653 PASS on CPU/CUDA, 2.011 s. Factoring
  the exact quintic endpoint zeros closes the original derivative budgets:
  maximum relative derivative error 6.84e-10, cv_Ye 4.13e-10, with the unchanged
  decimal-grid independent fixtures and 2e-9 derivative budget. All 36 monomials
  and both Tabular value/free-energy coordinate/Hessian/fallback controls pass.
  Standalone built-in reference rebuild-657 PASS, 36.180 s / peak owned
  824,852 KiB / no swap growth. Independent review-658 PASS for all four real
  networks: DOP853/Radau at two resolutions and separate 60/80-digit first-law
  energy balance. Independent endpoints replace the unphysical q/cv endpoints;
  historical main snapshots remain untouched. All twelve production CPU ODE
  routes PASS the unchanged scientific budgets in CTest-659, 101.129 s.
  Attempt-655 overlapped the final link and was correctly rejected by the
  artifact-identity guard before any scientific run; it is not passing evidence.
  The real weak test now reuses the same driver for constant-cv and Helm views.
  Helm-661 has finite physical endpoints and passing energy balance but an
  unresolved focused parity FAIL; the failing-field diagnostic is compiling-664.
  No full-program refresh or release declaration has occurred.
- Anti-drift reread 00:50:55 UTC: active contract, five packages and affected
  burn/EOS/validation owners reviewed. No full-program build or new large-network
  build has been started. Tabular3D/4D analytic interpolation derivatives and
  the shared species chain rule now replace their nested numerical fallback;
  independent polynomial, coordinate, fallback and mixed-Hessian controls are
  in the existing EOS test. All actual measured builds remain memory guarded.
  Helm decimal-grid construction now delays binary64 rounding until after the
  exponent/power calculation: this closes the pressure-density witness failure.
  Its near-cancelling pair-dominated cv_Ye derivative still fails-642/646 the
  unchanged 2e-9 relative budget (4.45e-9 in 646). The separate independent
  binary64-grid oracle-643 distinguishes coordinate quantization but is NOT a
  replacement pass or a loosened budget. Factored exact quintic endpoint zeros
  are under check-648; existing decimal-grid fixtures are unchanged.
- Shared first-law focused CPU/CUDA acceptance-629 PASS (2.012 s): analytic
  energy-rate and complete Jacobian controls, tolerance response, rollback,
  signed quadrature and fourth-order ROS4 composition-dependent EOS trajectory.
  This does not replace real-network independent energy acceptance.
  Independent Helm reference-630 agrees at 60/80 digits and satisfies Maxwell's
  identity. The new third-Hermite derivative coefficient failed polynomial
  control-633, was corrected algebraically, and passes all 36 monomials-636
  (maximum scaled error 1.56e-12). That run then exposed a 1.01e-8 relative
  second-density derivative conditioning error in the radiation/pair-dominated
  witness, above the declared 2e-9 budget. Shared interpolation now forms the
  density endpoint difference before rounding temperature contractions; guarded
  focused build-637 checks this same polynomial, without changing any tolerance.
  Preserve all failures and do not count a diagnostic relink as a final build.
- NEW SHARED PHYSICS BLOCKER-626: independently differentiate
  e=T^2(1+X_B)/2 with A'=-A, B'=A, nuclear q=A at (A,B,T)=(0.5,0.5,1).
  Existing temperature RHS q/cv yields de/dt=0.75 although q=0.5.
  Fixed-density thermal closure needs (q-sum(e_Xi X_i'))/cv and its complete
  Jacobian. This affects both CPU and CUDA; parity alone does not establish
  energy accuracy. Preserve T-based layout and the DenseLU/sparse boundary.
  Analytic Ideal/Helm derivative adapters and common first-law repair are now
  in progress; all later final-identity gates must use the repaired candidate.
  Prior weak-625 remains a genuine BACKEND PARITY pass, not independent
  qualification of this thermodynamic closure. No existing archive is rewritten.
- Physical-time acceptance-625 PASS: all three real weak/Helm ARCH routes,
  24 executions / twelve comparisons. Both backends actually reach t=0.01;
  terminal peak-normalized differences are BE_NR 1.332e-11, BD 8.177e-10,
  ROS4 8.531e-11 (including ENUC), below the unchanged 2e-8 field budget.
  Step-5 time/controller differences remain visible in step-diagnostic records,
  not mislabeled as reproducibility passes. Strict restart mode is unchanged.
  Comparator build-622 and temporal negative controls-623 pass; 25 backend
  tooling / 37 provenance tests pass. Invocation-624 used the wrong ARCH path
  and was rejected before a simulation; 625 uses the configured bin/ARCH.
  Guarded time 51.160 s, minimum available 6,540,144 KiB, peak owned RSS
  300,204 KiB, no swap growth or pressure stop. Archive:
  `validation/network/results/runtime-weak-physical-time-20260907/backend-validation-evidence.json`.
  Remaining: independent derivative review, complete final-identity matrices,
  Debug, complete application sanitizers, sustained/capacity and build metrics.
- Audit31/Helm application matrix-621 PASS: three ODEs, 24 actual ARCH executions,
  twelve comparisons, Auto-selected CPU KLU / GPU cuDSS for 32 equations, steps
  1/2/5 and the original scientific terminal time 1e-10. Each terminal lane takes
  twenty macro steps. Original field/controller/closure/time checks pass;
  source and artifacts match the four refreshed application/restart reports.
  Guarded elapsed 409.120 s, minimum available 6,489,304 KiB, peak owned RSS
  335,840 KiB, no swap growth or pressure stop. Archive:
  `validation/network/results/runtime-audit31-accounted-20260907/backend-validation-evidence.json`.
  The active contract/five packages/affected owners were reread at 22:48:54 UTC.
  Weak/Helm time acceptance still needs the earlier requested owner decision;
  no tolerance or EOS derivative change is made implicitly. Final Debug,
  complete application sanitizers, isolated cold-build metrics and whole-regrid
  capacity/timing remain open. No large generated build or commit/push was run.
- Current application refresh PASS: uniform-617 (14 cases / 114 executions),
  Cartesian AMR-618 (10 cases / 54 executions), and smooth/burn restart-619/620
  (12 executions / nine comparisons each, four backend directions and both
  checkpoint phases). All use ARCH `b83138a1e1c3e89d414e3c16b172c46ba08f6ac5502072c5a1ecb0fdfafb7fd9`
  and comparator `b24826cf008b441b9822fa821cf5efe1238e6e7733360abf112671d62a8dd203`,
  unchanged source/package identities and the original physical budgets.
  The two restarts overlap the small Cartesian suite; their 8.040 / 39.134 s
  guarded times are not isolated performance results. All three finish with
  at most 256 KiB observed swap growth and no pressure stops. Uniform-617
  takes 54.179 s with no swap growth. Public module/index links now point to
  these accounted-candidate records. Curved-601 retains its older identity.
  Actual audit31/Helm application matrix-621 is running; weak application time
  acceptance remains awaiting the owner's earlier requested decision.
- Direct focused sanitizer campaigns PASS: memcheck-614 and racecheck-615 each
  complete all 18 configured routes, mandatory CUDA initialization, unique
  instrumented-process reports, zero errors/leaks or race warnings, and unchanged
  source/package/tool/artifact identity. No kernels or matrix sizes were excluded.
  Archive: `validation/backend/results/accounted-sanitizer-20260907/`.
  Racecheck is slow (1,141.252 s guarded) but completes; peak owned RSS 775,580
  KiB, minimum available 6,151,016 KiB, swap 93,768 -> 94,536 KiB, whole-device
  peak 5,279 MiB, no sustained-pressure stop. These are focused instrumented
  checks, not production performance or whole-application/capacity qualification.
  Refresh the final-identity application records next. Invocation-616 contained
  a misspelled build path and failed identity checks before executing a case;
  corrected invocation-617 runs the original full uniform manifest.
- The README CUDA configure examples now explicitly select Ninja and explain
  the fresh-directory requirement when changing generators. The measured heavy
  pool / total concurrency policy is Ninja-specific; do not silently rely on
  the default generator to provide the same scheduling. No compiler flags changed.
- Anti-drift reread 22:27:31 UTC: active contract, all five packages and affected
  backend/burn/IO/resource/validation owners reviewed. Full regression-613 and
  all 18 direct memcheck routes-614 pass; racecheck-615 is still running through
  sparse ODE batching after its AMR, geometry, hydro and cuDSS provider checks
  passed. The independent built-in time/EOS reference refresh also passes
  (`validation/burn/results/independent-accounted-20260907.log`, 13.067 s,
  no swap growth; concurrent diagnostic load, not a throughput benchmark).
  Restart EN/ZH summaries now separate usage/results from the preserved earlier
  machine/format audit under their results directory; relative links pass.
  No math, tolerance, provider setting or generated package changed here.
- Regression-613 PASS: all 97 configured CPU/CUDA tests pass, none skipped;
  guarded elapsed 287.823 s, minimum available 6,418,116 KiB, peak summed owned
  RSS 495,296 KiB, swap 92,232 -> 93,512 KiB (1.25 MiB growth). Transient I/O
  full-stall peak 38.102% is not sustained and does not trigger a stop. The 22
  memory-guard unit/owned-process controls also pass. Focused sanitizer-614
  now checks 18 configured executable routes with mandatory CUDA initialization,
  direct per-process reports and unchanged source/artifact observations.
- Regression-608 completes 96/97 PASS in 281.785 s. Its remaining failure is
  the second old zero-H2D assertion, in backend reconstruction; the initial
  construction check already passes. One test helper now checks exact boundary
  metadata bytes, zero field downloads and one grid-metric kernel in both
  phases. No production source or physical budget changes. Rebuild-611 PASS
  (6.033 s); focused new/reconstructed backend test-612 PASS (0.35 s CTest).
  Full 97-test regression-613 is running. These are not zero-swap requirements:
  608 allows 3.75 MiB swap growth without sustained pressure; 611/612 have none.
- Direct application memcheck-610 PASS: actual ARCH process, mandatory CUDA
  initialization, complete spherical RKL2 coupled five-step execution, zero
  memory errors and zero leaked bytes. Comparison with the matrix-609 CPU
  checkpoint passes the original budgets (max absolute field error 1.24345e-14).
  Matrix-609 itself passes both selected curved cases / eight executions, but
  its child-following aggregate sanitizer report is not sufficient evidence of
  per-process GPU instrumentation. Use the direct check, not the wrapper's
  zero-error text, for this memory witness. Complete sanitizer coverage remains
  open. Both logs and exact direct input remain under build pending curation.
- Anti-drift review 21:58:38 UTC: active contract, all five packages and the
  affected backend/validation ownership reviewed. Test-only rebuild-607 PASS
  in 12.056 s, no swap growth; ARCH content is unchanged. All configured
  regressions rerun-608. In parallel with the CPU portion, memcheck-609 runs
  complete cylindrical RKL1 and spherical RKL2 coupled applications through
  the existing matrix runner, with no kernel exclusions and leak checks.
  The sanitizer follows ARCH children; non-CUDA Python/CPU lanes may omit CUDA
  initialization, while the runner still requires explicit device execution and
  trace kernels for every CUDA lane. Inspect actual sanitizer reports before
  claiming memory safety; the matrix PASS alone is insufficient.
- Full regression-606 completes 96/97 PASS (287.795 s guarded, no swap growth).
  All three new counter regressions pass, including nine curved exchange
  combinations and transactional no-commit controls. The sole failure is the
  old single-block assertion that construction has zero H2D bytes; actual
  boundary descriptors were always uploaded but previously uncounted. Replace
  it with exact `boundary operations * sizeof(DeviceBoundaryTransfer)` bytes,
  zero D2H and one grid-metric kernel. This retains the no-FluidState-upload
  requirement. Its direct CUDA ABI-header dependency is explicit; focused
  rebuild-607 precedes a fresh full run. No production source changed.
- Backend accounting rebuild-605 PASS: 23 edges in 33.752 s with LTO and
  existing numerical flags retained, heavy pool two / total four. Minimum
  available 5,984,264 KiB, peak owned RSS 1,650,016 KiB; swap 83,272 ->
  88,904 KiB (5.5 MiB growth), no sustained-pressure stop. No large network
  instantiation was repeated. ARCH is now
  `b83138a1e1c3e89d414e3c16b172c46ba08f6ac5502072c5a1ecb0fdfafb7fd9`;
  comparator content hash is unchanged. All 97 configured CPU/CUDA CTests are
  running-606 on this new candidate. Matrix-601 remains valid for its recorded
  pre-accounting candidate, not automatically reassigned to this one.
- Counter negative-604 FAILS all three new controls as intended: same-level,
  coarse-fine and staged construction omit recorded metadata/kernel work.
  Test build-602 initially lacked a direct CUDA header dependency in the Host
  multiblock target; explicit CUDA::cudart fixes it, rebuild-603 PASS (16.231 s).
  Backend repair uses one metadata enqueue/account function in the existing
  resource translation unit, shared by exchange, boundary/flux construction,
  indicators and reflux. Metric kernels and flux fences are counted at their
  existing sites. No math, state layout, launches or numerical budgets change.
  Rebuild and targeted counter/state/transaction regressions are next.
- Full curved matrix-601 PASS: all 24 cases, 48 comparisons and 96 actual
  CPU/CUDA executions, unchanged field/conservation/RKL/topology budgets and
  verified before/after source/artifact identity. Largest absolute field
  difference is 2.26485e-14; tiny near-zero fields use the declared absolute
  allowance, not a new relative-error rule. Archive:
  `validation/amr/results/release-curved-weighted-20260907/backend-validation-evidence.json`.
  Guarded elapsed 1446.492 s, minimum available 4,948,892 KiB, peak owned RSS
  1,907,464 KiB, swap 75,336 -> 83,272 KiB. Whole-device peak 3,210 MiB,
  minimum free 4,815 MiB. Full-stall peaks memory 2.218%, I/O 4.736%; no stop.
  This closes the coupled volume-weight repair's application check, not final
  release identity or isolated whole-build/capacity qualification.
- Anti-drift checkpoint 21:36:18 UTC: active contract, all five packages and
  AMR/validation/resource owners reread. Matrix-601 is still progressing through
  coupled 3-D cases without failure; no source/flag/budget change is made during
  it. Next remains backend metadata-counter repair, then final artifact/runtime
  and isolated build/capacity qualification. Large networks stay deferred.
- Resource instrumentation audit during matrix-601 identifies uncounted Host
  metadata uploads in same-level/coarse-fine exchange and boundary/AMR-flux
  construction. Field migration remains device-side; this is an incomplete
  communication counter, not a newly established numerical error. Correct the
  existing backend counters at their actual enqueue sites and add exact-byte
  regression controls before whole-regrid transfer/capacity qualification.
  Preserve the in-flight numerical matrix's source identity until it finishes.
- AMR English/Chinese module pages now separate concise coverage/results/usage
  from raw history. Their previous complete texts, measurements and commands
  are preserved under `validation/amr/results/pre-release-notes-20260907/` with
  archive labels and relocated links. No numerical source/input changed.
- Original coupled cylindrical RKL1 application-600 PASS, 7.032 s guarded,
  with unchanged inputs and budgets at steps 2 and 5. No swap growth or sustained
  pressure stop. Full 24-case / 96-execution curved matrix-601 now runs on the
  repaired Release artifact, with whole-device memory telemetry and the existing
  1.5 GiB available-memory reserve / 256 MiB incremental-swap ceiling. No heavy
  compilation overlaps this matrix. Active contract, all five packages and
  affected ownership entries reread after context resumption.
- Volume-weight repair builds and focused tests PASS: core/exchange/composition
  build-597 completes 15 edges in 25.329 s, peak owned RSS 1,510,528 KiB and no
  swap growth, preserving optimized flags/LTO. Dependent relink-599 completes
  eleven edges in 14.363 s. Exchange/composition CTest-598 PASS, 97.377 s,
  covering all nine geometry/dimension combinations, three state slots,
  composition and invalid-plan no-commit. The old backend FAIL-596 is retained.
  Current ARCH SHA-256 is
  `89bdddd93730d9441c70bc69d802ac535270ae58de0e276c82ac111770174ab9`;
  relinked comparator hash remains unchanged. Original coupled case-600 is
  rerun before the full matrix; no numerical or input budget was altered.
- Negative exchange control-596 behaves as required: old backend passes all
  Cartesian dimensions, then FAILS cylindrical 1-D restriction. The new
  descriptor/lowering supplies validated shared-geometry volume weights;
  device gather consumes existing restriction leaves rather than unit weights.
  Core/exchange/composition rebuild-597 starts with heavy pool two / total four,
  retaining runtime flags and guarded memory/swap/pressure limits. Next: all
  nine geometry/dimension exchange controls and the original failing coupled
  application, then resume the full matrix on the repaired artifact identity.
- Isolation-594 PASS as a diagnostic, not acceptance: the original difference
  is present at step 1 and exceeds the field budget by step 3. Uniform curved
  and AMR Cartesian controls agree around 1e-15; removing viscosity does not
  cure it, removing thermal diffusion reduces it. Errors localize to coarse/fine
  interfaces. Source inspection identifies a concrete missing backend input:
  Host `GhostExchange` restricts with `GridMetrics::CellVolume`, but CUDA
  `CoarseFineExchangeKernels` supplies unit weights unconditionally. Extend
  the existing exchange test across Cartesian/cylindrical/spherical 1D/2D/3D,
  prove failure with the old backend, then carry shared-geometry measures in
  the backend transfer descriptor. Do not duplicate metric/restriction math.
  Active contract/five packages/affected ownership reread at this phase change.
- Curved full matrix-590 FAILS at cylindrical coupled RKL1 1-D step 5, after
  all twelve species-only cases and the coupled step-2 comparison pass (52 ARCH
  executions completed). First reported radial momentum difference is
  1.23957e-9, normalized to field peak 2.71544e-8, exceeding the unchanged
  1e-8 / 5e-12 field budget. Preserve the failed checkpoint/logs and isolate
  the first divergent stage; do not increase the comparison threshold.
  Resources were bounded: 723.029 s, minimum available 4,909,564 KiB, peak owned
  RSS 1,902,128 KiB, swap 72,776 -> 75,336 KiB; whole-device peak 3,210 MiB.
  No guard stop or sustained stall. Active correctness work takes priority
  over final resource/cold-build qualification until this supported case works.
- Actual current-tree combination/header-layer audit-593 PASS, 2.006 s guarded,
  in addition to tooling unit controls. No production source edit is made
  during curved matrix-590; documentation and stored results are outside the
  numerical source fingerprint by the established evidence contract.
- Current tooling regression PASS: root Python suite-591 has 157 tests and
  tooling suite-592 has 105 tests, including provenance, input contracts,
  include-layer checks, configured code images and compile-evidence parsers.
  These are lightweight overlapping checks, not isolated resource benchmarks.
  Curved matrix-590 is advancing through the first 3-D cylindrical cases;
  only the complete matrix/identity check may establish a matrix PASS.
- Current optimized audit31 sparse matrix-589 PASS, 159.427 s guarded,
  peak owned RSS 270,088 KiB and unchanged swap. All three production ODEs,
  four external steps at both two/three-cell storage sizes, real CPU KLU / CUDA
  cuDSS, active evolution and unchanged field/controller budgets pass.
  `validation/network/results/audit31-release-20260907/evidence.json` verifies
  current identities before/after. Together with weak-588 this closes the
  explicit-input focused trajectory allocation after factory-test repair, not
  the complete Helm application gate. Full curved 24-case / 96-run matrix-590
  starts alone, with host/system-pressure and whole-device memory observation.
- Current optimized weak science/factory matrix-588 PASS, 77.250 s guarded,
  peak owned RSS 442,068 KiB and no swap growth. Independent original Suzuki
  table interpolation, DOP853/Radau trajectories, all three production ODEs,
  DenseLU/cuDSS handoff and persistent table owners retain their original
  budgets. BE_NR tighter-local-tolerance control improves error by 100.035x.
  `validation/network/results/weak-urca-release-20260907/evidence.json` records
  current artifacts and verifies identity after execution. This does not close
  the separate Helm application time condition awaiting owner direction.
  Active contract/five packages/affected owners reread; next sparse-589 and
  complete curves, with no overlapping heavy compiler/3-D workload.
- Complete Release CUDA CTest recheck-587 PASS all 66 tests, 75.269 s guarded,
  peak owned RSS 490,564 KiB, unchanged swap. Together with CPU-584 (31 tests),
  all 97 configured CTests have passed. This is current-candidate regression,
  not a final public-release certificate: custom weak/sparse explicit-input
  scientific matrices, complete curves, Debug, sanitizer and isolated capacity
  remain required. Weak science-588 is next on the current optimized targets.
- Resumed full build-582 PASS: remaining 22 edges, 323.317 s, minimum available
  4,546,644 KiB, peak owned RSS 2,377,064 KiB, swap baseline/peak 72,776 KiB.
  CPU CTest-584 PASS all 31 tests (150.481 s guarded), including independent
  burn time controls, checkpoint compatibility, tabular EOS and 161-equation
  KLU. CUDA CTest-585 passes 64/66: the reduction test incorrectly requires
  sm_90 and a retired diffusion snapshot; the generic custom factory feeds
  every registered network the same arbitrary short-step physical state,
  causing its weak-network CPU oracle to fail before CUDA comparison.
  The reduction now checks actual device candidates against a serial minimum,
  retains all 36 synthetic edge contracts and requires real successful launches
  without a model-name restriction. Custom registry checks construct all typed
  owners with bounds/rejection controls; actual custom trajectories use the
  existing explicit-input sparse/weak science runners, not universal guessed
  states. This changes test coverage allocation, not production math or budgets;
  rerun BOTH full focused trajectory matrices before closing the factory gate.
  Repaired test build-586 PASS; complete CUDA recheck-587 follows. Public
  reference manuals now accurately describe retained Release dispatch LTO.
- Guard regression PASS: 22 tests, including stale/live/zombie PID controls,
  bounded swap, sustained pressure and escaped-child cleanup. Formal CTest-583
  also passes the guard and both corrected CPU contracts (three tests, 2.018 s).
  Full test compilation resumes as build-582, retaining successful objects,
  total jobs two and all original optimization flags. Current candidate
  uniform/Cartesian/restart status is synchronized in both validation indexes
  and restart summaries; final-identity and remaining gates stay open.
- Full test build-578 FAILS at 103/121 edges because the memory guard raises
  `pidfd_open EINVAL`, then cleans its own command tree. Ninja's resulting
  "interrupted by user" is not a user cancellation or an OOM. Minimum available
  RAM 2,993,104 KiB, swap 69,448 -> 73,288 KiB, peak owned RSS 3,975,072 KiB;
  no pressure threshold was reached. The existing guard now ignores EINVAL
  only after verifying that the sampled process identity has disappeared or
  changed; live identity/other errors still fail closed, and monitor errors
  are named in retained logs. Regression and resumed build are next.
  CPU contracts-581 pass 26/28: the old two-registration source assertion
  predates staged device migration, and two diffusion reduction bit snapshots
  predate the corrected face-capacity/geometric operator. Test-only repairs
  check actual device publication order and serial reduction of shared cell
  candidates; independent physical geometry/capacity oracles remain separate.
  No production formula or numerical tolerance changes in these repairs.
- Both current-candidate canonical restart matrices PASS: smooth advection-579
  (8.060 s) and burning/ENUC AMR-580 (32.221 s), each twelve ARCH executions
  and nine comparisons covering all four backend directions at both actual
  intermediate post-regrid and terminal source phases. Existing time, field,
  ENUC/controller and output-counter budgets are unchanged. Results:
  `validation/amr/results/restart-{smooth,burn}-shared-helm-20260907/`.
  Test build-578 is at 76/121 edges with bounded jobs two and no swap-growth
  stop. The active contract/five packages/affected owners were reread before
  proceeding; successful runtime subsets do not close curved/generated or
  final physics/sanitizer/resource gates.
- Actual checkpoint replay-576/577 localizes time FAIL-564: on the CUDA
  checkpoint state, the same-input CPU/CUDA base pressure, density-perturbed
  pressure, recovered temperature and cv agree; temperature-perturbed pressure
  differs by one ULP. The existing finite-difference sound-speed formula
  amplifies it to about 7.5e-13 relative CFL difference. The CPU checkpoint
  state's same-input replay agrees. This is not another backend physics body.
  The owner has been asked whether temporal comparison may consume the existing
  case-relative precision instead of a roundoff-only 10-ULP assumption, while
  retaining all field/energy/conservation limits and fixed-time scientific
  comparisons. No temporal or physics budget has been changed pending that
  direction. Continue unaffected regressions: full test build-578 (bounded
  total jobs two) and canonical restart-579 are active.
- Current-candidate uniform matrix-572 PASS: 14 canonical cases / 114 ARCH
  executions, including Sod reference and long periodic evolution, 53.169 s.
  Cartesian AMR matrix-569 PASS: all ten cases / 54 executions, 41.124 s.
  Both retain original comparison/conservation/topology budgets and observed
  source/binary identities. Gaussian retained runner-566 PASS (9.033 s), six
  coordinate initial states plus same-time/topology thermal on/off on both
  backends; about 1.058% resolved energy change, not a flag-only witness.
  No release-completion claim: weak application time acceptance is still open.
  Same-input cell thermo trace-570 alone does not reproduce its CFL difference;
  real step-4 capture-571 shows a one-ULP internal-energy difference. CPU-only
  sensitivity-574 gives equal sound speeds for those two states. Inspect both
  states on the actual device before attributing this failure to a tolerance.
  Diagnostic build-567 failed a range initializer's type deduction; corrected
  diagnostic-568 compiles. None of these changes production math or budgets.
- Original weak application rerun-564 now passes steps 1 and 2, but FAILS
  step 5 at the checkpoint physical-time condition. All conserved fields at
  that checkpoint are bitwise identical; ENUC relative difference is 7.48e-13.
  CPU time 0.0028130494133493554 / CUDA 0.002813049413348374 differ by about
  9.81e-16 s when the hydro CFL first limits the timestep. The comparator's
  existing rule permits only 2*min(steps,10) ULPs. Trace the common Helm sound
  speed / differenced pressure derivative before deciding whether this is a
  physics issue or an invalid roundoff-only temporal acceptance assumption;
  no budget change or final pass is authorized by this failure alone.
  Native CMake capability probe-565 PASS. Comment-only reconfigure-563 PASS,
  no compilation work, 7.030 s. Current ARCH SHA-256 is
  `9bb382dff08aeaae50bc3fe412f100418de8946f36df0eb5be0bf5a6a0e483b4`;
  checkpoint comparator remains `b24826cf008b441b9822fa821cf5efe1238e6e7733360abf112671d62a8dd203`.
- Core build-555 PASS, all 92 requested edges including the real ARCH link:
  guarded 2,069.497 s, peak owned RSS 3,775,900 KiB, minimum available
  3,198,592 KiB, swap baseline/peak 70,472 KiB. Pressure telemetry is complete;
  memory full-stall peak zero, I/O peak 9.891%, no guard stop. This is a broad
  incremental rebuild with overlapping small diagnostics, not a clean isolated
  cold-build benchmark. Formal CMake EOS/controller tests-562 both PASS (3.15 s).
  Before runtime identity capture, misleading inherited CPU-dispatch no-LTO
  comments are corrected only in prose; compiler options/properties are unchanged.
  Next: original weak application, full generated matrix, retained Gaussian
  initialization/activity and remaining canonical runtime/restart matrices.
- Extended weak cell diagnostic build-560 / run-561 PASS: all three production
  ODE policies complete five steps / ten Strang half-steps using the same
  registered weak network, Helm view and DriverBurn handoff. Controller
  differences meet the unchanged 2e-8 budget; the original first-step failure
  is not reproduced. Run takes 3.020 s, peak owned RSS 132,216 KiB, stable swap.
  This remains a diagnostic (no hydro/checkpoint path), not a replacement for
  application matrix-532's rerun. Build-555 continues without changed flags.
  Memory guard regression now reports 21 tests PASS, including bounded swap,
  brief-stall recovery, sustained pressure and owned-descendant cleanup.
  Controlled compile logs-510/511 and the original replay helper are retained
  under `validation/backend/results/local-build-reference-20260907/`. The
  README distinguishes the measured heavy-pool choice from the still-open
  whole-build/total-job optimum, and does not invent a historical fingerprint.
- Shared Helm CPU regression-558 PASS: both registered tests actually ran;
  burn_mainline_reference takes 143.31 s, resolved_execution_plan also passes.
  Independent burn time-reference runner-559 PASS (13.076 s), all four built-in
  networks retain their independent EOS/time-integration acceptance and unchanged
  fixtures. Core build-555 has reached 51/92 edges, with about 4 GiB available
  and stable modest swap. These overlapping checks are scientific evidence,
  not an isolated compile/runtime performance benchmark. Original full-program
  weak case and final runtime matrices still await the linked candidate.
- Original weak first-step input now passes shared burn-cell diagnostic-557:
  actual RuntimeParams/registered generated weak network/Helm view/BE_NR/
  DriverBurn handoff, two Strang half-steps, both energy and ENUC/limiter
  bitwise equal across Host/CUDA (controller relative difference zero).
  No energy definition or comparison threshold change was needed. Build-556
  takes 11.066 s and 353,920 KiB; run-557 takes 1.014 s. This isolates the
  repaired cause of application FAIL-532 but does not cover hydro/reduction/
  checkpoint integration. Core build-555 continues before those final reruns.
  Root Python 156 tests and tooling 105 tests PASS. Retained Gaussian reader
  accepts all six earlier 537 initial states and the same-time on/off control;
  identical states are rejected. Fresh archived application evidence is pending.
- Guarded core build-555 is active: unchanged Release optimizations, heavy pool
  two / total jobs four, core plus current EOS/controller/CPU-science/probe
  targets. CMake configure-553 PASS and confirms image metadata appears only in
  RuntimeProbe.cpp. Extra Helm grid descriptor/move tests compile-552 and
  memcheck-554 PASS (zero errors, 2.012 s). Tooling 105 tests PASS, including
  the repository ownership/header audit. Native image probe-550 PASS.
  The previous build-local Gaussian diagnostic is now retained as one functional
  module runner, reusing the existing lane and provenance writers. Its reader
  checks the old 537 evidence at identical physical time/topology and rejects
  identical thermal on/off states; a fresh final-identity run remains pending.
  Actual compile-command inspection also corrects the prior CPU-dispatch no-IPO
  claim: per-configuration IPO currently enables -flto=auto. No flags were
  removed to speed this build; assess future changes by actual compile/link
  commands and runtime metrics, not the unsuffixed property alone.
- Anti-drift reread 19:11:13 UTC: active contract, all five packages and EOS/
  dispatch owners reviewed. Image-list normalization has four passing CMake
  tests (numeric/suffix/special/malformed/manual-flag cases). Native probe-550
  PASS: device 8.6, runtime 12030, driver 13030, primary context stays inactive
  before/after; pure metadata tests exercise SASS major/minor compatibility,
  PTX version gating and strict CUDA / startup Auto behavior. The repeated
  model-derived 8.6 policy floor is removed; descriptors retain feature fields.
  Updated Helm owner test build-548 failed on a test-only annotated lambda
  requiring an optional compiler flag; use a normal Host oracle function instead.
  Memcheck attempt-551 had no executable and is NOT a sanitizer pass. Full
  configure/build and real weak application verification remain pending.
- Helm focused correction PASS-547 (2.011 s): existing actual-table independent
  references and Host/Device budgets pass unchanged. Added 36 analytic monomial
  cases x nine points x two backends have max scaled error 2.273e-13; a 2^60
  constant background has exactly zero pressure/cv derivatives. Conditioned
  polynomial alone passes round-trip diagnostic-540 but fails derivative
  parity-542; trace-545 identifies a one-ULP libm/libdevice density-node
  difference. The canonical Host owner now materializes the grid once and the
  existing CUDA owner uploads it. Free energy/eta share one locator. This
  removes repeated pow queries, not runtime optimization flags. Extra node
  lifetime/descriptor checks and real weak application reruns are pending.
  Diagnostic build-543 had an archive-path typo; corrected build-544 passes.
- Anti-drift reread 18:49:39 UTC: active contract, all five packages and affected
  EOS/burn/problem owners reviewed. No heavy processes remain. Ordinary-gas
  correction builds PASS-535 (24.248 s); initialization/activity PASS-537 on six
  coordinate cases. Thermal on/off changes energy by 0.03159413725316 on BOTH
  backends, confirming actual nonzero transport. Diagnostic-538 isolates Helm
  inversion residual noise: at identical weak-state target energy, first CPU/
  CUDA residuals are 58,880 / 37,632 erg/g; three extra Newton corrections do
  not remove it. Evaluate the SAME Hermite polynomial with constant-background
  cancellation enforced before contraction; retain independent references and
  all original budgets. No burn-energy definition or table data change.
- Further coverage correction: the original Gaussian's unconditional default
  aprox19 setup supplies Cv_ref=0 to IdealGas. Thermal flux was therefore zero
  even in the pressure-perturbed 530/531 runs. Those passes cover the recorded
  states/viscous/species paths, NOT thermal transport. Shared problem setup now
  accepts the registered None network only when burning is disabled, leaving
  gas definitions to the problem. Gaussian defaults to that mode and its
  original passive gases, with positive configurable `gas_cv` and input gamma;
  explicit nuclear networks still use the same factory. The canonical passive
  diffusion input explicitly selects None and gas_cv=717.5. Rerun both physics
  activity and conserved-field matrices; require nonzero thermal transport,
  not just activation flags. Independent initialization diagnostic-534 passed
  six native-coordinate expressions at <4.84e-14 absolute, but pre-fix Cv was
  zero; preserve it as initialization-only evidence. First diagnostic-533 was
  an oracle error (did not account for the global-gamma fallback), not a
  production thermodynamic failure. No budgets or frozen reference numbers
  have been widened.
- Generated application matrix FAIL-532 (522.076 s): audit31 BE_NR/BD/ROS4
  advance through their accepted-step and scientific-time checks, then compact
  weak BE_NR fails the first checkpoint's controller comparison. CPU/CUDA
  species are bitwise identical; total energy differs by 4.9250e-14 relative,
  but ENUC from subtracting large EOS energies differs by 8.5169e-7 relative.
  dt_burn is 1.7012401634844977e33 versus 1.701238737399687e33. Trace initial
  EOS inversion, accepted temperature and `DriverBurnPolicy` energy handoff;
  do not widen the fixed comparison budget or replace thermal definitions
  without an independent first-law/roundoff control. Preserve this failed
  archive at `validation/network/results/release-runtime-20260907/`.
  Modest swap was allowed as requested: baseline 57,368 KiB, peak 72,008 KiB;
  minimum available 3,502,604 KiB, no sustained-pressure stop. Matrix-529 is
  deliberately stopped through its OWN supervisor, not marked passed, to
  prioritize this burn defect before further expensive pre-fix qualification.
  No validation output is removed. Complete curved matrix, compatibility
  image correction and final build/regression gates remain OPEN.
- Binary-image audit design (implementation queued after active matrices):
  `PolicyDescriptor.h` repeats `{..., 8, 6}` in the registration macro and both
  Tabular specializations; there is no corresponding sm_86-only primitive in
  the reviewed CUDA source. Remove that model-derived floor while retaining
  the feature-requirement fields for genuine future requirements. Keep image
  metadata private to `RuntimeProbe.cpp`, not the shared math headers. Resolve
  CMake numeric/native/all/all-major targets into one explicit image list used
  by every ARCH CUDA target and the probe; report incompatible images before
  construction and preserve strict explicit-CUDA versus startup Auto behavior.
  Ordinary SASS compatibility is within a major CC with a compatible minor;
  PTX compatibility is distinct and needs a supporting driver. Do not accept
  a higher major merely because its CC number is larger than a SASS-only build.
  Unresolved/custom architecture flags need an actionable configuration error,
  not a guessed image list; architecture/family-specific targets need their
  actual rules, not the ordinary numeric predicate. Native probe must still
  leave primary-context activation unchanged. Retain negative pure-metadata,
  injected capability and actual native-probe tests. Sources:
  [NVCC GPU compilation](https://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html#gpu-compilation),
  [CMake CUDA architectures](https://cmake.org/cmake/help/latest/prop_tgt/CUDA_ARCHITECTURES.html).
  Batch this policy-header change before the next guarded core rebuild because
  it invalidates costly burn instantiations; do not repeatedly rebuild for docs.
- Anti-drift reread 18:23:37 UTC: active contract, all five packages and
  affected owners reviewed. Matrix-529 and generated matrix-532 are active;
  no new production/build/validation-source edits until their identity checks
  finish. Documentation is excluded from the declared code/input fingerprint.
  New HDF5 outputs under `validation/**/results/` are now ignored by Git, while
  metrics/inputs/logs/hashes remain visible. No output was removed; tracked
  historical files and EOS inputs are unchanged. Full data-archive publication
  is a separate release-packaging action, not automatic LFS upload.
- Radial coupled subset PASS-530: four 1D cylindrical/spherical RKL1/RKL2
  cases, 16 CPU/CUDA executions, 13.049 s. Two-dimensional coupled subset
  PASS-531: four cases / 16 executions, 52.179 s. Both retain the original
  conservation/parity budgets with nonzero pressure/velocity perturbations;
  neither substitutes for the complete 24-case report. Artifacts are under
  `validation/amr/results/release-curved-coupled-{radial,wedge}-20260907/`.
  Root tooling 155 and architecture/compiler tooling 101 tests PASS. Full
  matrix-529 remains active; it has reached actual 3D CPU/CUDA integration
  with mixed leaves. Small subset runs overlapped it, so its whole-device GPU
  peak and wall time must NOT be used as an isolated performance benchmark.
  Generated application matrix-532 now runs alongside it on the SAME frozen
  source/build inputs; no heavy build runs concurrently. Continue protecting
  RAM/swap and retain separate scientific versus resource acceptance.
- Coupled Gaussian build-527 PASS (11.185 s, no swap growth). Matrix-528
  stops at the original cylindrical 2D species-only topology gate: all eight
  roots refine, so the declared mixed hierarchy is absent. This is not a
  numerical-tolerance failure or a qualified AMR interface workload. Keep the
  failed archive. Multidimensional inputs now extend the outer radius 2 -> 3
  with 4 -> 8 radial roots, preserving the cell spacing, pulse, thresholds,
  timestep-stage and conservation/parity budgets. Quiescent outer roots make
  the intended coarse/fine interfaces observable. 1D inputs stay unchanged.
- Focused closure PASS-523: all three named geometry/RKL tests, 2.64 s CTest.
  Current geometry/capacity archive PASS-524:
  `validation/amr/results/diffusion-capacity-release-20260907/evidence.json`;
  includes nine variable-density contraction matrices per backend in addition
  to origin and spatial convergence. Memcheck-525 reports zero errors. The
  complete Cartesian AMR application matrix PASS-526: 10 cases / 54 CPU/CUDA
  executions, guarded 41.736 s, peak owned RSS 644,920 KiB, no swap growth;
  whole-device VRAM baseline 1777 MiB / peak 2193 MiB (not owned allocation).
  Evidence: `validation/amr/results/release-cartesian-20260907/`.
  This qualifies that candidate/matrix, not final source identity or release.
- Next coupled-geometry gap: current Gaussian initializes uniform pressure and
  zero velocity, so the existing curved matrix only exercises species diffusion.
  Add ordinary configurable pressure/velocity pulse amplitudes to the SAME
  registered problem, and use Grid's physical Cartesian coordinates for the
  3D Gaussian distance (the existing spherical expression omits theta). Preserve
  separate species-only cases and add coupled RKL cases with unchanged physical
  conservation/parity budgets. Do not claim switch activation alone exercises
  thermal/viscous operators. Resolve compiled-image versus repeated sm_86 policy
  floor before final-identity freeze, then run all four matrices and restarts.
- Anti-drift reread 17:53:33 UTC: five packages/ownership reviewed; core-519
  PASS (82.590 s, 17 edges, peak owned RSS 2,051,972 KiB, no swap growth) and
  actual AMR/diffusion/restart smoke-521 PASS (4.023 s). Regression-520 passes
  both geometry suites, independent face-capacity references and original
  numerical-state fixtures; only an invalid-EOS-payload versus valid-payload
  dt-bit comparison fails. That comparison is not a physical acceptance rule:
  invalid status already rejects its timestep. Recovery now checks the valid
  uniform-capacity analytic limit and bitwise repeatability of TWO subsequent
  valid calls, retaining intermediate-error/status-reset checks. Build-522
  PASS (37.138 s); named CTest-523 pending. No production numerical changes
  were needed for this test-contract correction.
- Capacity regression-517: both CPU/CUDA geometry and density-contraction suites
  PASS; RKL suite FAILS only old cell-only dt/coefficient-cutoff assertions.
  Actual flux/operator/RKL-state envelopes remain within their existing budgets.
  Test reconciliation preserves those numerical fixtures and the original
  constant-capacity dt bits, adds an independent long-double face-capacity
  reference, and removes the unsafe positive-transport cutoff from dt (the
  operator already applies that transport). No tolerance widening or endpoint
  regeneration is used. Build-518 PASS; final small source-only/zero-viscosity
  edge handling is included in core/test build-519, now running. Recheck actual
  named CTests, memcheck and application/restart smoke before proceeding.
- Variable-density diffusion failure-513 is independently confirmed on actual
  operator matrices: alternating rho=1/100 gives default-CFL amplification
  about 13 in both radial systems, even after the origin-only repair. Fluxes
  use face rho/transport, while the old dt uses only cell diffusivity. The
  shared face thermodynamics/coefficient evaluation is now factored once in
  `DiffFlux`; dt consumes those same faces with rho and rho*cv capacities.
  Probe-515 passes the same spectral witness. CPU/CUDA adapters gain only two
  scratch arrays within the existing five-array diffusion workspace. The
  driver completes backend-local Current ghosts before the new neighbour
  reads; CUDA does not materialize the Host fields. Retained variable-density
  contraction controls reuse the existing test matrix assembly. Build-516 is
  pending; audit the affected historical dt fixture semantics explicitly,
  not by regenerating numbers from the repaired implementation.
- Controlled local build choice: jobs-2 control-511 PASS, guarded 159.564 s,
  peak owned RSS 3,882,728 KiB, minimum available 3,095,800 KiB, swap unchanged
  at 57,880 KiB. PSI full-stall peaks memory 1.829%, I/O .042%, no sustained
  pressure. Against jobs-1's 287.928 s guarded duration, about 44.6% less elapsed
  time / 1.80x throughput on the same two uncached heavy commands. Choose heavy
  pool 2 / total jobs 4 as the current local mid-range reference, also supported
  by complete link-490. Do not extrapolate this two-TU result to a full cold
  build or server scaling; cold final-identity and runtime/capacity gates remain.
  Reproduce locally via `build/compare-heavy-compile.py`, which records exact
  compile-database argv and uses fresh scratch outputs. GNU time command wall
  time and Python/guard monotonic intervals are separately recorded; compare
  like timing sources. Production inputs remained unchanged during both runs.
- Anti-drift reread 17:29:36 UTC: active contract, all five packages and affected
  ownership entries reviewed. Actual RKL regression-509 PASS (one registered
  CTest, 0.70 s; empty-test rejection enabled). Controlled jobs-1 compile-510
  PASS: identical two largest dense/sparse Tabular4D/aprox21 commands, no ccache,
  fresh scratch outputs, group monotonic 287.420 s / guarded 287.928 s, peak
  owned RSS 1,951,348 KiB, minimum available 4,659,456 KiB, no swap growth.
  Jobs-2 control-511 is running without other heavy work or production-input
  edits. Runtime/capacity and final full identities are still open.
- Evidence correction: RKL invocations-496 and -507 selected an unregistered
  CTest name and ran zero tests. Their exit code 0 is NOT a test pass; previous
  ledger wording is withdrawn. Use the registered `diffusion_rkl_parity` name
  and `--no-tests=error` for subsequent filtered CTest runs; corrected run-509
  is pending. Actual core increment-505 PASS (71.450 s, 12 edges, peak owned
  RSS 1,875,268 KiB, no swap growth), and runtime smoke-508 PASS (4.018 s).
  The smoke exercises Cartesian dynamic AMR and CPU/CUDA curved diffusion/
  restart, not the full release matrix.
- Shared origin/source and curved frozen-coefficient timestep repair PASS-502
  (CPU/CUDA geometry CTests, 0.95 s). Archive-504 passes with unchanged artifact
  identity: `validation/amr/results/curved-origin-release-20260907/evidence.json`.
  Per backend: 90 viscous refinement samples (max finest normalized 7.26751e-5),
  18 origin cases (max null error 2.93099e-14), six actual radial FE matrices
  (minimum entry -2.22e-16, maximum row sum .999207). Paired thermal/species
  cases total 54 samples / 108 backend evaluations; finest absolute error
  1.14163e-7 within predeclared 1e-6 budget. Memcheck-506: zero errors.
  Full application increment-505 is rebuilding only 12 affected edges, not the
  whole burn matrix. Do not mark coupled/nonlinear diffusion qualified yet.
- Next local compile measurement: replay the same two largest Tabular4D/aprox21
  dense/sparse compiler commands at jobs 1 and 2 into fresh scratch outputs,
  without ccache or changed optimization. No concurrent heavy workload during
  that comparison. Three worst-case commands would exceed the currently
  available WSL RAM minus the declared reserve; retain higher configurable
  concurrency for larger machines instead of testing overload locally.
- Core/application/comparator/RKL build-490 PASS: 2085.354 s, heavy pool 2 /
  total jobs 4, peak owned RSS 3,642,832 KiB, minimum available 2,936,060 KiB,
  swap baseline/peak 57,880 KiB. Full-stall peaks: memory 12.151%, I/O 2.572%;
  no sustained-pressure stop. GCC 12 final LTO link succeeds. This mixed
  cold/incremental run is a viable concurrency candidate, not a comparable
  cold-build speedup or the final sweet spot. Compiler inputs may now change.
- NEW shared radial-origin failures: independent v=r e_r has zero vector
  Laplacian, but spherical first-cell momentum is -1.2/-2.4/-4.8 as spacing
  halves in probe-495. Pointwise 1/r in the viscous divergence source does not
  use the spherical finite-volume measure. Reuse the existing volume-averaged
  inverse-radius authority, not a backend-only formula. Actual operator columns
  in spectral probe-498 also give default-CFL forward-Euler amplification
  1.54609 (cylindrical) / 3.46425 (spherical): the Cartesian dt estimate misses
  geometric damping. Both are mathematical failures despite CPU/CUDA parity.
  Retain those logs and add origin/stability controls to the common fixture.
  Away-origin archive-492 remains scoped evidence, not origin qualification.
- Guard tests now total 21; root Python tests 153 and tooling/header tests 101
  pass. Focused optimized RKL CTest-496 passes. Thermal spatial additions await
  their geometry-target rebuild and are not included in archive-492.
- 16:48 UTC anti-drift reread: core build-490 is at 55/115 remaining-build
  edges, under two heavy slots / four total jobs. No compiler errors or guard
  stops observed; swap remains about 56 MiB. New production inputs are frozen
  during this build. The separately built geometry test now has an additional
  thermal manufactured-field mode awaiting its next rebuild; this is not counted
  in archive-492. The same scalar oracle serves thermal and species paths.
- Full-runtime evidence now requires the generated-network application matrix
  as well as Cartesian AMR, curved AMR, uniform and both restart suites. The
  new `--generated-matrix` uses `validation/network/runtime_cases.json` and the
  existing plan/provider/provenance checks. All 37 provenance/gate tests pass.
  This closes a coverage omission in the aggregator, not any unexecuted case.
  Memory-guard controls now total 21 passing tests, including explicit bounded
  swap success and swap-ceiling stop through the complete child-cleanup path.
- Shared curved vector diffusion is corrected in `DiffFlux`: one basis rotation
  supplies covariant face gradients and their matching momentum source. CPU
  and CUDA adapters only load neighbouring state. The conservative work flux
  retains variable dynamic viscosity; thermal/species formulas are unchanged.
  Independent Release CPU/CUDA geometry tests PASS-491 (0.94 s CTest); each
  backend checks 30 analytic profiles at three spacings, with finest normalized
  error <=7.26751e-5 against the predeclared 1e-4 budget and >=3.5 refinement
  factor above roundoff. Full focused archive PASS-492 (6.035 s) is
  `validation/amr/results/curved-viscosity-release-20260907/evidence.json`.
  Memcheck-493 PASS (0 errors, 3.022 s). These close focused spatial controls,
  not full time-evolved curved viscous/thermal AMR or final application gates.
- GCC 12 C/C++/CUDA-host alignment configures successfully-483; early mismatched
  GCC/LTO diagnostic is witnessed by configure-482. The existing memory guard
  now optionally observes PSI full stalls and swap-in/out rates. It allows
  bounded swap and stops sustained memory/I/O stalls through the same owned-
  descendant cleanup. All 19 guard controls pass, including productive swap,
  transient recovery, sustained stalls and lost counters. Root tests: 149 pass;
  tooling/header/ownership tests: 101 pass. Two further geometry transcript
  controls pass (complete 90-sample coverage, missing/duplicate/nonconvergent/
  wrong-backend/nonfinite rejection); references remain independent.
- Single-heavy focused build-488 was deliberately stopped via its verified
  guard PID after the CPU/CUDA geometry executables linked, to measure the next
  safe concurrency candidate. No unrelated process or completed object was
  removed. This interrupted build is not a pass. Two-heavy / total-jobs-4
  configuration PASS-489; core/application/RKL build-490 is running with
  1536 MiB available-RAM headroom, 256 MiB swap-growth allowance and sustained
  pressure protection. Keep its compiler inputs stable. A two-slot run in
  progress is not yet a measured sweet spot or a cold-build speedup claim.
- README EN/ZH now explains shared capabilities and build controls in ordinary
  language, with a single current-status entry and links to quantitative
  Validation. Hardware is a mid-range measurement reference, not a device-name
  branch or changed physical budget. Final public-document synchronization
  follows final validation, without promoting unfinished gates to supported
  release claims. The five work packages and affected ownership were reread.
- Cold core build-477 compiled all source objects but FAILED at final linking:
  fetched KLU C objects used GCC 13 LTO, while the C++ linker used GCC 12.
  Guarded duration 3983.856 s, peak owned RSS 1,974,468 KiB, minimum available
  4,474,976 KiB, swap stayed at 58,904 KiB. This is not a completed cold-build
  result. Keep optimization enabled, align all compiler languages and add an
  early configure diagnostic before retrying. Shared viscous repair remains
  next; do not qualify the pre-repair application as a release artifact.
- Independent curved-viscosity defect confirmed, diagnostic build/run-480/481:
  a uniform Cartesian velocity (1,0,0), represented as (cos(phi),-sin(phi))
  on either 2D polar specialization, must have zero vector Laplacian. Actual
  CPU diffusion at r=1.5, phi=.7, rho=1, nu=.03 gives radial errors approaching
  -0.0203958 (cylindrical) / -0.0305937 (spherical), not zero under h halving.
  `DiffFlux` differentiates orthonormal components as scalars and adds only
  diagonal sources; angular basis-derivative coupling is absent, and spherical
  2D retains a spherical-radial diagonal inconsistent with its polar metric.
  This is a SHARED defect, not a reason for a GPU-only math correction or wider
  parity tolerance. Repair the shared vector-diffusion geometry and test its
  continuum/null-field limits, variable coefficients and energy flux before
  claiming complete curved viscous qualification. Keep the current cold build's
  compiler inputs stable until it finishes; no release pass is inferred.
- Updated runtime-input / telemetry contracts PASS-479: 142 root tests,
  3.253 s, peak owned RSS 65,260 KiB, no swap growth. Optional real-device
  observation completed (five samples); these are tool tests, not capacity
  qualification. Architecture/tooling suite separately passes 101 tests.

- Compatibility renewal and all five packages reread at 15:36 UTC. Cold core
  build-477 continues at 166/222 edges; no production source/flag changes during
  that build. User baseline is an entry-level recommended/reference workload
  configuration, not an unmeasured universal minimum. Public EN/ZH build notes
  distinguish WSL allowance, local CPU ISA and selected CUDA code images.
- Curved runtime manifest expands from four 1D cases to twelve 1D/2D/3D
  annulus/wedge RKL cases, retaining the original numeric parity/conservation
  budgets and shared canonical input. Three input contracts PASS; execution
  remains pending. The inherited spherical-3D Gaussian has no theta variation;
  this matrix is not the independent angular manufactured-solution oracle.
  New `validation/network/runtime_cases.json` adds six real-ARCH Helmholtz
  coupling cases for audit31/weak_urca and BE_NR/BD/ROS4. The common validator
  and evidence reviewer now check declared resolved policies, including Auto's
  CPU KLU / CUDA cuDSS (audit31) versus DenseLU (compact weak) selection.
  Two policy/input negative-control tests PASS; actual execution remains open.
- Optional whole-device GPU memory observation extends the existing memory
  guard rather than introducing another process supervisor. It records device
  UUID, total/baseline/peak usage, minimum free memory and sample completeness;
  display/other processes and sampling gaps are explicitly not attributed to
  ARCH. Missing counters, changed device or query failure cannot become zero
  usage/pass. Twelve guard/parser/escaped-child controls PASS. Real no-op
  telemetry check-478 PASS (1.199 s, no swap growth), before the completeness
  field addition; this is not a workload/capacity result. Capacity sampling is
  opt-in and separate from timed performance comparisons.

- Scope/performance renewal reread at 15:12 UTC: all five packages and affected
  ownership reviewed. Cold Release core build-477 continues (147/222 edges at
  15:10 UTC); no source/flag changes are made to its running production inputs.
  Large workloads remain deferred. Next: current-binary smoke and full runtime
  matrices as soon as the candidate links, with optimization, sanitizer and
  resource evidence included in final acceptance rather than another feature
  refactor. The renewed owner request is not a publish/commit authorization.
- Owner documentation/status review (2026-09-06): public large-network wording
  now describes pynucastro and CPU KLU / GPU cuDSS capabilities directly.
  Scientific model reliability depends on isotope selection, reaction data and
  applicability; this does not transfer responsibility for ARCH's numerical or
  backend correctness to a user's network. Keep large-workload follow-up and
  actual pending/pass evidence in the validation records. No gate is waived.
  Burn and local geometry status summaries now reflect their passing focused
  controls. Final application Validation starts with usable current binaries
  and a successful short integration check; no further large-network build or
  search for a globally optimal compiler configuration is a prerequisite.
  Cold/incremental build metrics, sanitizer, sustained runs and the declared
  capacity workload remain part of final acceptance, not new features to add.
- Release profile configure PASS-476 (13.065 s); cold core build-477 is RUNNING,
  not passed or capacity-qualified. At 14:52 UTC, 114/222 Ninja edges are
  complete (many early edges are small SuiteSparse dependency objects, not a
  linear percentage of remaining time). Largest completed compiler command:
  Tabular3D/aprox21, 1,795,016 KiB maximum RSS / 151.09 s. Tabular3D/aprox19
  takes 161.28 s / 1,556,260 KiB. Those are Release/O3 device builds with strict
  floating point and no compiler-cache hits, not Debug speed estimates.
  One heavy slot and top-level parallelism 2 remain in force; the memory guard
  retains 1536 MiB host headroom / 128 MiB maximum additional swap. Do not label
  this unfinished configuration a measured optimum. Existing compiler-metric
  summarizer extended with six attribution/negative controls; tooling/architecture
  total 101 tests PASS, and `git diff --check` is clean. Next: inspect final
  whole-build metrics and smoke/runtime behavior before any speedup claim.
- Regenerated local profile PASS-473: audit31 and weak math, real cuDSS provider,
  registry-derived policy witness (four CTests, 2.025 s guarded). Full weak
  science/factory PASS-474 (94.256 s, peak 442,208 KiB, no swap growth) and
  audit31 all-method/multi-step/two-storage-generation PASS-475 (181.484 s,
  peak 268,432 KiB, no swap growth), archived under
  `validation/network/results/{weak-urca-inline,audit31-inline}-20260906/` with
  unchanged observed identities and original budgets. Focused burn acceptance
  is closed; this does not qualify all EOS/application/restart paths.
  Active contract, five packages and affected ownership reread at phase change.
  Next: isolated Release core build, OpenMP enabled, audit31 + weak_urca only,
  one heavy compiler plus light parallel work under the shared memory guard.
  GNU time compiler-command metrics use CMake's project-include instrumentation
  in that build directory, without editing production math or adding another
  process supervisor. Disable compiler-cache hits for the cold-build record;
  subsequent cache/incremental measurements must be labeled separately.
- Inlineable representative packages generated-468/469 under
  `/tmp/arch-release-inline-networks.zr6m6q`; only audit31 and weak_urca are
  selected. The local core profile enables CPU OpenMP-470 while retaining one
  heavy compiler slot. Focused regenerated math/provider/factory/route build
  PASS-472 (140.503 s, peak owned RSS 811,220 KiB, min available 5,752,500 KiB,
  no swap growth). Root tooling 135 / architecture 95 tests pass. This is a
  Debug focused rebuild, not cold-core, Release throughput or capacity evidence.
  Next: new-package focused math/weak/sparse runs, then optimized core build and
  complete application Validation. Historical built-in CTest-455 transcript is
  retained in `validation/burn/results/builtin-time-20260906/ctest.log`;
  public module summaries distinguish its focused pass from final qualification.
- Inlineable lookup control PASS-467 (92.246 s, peak 441,752 KiB, no swap
  growth), archived at
  `validation/network/results/weak-urca-inline-control-20260906/evidence.json`.
  Compare outlined lookup PASS-461 (902.241 s): the shared backend, controls,
  factory/science budgets and BE_NR improvement (100.035x) agree. The weak
  packages differ only in lookup annotations/comments and generator identity;
  this is a Debug diagnostic, not optimized whole-application performance.
  Withdraw small immutable lookup outlining in `PortableCxx`, keeping heavy
  math boundaries and value-rate storage reuse. No special-case network or
  backend math is introduced. Regeneration / fresh focused tests are next.
  Registry-derived policy witness PASS-466: 33 built-in device bindings and
  analytic ODE controls; one generated network and cuDSS are explicitly covered
  by separate execution tests, not fake device witnesses. No numerical budget
  was weakened. Burn focused acceptance closes; full runtime remains open.
- Full built-in CPU scientific/negative controls PASS-455 (1087.508 s in
  Debug/O0): all twelve routes meet the original 1e-8 species/energy and
  1e-12 closure budgets. The largest species/energy errors are aprox21 BE_NR,
  7.760566758663323e-9 / 8.65707006081351e-9. Reference review-456 and CUDA
  policy/thermal controls-457 pass separately. Compact weak science/ownership
  PASS-461 (902.241 s); the 100.035x BE_NR refinement improvement is retained
  in `validation/network/results/weak-urca-bounded-20260906/evidence.json`.
  Its runtime increase makes small lookup outlining an unqualified candidate:
  current-backend/old-lookup control builds-464/465 pass, runtime comparison
  is next. Policy witness-463 failed a stale fixed route count (35 vs 33);
  derive counts from the registry and keep host-mediated/generated execution
  in its actual focused tests. Revised witness compiles-465, run pending.
  Active contract, all five packages and affected ownership reread. No large
  workload build is active. These focused passes do not close final release
  identity, complete-application Validation or optimized performance gates.
- Rebuilt CUDA burn policies (all twelve built-in routes plus helper/status
  contracts) and analytic thermal/Jacobian/order controls PASS-457 (118.506 s,
  peak 179,860 KiB, no swap growth), unchanged short-step parity budgets.
  Root tooling 135 and architecture 95 unit tests PASS. Current compact weak
  package generated-458 and separately configured-459 without altering the
  running CPU scientific CTest. Weak math/factory/trajectory build-460 PASS
  (34.164 s, peak 440,288 KiB, no swap growth). Full weak scientific/ownership
  rerun is next. CPU built-in scientific CTest-455 is still running; an O0
  high-accuracy first-order trajectory is much slower than the optimized review.
  No audit150/audit200 compiler is active or queued. Five packages/ownership
  reread before weak qualification; next remains core build/Validation.
- Independent endpoint review PASS-456 (36.163 s, no swap growth), archived at
  `validation/burn/results/independent-time-20260906/evidence.json`. DOP853 and
  Radau with two maximum-step resolutions agree with all four immutable
  endpoints: species at most 3.75e-16 and relative T at most 2.94e-14. Existing
  independent Helm 60/80-digit endpoint energies agree and differ from Host EOS
  by at most 1.22e-14. This is not a current-integrator or full runtime pass.
  Formal build-453 found an outdated test-only RhoSensitiveSolver signature;
  add the shared network-view parameter, not a production compatibility fork.
  Rebuild-454 PASS (396.100 s, peak owned RSS 2,289,772 KiB, min available
  4,069,536 KiB, no swap growth). CPU scientific CTest-455 and rebuilt CUDA
  policy/thermal controls-457 are running. Tool provenance 37 and architecture
  95 unit contracts PASS. Public capability/validation summaries now separate
  representative generated release coverage from deferred large qualification.
- Burn historical-baseline reconciliation is now explicit, pending compiled
  formal tests: retain ALL frozen-main snapshot numbers/checker controls, but
  qualify corrected integrators against one immutable DOP853 endpoint per
  network, corroborated by Radau/time refinement. This is independent TIME
  integration of the shared RHS, NOT independent nuclear rates. The existing
  1e-8 species/energy and 1e-12 abundance closure scientific budgets remain;
  they are a DIFFERENT contract from historical method-specific bit snapshots,
  not a claim that corrected solvers reproduce those snapshots. Short CUDA
  policy-parity tolerances remain unchanged; the scientific CTest runs once,
  not repeatedly inside each CUDA route. The read-only reference service reuses
  that same test executable and the existing independent Helm oracle.
  Tight review-450 has substep-limit FAILs for aprox21 (guard exit zero is NOT
  scientific success); the sweep completed before an attempted interrupt,
  with no process killed. Necessary aprox21 rtol=1e-13 / max_substeps=3,000,000
  point PASS-451: species Linf 7.760566758663323e-9; independent 60-digit EOS
  relative energy 8.657062791380149e-9; 187.707 s, peak 111,232 KiB, no swap
  growth. Application defaults unchanged. Small optimized review build-449 took
  251.857 s; it is not core compile qualification. Local profile configure-452
  removes audit150 and retains audit31; full test build-453 is in progress.
  All five packages and affected ownership entries reread.
- Owner explicitly defers large workload compilation/qualification; no large
  compiler is active. Resume burn scientific acceptance immediately, preserving
  the original error budgets and historical snapshots pending independent review.
  Latest bounded audit31 math/provider PASS-445 and full all-method trajectory
  PASS-446 (190.602 s, peak 246,572 KiB, no swap growth). Split build-444 PASS
  (78.278 s, peak 535,872 KiB, no swap growth), with Host driver 3.312 s and CUDA
  binding 73.756 s in Ninja's object records; this is NOT a cold-core benchmark.
  Build-443 exposed a missing complete IdealGas include, corrected explicitly.
  Audit150 before/after generator outputs are bitwise identical: 46,506 finite
  values, SHA-256 1b7be6489a4f699eadb54066fcc2dfdce4c280daf145d48490ac8633988e8592
  (build-447 and read-only comparison). New audit150 CUDA compilation is NOT
  qualified and is now deferred. Do not resume it from older next-action notes.
- Equilibrated provider memcheck-433 PASS (zero errors), racecheck-434 PASS
  (zero hazards). Focused sparse ODE/EOS/provider CTests PASS-436 (THREE tests;
  weak factory is not registered there), explicit 10-second weak Dense/cuDSS
  factory all-method regression PASS-439 (26.078 s, no swap growth). Audit150
  interval 1e-10 TIMEOUT-432 at 600.714 s: six completed CPU/GPU step comparisons
  pass, two-cell segment completes, but three-cell BE_NR and remaining methods
  are unfinished. Do not turn this timeout into trajectory or performance PASS.
  The initial short route remains passed-429; larger intervals remain open.
  Provider explicit kernel/metadata/scalar accounting and initial weak/pattern
  upload accounting are now implemented, pending new compiled controls; EOS
  initialization guards its actual variant, not the now-shared owner count.
- Compile-cost candidate: the real sparse trajectory harness is split by Host
  diagnostics/CPU trajectory versus a thin selected-network CUDA binding. No
  numerical body is copied. Portable generation gives runtime immutable lookups
  a bounded call, retaining constexpr literal access, and reuses value-rate
  storage for a strictly recognized value-only species Jacobian (derivative or
  unknown consumers unchanged). All 33 generator contracts pass. Separate audit31
  and audit150 generation PASS-437/441; old assets remain intact. Numerical and
  compile-cost comparison is pending, not an accepted speedup. Isolated configure
  FAIL-438 omitted nvcc from PATH; explicit installed path PASS-440. Build-442
  names a nonexistent math target and therefore builds nothing; correct from the
  actual CMake target inventory. All five work packages/affected ownership reread.
- Device-resident TWO-sided equilibration passes actual audit150 short all-method
  trajectories-429 (5.021 s, peak 295,672 KiB, zero swap growth), unchanged field
  and original-matrix residual budgets. Pure diagnostic row control-419 passed,
  and production row-only audit150-422 passed, but row-only provider-421/426
  FAILED the manufactured integrated-energy equation (backward error 0.999846).
  Preserve that negative result: row-only normalization is not sufficient.
  Reversible row/column control-424/425 passes both witnesses. Production
  `LinearEquilibration.h` owns one contiguous/permuted scaling body, consumed by
  small device kernels and Host tests; no network, EOS or ODE fork is introduced.
  The provider retains original inputs and private scaled storage, reuses column
  index metadata/factors, rejects nonfinite or erased forcing, and includes its
  new storage in the memory estimate. Build-427 takes 4.069 s without repeating
  the large network instantiation. Expanded analytic/exact-zero/unit-rescaling,
  input-preservation, invalid-data and recovery tests PASS-431 (build-430).
  New scalar error-latch traffic and kernel counts still need full backend
  resource-ledger integration; these diagnostic focused timings are NOT capacity
  qualification. All five packages/affected ownership reread. Next: sanitizer,
  actual longer 150 and weak/31/ODE regressions, then 200 and final release gates.
- Read-only actual cuDSS capture FAIL-417 confirms library-successful solutions
  fail the original componentwise residual (maximum 1): e.g. row 7 is the
  equation 1.0084660999962292*x7=0, but native IR=2 returns x7=-2.75e-45.
  Normwise agreement alone hides this. Offline zero-subspace enforcement does
  NOT fix the whole system and is not implemented. Independent SuperLU on the
  same captured matrix also has residual 1 without equilibration; row max-norm
  equilibration gives 7.49e-17 and exact x7=0. A temporary boundary control will
  test the SAME reversible row-unit normalization with actual cuDSS, leaving
  original-matrix acceptance unchanged. This control is diagnostic Host staging,
  not a production CPU fallback or a passed backend route. If valid, production
  preparation belongs on device, with common linear algebra and private storage.
- Geometry archive PASS-418 (9.062 s, peak 132,060 KiB, zero swap growth),
  source/configuration/binary identities unchanged around all three runs.
  Curated English/Chinese AMR summaries link its limited scope; raw metrics stay
  under `validation/amr/results/curvilinear-metrics-20260906/`. Final release
  and real 150/200 trajectories remain open.
- Debugger attempt-416 cannot inspect the test's Host locals: this configured
  CUDA Debug command has PTX O1 but no Host `-g`, while provider C++ does have
  symbols. The source-line breakpoint was not installed; the repeated burn
  failure is retained and no summary diagnosis is inferred. Use a temporary
  read-only CUDA/cuDSS boundary observer on the existing binary to capture the
  actual first linear system, or split Host diagnostics from the device factory.
  Both keep production math unchanged. All work packages/ownership reread;
  latest root tooling 133 and architecture 95 tests still pass.
- Audit150 first short trajectory FAIL-415 after 44.176 s (peak 279,424 KiB,
  no swap growth): CPU BE_NR first two-cell step succeeds in two attempts,
  but the GPU reports a failed burn summary. No 150 evolution gate is closed.
  Diagnose the existing debug binary's summary/continuation before spending
  another full device compile just to add Host logging. Separating the test's
  Host controls/diagnostics from the typed device factory is a responsibility-
  based compile-cost candidate; do not multiply numerical implementations.
  Audit200 trajectory build waits on this first real large-network failure.
- Real audit150 sparse-factory build PASS-388: 3350.129 s, peak summed owned
  RSS 3,200,008 KiB, minimum available 3,045,784 KiB, zero swap growth.
  This long incremental/focused compilation is NOT a cold-core benchmark or
  acceptable compile-speed claim. All-method short trajectory-415 is running
  before longer physical intervals and audit200. The geometry target build
  PASS-411 (23.187 s, peak 682,564 KiB, no swap growth), both CTests PASS-412;
  actual metric/convergence records PASS-413, memcheck PASS-414 with zero
  errors. All nine geometry/dimension combinations show second-order species
  operator consistency on CPU and CUDA. This closes that focused spatial
  control, not evolved hydro/thermal/viscous/AMR or final-identity qualification.
- Independent species-diffusion operator probe-410 shows second-order local
  spatial consistency in Cartesian/cylindrical/spherical 1D/2D/3D (build-409).
  The existing CUDA geometry test now reuses one metric-cache and one diffusion
  launch helper for both parity and this manufactured-field series. Real GPU
  target build-411 is pending, under memory protection; no network template is
  introduced by that target. This is not an evolved AMR/convergence release pass.
- Built-in tolerance review-408 completed (354.066 s, peak 116,468 KiB,
  zero swap growth). Every method completes the tested tolerance series.
  At rtol=1e-12, aprox19 BE_NR species Linf error is 5.34e-9; aprox21 is
  2.46e-8 and therefore still misses the existing 1e-8 species gate. Its
  corresponding temperature error is 23.39 K. Tightening local tolerance
  reduces BE's global error approximately with sqrt(rtol), as expected for
  first order controlled by a second-order local estimate. No acceptance
  threshold or frozen data was changed. The test's inaccurate phrase
  "independent burn references" is corrected to "frozen-main burn snapshots".
  Next reconcile with independently integrated endpoints and unchanged physical
  budgets; do not silently treat historical numerical snapshots as exact ODE
  solutions. Actual GPU/runtime and final-identity gates remain open.
- Current built-in independent time integration / mesh-in-time review completed
  in log-406 after shared accepted-state fixes (build-405). DOP853 and Radau
  agree to at most 3.75e-16 in composition and 4.32e-5 K; this independently
  checks TIME integration of the shared RHS/EOS, not reaction-rate data.
  Explicit tolerance series and historical snapshot comparison are running-408
  (build-407), with all frozen fixture data/thresholds unchanged. Audit150
  remains active in normal PTX O1: inspected PTX contains 463,662 lines, with
  distinct value/derivative rate-buffer specializations despite dT being
  disabled in the species Jacobian. This is a compile-cost candidate, not a
  measured optimization; do not change its in-flight package. Weak initial
  upload/pattern bytes and owner counters still need resource-ledger coverage.
  Ordered work packages and affected ownership entries reread; final gates open.
- Shared thin-shell and polar measure fix PASS-395 against the original
  80-digit negative control; 12 immutable 70/90-digit cases cover both poles,
  narrow and broad cells, and radial unit scales. CPU metric/equilibrium/CFL
  target build/run PASS-396/397, max relative measure error 3.04e-16.
  Runtime-uploaded CUDA inputs PASS-404 (build-403); initial constant-input
  run-399 alone is not runtime-arithmetic evidence. Standalone compile-401
  omitted the target's existing relaxed-constexpr flag and failed; consequent
  missing-executable attempt-402 is retained, neither is a pass. No production
  compiler flag was changed. Root tooling 133 and architecture 95 PASS.
  Full geometry kernel target/convergence and final application remain open;
  audit150 build-388 is still active. The short source check-400 agrees with
  independent angular integrals at its two sampled states; no speculative
  source-term change was made from the measure defect alone.
- Independent 80-digit finite-volume integrals FAIL-393 on the unchanged
  shared metrics sampled in log-392: thin shells at r=1e10 lose about 1e-7
  relative accuracy, and a 1e-7 polar angular cell loses about 8e-4. This is a
  shared cancellation defect, not a CUDA-only physics difference. Factor the
  radial polynomial differences and give spherical volume/radial area one
  stable angular measure; add independent Host/CUDA controls at both poles.
  Audit150 build-388 is still assembling with no swap growth. Its actual
  dependency record does not include GridMetrics, so this repair does not
  invalidate that in-flight build. All five work packages/ownership reread.
- Audit31 focused trajectory archive PASS-390 (197.947 s, peak 268,728 KiB,
  zero swap growth), all-method/step/storage coverage and observed identity
  verified at `validation/network/results/audit31-trajectories-20260906/`.
  Current shared ODE memcheck PASS-391 (11.054 s), not full-runtime sanitizer
  qualification. Audit150 compiler is making progress in ptxas (about 2.5 GiB
  RSS, one active compiler), with safe headroom and no swap growth. While it
  builds, inspect independent geometry conditioning and remaining acceptance
  inputs; do not change active large-network dependencies or treat its in-flight
  compilation as a pass. Larger trajectories, historical built-in baselines and
  all remaining final gates stay open.
- Actual full audit31 matrix PASS-386 (186.835 s, peak 247,728 KiB, zero swap
  growth): max field errors BE_NR 5.56e-17, BD 1.29e-19, ROS4 8.10e-11, all
  unchanged budgets. Weak scientific/factory archive PASS-389 (109.582 s,
  peak 441,268 KiB, zero swap growth), stable observed identity. Common focused
  provenance now also guards artifact replacement and derives configured build
  type, shared by weak and sparse runners; 37 provenance and five sparse-reader
  contracts PASS. New sparse reader accepts the actual complete transcript and
  rejects partial/missing/duplicate/nonfinite/over-budget controls. Current
  audit31 archive-390 is running; source/tool edits paused for its identity.
  Audit150 real factory build-388 remains active with healthy memory headroom;
  no build/trajectory pass is declared until it finishes.
- Current audit31 full matrix-386 has completed/passed its two-cell BE_NR
  segment; the three-cell repetition and remaining methods are still running.
  Timings show host-API request/factor/solve synchronization is expensive on
  this small-lane workload; this is not a speedup or controlled benchmark claim.
  Weak factory/trajectory rebuild PASS-387 (24.178 s, peak 437,472 KiB, no swap
  growth). Actual audit150 trajectory target starts guarded serial build-388;
  current weak scientific archive-389 runs with source edits paused for its
  observed-identity check. No 150/200 trajectory pass is claimed from source
  presence or earlier mathematical-only tests.
- All three ODEs' one-/16-substep analytic ranges, direct accepted-state
  projection/discard controls, sparse retries and EOS failures PASS-384 on
  CPU/CUDA (four CTests). Build-383 PASS (55.263 s, peak 430,912 KiB), actual
  audit31 rebuild PASS-385 (91.500 s, peak 570,556 KiB), no swap growth.
  Root tooling 131 and architecture/header 95 tests pass after the BE repair.
  Full current audit31 method/storage matrix is running-386; weak factory/
  trajectory rebuild-387 precedes a fresh scientific archive. Ordered work
  packages and affected ownership entries reread; no historical fixture,
  physical definition or acceptance threshold was changed for these passes.
- Corrected accepted-state/fixed-step/projection controls PASS-380 on CPU/CUDA,
  including all prior sparse retries. BE_NR exact source negative FAIL-381
  demonstrates the same lost cumulative heating. It now uses the SAME accepted
  state authority and an increment-form Newton residual dt*f-increment; no
  timestep/error/closure threshold changes. Host witness PASS-382. Full controls
  rebuild-383 is next before actual all-method trajectories and 150/200 builds.
  BE continuation adds three doubles/equation (accepted pair plus Newton
  increment), while BD/ROS4 add two; existing typed sizing includes them.
- Actual audit31 with accepted-step compensation: ROS4 middle trajectory
  PASS-378 (max field error 8.10e-11, limiter 7.61e-11), BD PASS-379 (max field
  error 1.29e-19, limiter zero), original budgets unchanged. Build-376 PASS
  (99.544 s, peak 567,772 KiB, zero swap growth). Corrected fixed-step/direct
  projection controls build PASS-377 (19.120 s, peak 322,104 KiB); execution-380
  is pending. Before expensive larger-network builds, inspect BE_NR with the
  same exact small-increment witness; do not assume its different Newton-state
  representation is immune. Full application and final qualification remain open.
- Accepted-state control build PASS-374 (32.168 s, peak 428,256 KiB, no swap
  growth). First execution FAIL-375: all ROS4 translation/substep checks and
  sparse controls pass, but the intended fixed-step BD fixture only pinned the
  maximum growth factor. BD's own safety factor shrinks every step, so this
  fixture never covers the requested interval within 100 steps. Pinning BOTH
  min/max growth to one corrects the test setup; no production controller is
  changed and the required count remains exactly 16. Direct accepted-state
  discarded-trial/projection controls were also added; rebuild/rerun pending.
- Full ROS4 trace-371 has no failed original-matrix residual and matches all
  request/stage/attempt/reject histories. The first two energy handoffs match;
  the third differs by three energy ULPs. Independent fixed 16-substep source
  witness FAIL-372: rounding accepted substeps individually can lose the entire
  small total heating. Shared `OdeMath::AcceptedState` now retains compensated
  increments across accepted steps for BD/ROS4, while real projections reset
  the sum and rejected trials never update it. No energy definition, tolerance
  or method coefficient changes. Host source range PASS-373; CPU/CUDA controls
  build-374 is pending before real audit31 reruns. Workspace increases by two
  doubles/equation, automatically included in the existing typed sizing.
- Current weak scientific archive PASS-368 (98.524 s, peak 441,332 KiB,
  zero swap growth), stable observed identity; public network summaries now
  link `weak-urca-increment-20260906` and retain full-application limitations.
  Actual ROS4 energy diagnostic FAIL-369: the failing lane has identical old
  total energy and post-step mixture cv, but recovered temperatures differ by
  four ULPs. ENUC remains outside the original budget. The shared-continuation /
  actual DriverBurn multi-step trace builds PASS-370 (37.260 s, peak 467,404 KiB,
  zero swap growth); inspect original residuals, time/order histories and handoff
  without altering the production energy definition. All five release work
  packages were reread; no final release, large-network or capacity gate is waived.
- ROS4 affine arithmetic controls PASS-364 (expanded exact signed-source range,
  CPU/CUDA thermal/order, sparse retry and rollback). Real rebuild PASS-363
  (96.606 s, peak 568,188 KiB, zero swap growth), but real ROS4 still FAILS ENUC
  at step 2, relative 2.299095e-10-365. The independent roundoff repair is valid
  but does NOT close this real-network failure. The harness now reports old/new
  energy, recovered temperature and mixture cv for failed fields; rebuild-367
  is pending to distinguish EOS/handoff amplification from remaining ODE or
  provider differences. Weak factory/trajectory/EOS targets rebuild PASS-366
  (42.225 s, peak 443,176 KiB, no swap growth); current scientific archive-368
  is running. Do not edit numerical sources/tools while its identity is captured.
- ROS4 broader dyadic-source negative control FAIL-360 (two ULPs at T=2^30),
  whereas its initial two-point sample passes-359. The single ROS4 stage-state
  helper now uses the existing compensated sum/product authority; all tableau,
  error weights and physical rules stay unchanged. Host range PASS-361. Expanded
  BD/ROS4 signed heating/cooling range and existing controls build PASS-362
  (27.232 s, peak 423,848 KiB, zero swap growth); execution-364 and actual
  audit31 rebuild-363 are next. This is an independently demonstrated shared
  roundoff defect, not proof yet that it accounts for the entire real ROS4 ENUC
  failure. No baseline refresh or acceptance change.
- Shared BD increment controls PASS-356 on CPU/CUDA, including signed dyadic
  heating/cooling, Jacobian/order, quadrature/rollback and sparse failures. Real
  audit31 rebuild PASS-355 (103.519 s, peak 568,148 KiB, zero swap growth). The
  previously failing BD middle trajectory now PASS-357 with the unchanged field
  budget (max field error 1.39e-19, limiter error zero). ROS4 independently
  FAILS step 3 ENUC by 2.282483e-10-358. Its unchanged two-point constant-source
  witness passes-359; investigate broader offset sensitivity and original-matrix
  residuals rather than assuming BD's cause automatically applies. Tooling
  131 and architecture/header 95 contracts pass after the BD edit (console).
- BD trace-351 completes with matching requests/order/attempts/rejects; tiny
  step-size roundoff leaves a five-ULP final temperature difference, amplified
  by ENUC differencing. Independent exact constant-source control FAIL-352:
  absolute-state midpoint/extrapolation produces six ULPs instead of four ULPs
  of heating on T=2^30 (50% increment error). The common BD now extrapolates
  relative increments with unchanged physical clamps, controller and storage
  size. Host exact witness PASS-353 (zero ULP error); signed Host/CUDA controls
  build PASS-354 (28.139 s, peak 424,852 KiB, zero swap growth), execution-356
  and real audit31 rebuild-355 pending. No acceptance budget or historical
  fixture changed. Production BTF weak archive PASS-348 (90.433 s, peak 440,816
  KiB, zero swap growth), stable identity, independent coarse/tight convergence
  retained at `validation/network/results/weak-urca-btf-20260906/`. This archive
  predates the BD increment edit and is not final-identity evidence.
- Production middle-duration audit31 FAIL-345 after 186.833 s: BE_NR completes
  both pool/storage generations with 13,194 CPU attempts / 98 rejects and field
  error below 5.5e-20, but BD first-step ENUC differs by 3.366522e-10 against the
  unchanged 2e-10 budget. This is neither a pass nor evidence of a shared math
  defect yet. Trace BD factor/solve/accepted-state histories before deciding
  whether linear accuracy or energy differencing is responsible. Current weak/
  sparse/EOS-failure rebuild PASS-344 (55.315 s, peak 441,124 KiB), three provider/
  sparse/EOS controls PASS-346 (4.021 s); no swap growth. Larger networks remain
  behind this correctness check, not silently waived by short-trajectory passes.
- BTF is now the production cuDSS ordering; matching is disabled, native IR
  remains 2. Opaque library/CUDA/DATA_INFO errors fail closed through the shared
  `CuDssResult::require_success`, not adaptive retries. Provider memory estimates,
  error controls, factor reuse and mixed units PASS-336/339. Production audit31
  rebuild PASS-337 (104.660 s, peak 567,468 KiB, zero swap growth; overlapped with
  a GPU trajectory, NOT a controlled benchmark), actual short trajectory
  PASS-342. The independent binary-exact trace chain passes both BTF and the old
  configuration-339/343: useful range coverage, NOT the negative witness for
  this defect (the actual audit31 unchanged failing/passing pair is that witness).
  Long candidate run-334 completes/passes the two-cell BE_NR segment but times
  out at 600 s before the whole matrix; it is NOT a full trajectory pass. Next:
  use production per-step counts to profile long execution, rerun current weak/
  singular controls, then real 150/200 and remaining release gates.
- Valid BTF without matching resolves the audit31 first-step divergence-325:
  original residuals pass, both backends take 18 attempts / 2 rejects and final
  temperature is identical. The unchanged previously failing 1e-12 s full
  typed-cell trajectory now PASSES all BE_NR/BD/ROS4 routes-330 (7.026 s);
  ENUC/limiter align, maximum reported field error below 8.1e-22. Independent
  provider 32/151/201 and mixed-unit controls PASS-329; 10 s weak factory
  PASS-331 (14.045 s). These are candidate configuration-adapter tests, not yet
  production binaries. Singular retry is next; then check longer trajectories,
  provider resource/error contracts and promote only the verified configuration.
- BTF + matching experiment is unsupported, not a physics failure-321/322:
  NVIDIA's own error log says these two choices are incompatible, consistent
  with the documented API restriction. The experimental production ordering
  setting was removed; matching + native IR=2 remains the current provider.
  A temporary configuration-only adapter (includes the production provider,
  no copied implementation) is testing valid BTF without matching-323/324.
  Do not report this experiment as a production or sparse-trajectory pass.
- Audit31 first divergence is linear, not just diagnostic differencing:
  cuDSS status/info are zero, but the unchanged componentwise residual rejects
  row 16's ~2.8e-43 correction (relative row residual 7.48e-3)-312/315, while
  KLU passes. GPU then retries at dt/4. Native IR=4 instead of 2 does NOT repair
  it-318; production default remains 2. Investigating the provider's documented
  nonsymmetric BTF/COLAMD ordering, with shared math and all budgets unchanged.
  [NVIDIA's ordering contract](https://docs.nvidia.com/cuda/cudss/types.html#cudssconfigparam-t)
  describes its general-matrix/global-pivot factorization and LU fill-capacity
  restriction; resource/error contracts must be checked before acceptance.
- Real audit31 ENUC parity FAIL-308/309 (2e-10 test field budget unchanged):
  first step's relative error is 1.8899591e-6 at 1e-12 s total duration and
  1.6159136e-8 at 1e-10 s. Earlier state fields pass. Scaling suggests a small
  energy-increment conditioning contribution, not yet proof that the error is
  harmless. The temporary shared BE/KLU/cuDSS first-divergence probe compiles
  PASS-310 (45.248 s, peak 506,440 KiB, no swap growth); inspect original-matrix
  residuals and adaptive histories before changing physics or acceptance.
  Existing production runtime comparisons already distinguish ENUC differencing
  from state budgets; do not silently change the newly declared test budget to
  get a pass. Larger 150/200 trajectory builds are held until this is understood.
- Actual audit31 sparse target build PASS-305 (125.686 s, peak 567,520 KiB,
  zero swap growth, includes KLU dependency build). First real C/O trajectory
  at rho=1e7, T=3e9, 1e-8 s times out at 180 s-307, not a numerical pass or a
  proven numerical failure. GPU activity and safe memory headroom were observed.
  A line-buffered 1e-12 s diagnostic is running-308 to isolate initial-step
  correctness from long adaptive execution. Root tooling 131 PASS-306. No
  historical baseline or tolerance budget was changed to obtain acceptance.
- Current audit31/150/200 packages regenerate PASS-300 into
  `/tmp/arch-release-final-networks.oWugNw`, current generator identity
  `b04e6fb59c9d04754fcc184b28ddb3507894e0626d245597928bf2732ae3a3c4`.
  A single selected-package sparse trajectory test uses shared IdealGas/ODE/
  DriverBurn leaves, CPU KLU and the actual typed cuDSS factory, with explicit
  physical/composition controls. It adds no production physics. Isolated CMake
  configuration PASS-304; audit31 target is compiling serially-305. Attempts
  301/302 failed to find nvcc / had no build graph, not code or science failures.
  Architecture/header audit 95 PASS-303. Public network summaries now separate
  current coverage from retained historical CPU v3 raw evidence. Next: execute
  real audit31 trajectories, then serial 150/200; full runtime, geometry and
  final capacity/compile/Validation gates remain unchanged and OPEN.
- cuDSS invalidation repair PASS-294/296: singular first-matrix retries recover
  with unchanged CPU states/status/retry histories, all three manufactured ODEs
  and 32/151/201-equation providers pass. The added three-unit independent
  system passes with matching but fails without ONLY that config-298. Build-293
  PASS (32.180 s, peak 439,392 KiB, zero swap growth); build-292 did no work and
  is not a repair build. Weak archive-299 PASS (98.430 s, peak 463,436 KiB,
  zero swap growth), observed identity stable before/after. Evidence lives at
  `validation/network/results/weak-urca-matching-20260906/evidence.json`;
  constant-cv scope, coarse negative and tight convergence retained, not a
  complete application/large-network/final-release qualification. Next:
  real generated CPU KLU / GPU cuDSS trajectories, starting at audit31.
- cuDSS first-divergence trace-280 isolates a library-successful but inaccurate
  correction, correctly rejected by ARCH's unchanged original-matrix residual.
  Native matching/scaling restores the 10 s weak typed factory PASS-283 for
  all three ODEs, with near-roundoff first-correction agreement-288. However,
  the singular-first-matrix retry regression FAILS-284/290; the identical test
  object with only matching disabled PASSES-291. Device residual rejection
  must invalidate value-dependent analysis/scaling as well as factors. The
  existing integer request protocol now carries this rejection without another
  numeric transfer or fence; rebuild/retry and weak archive are next. No gate
  is closed by the long weak pass while this failure remains.
- Long factory control FAIL-276/278: BE_NR + cuDSS at 10 s differs from Host
  Dense total energy by 7.2446163e-7 relative, beyond the unchanged 2e-10
  provider-parity budget. Dense CUDA passes that same cell comparison. The
  archive under `validation/network/results/weak-urca-20260906` retains failure
  logs and has NO passing evidence.json. Short factory passes do not override
  this failure. A shared-continuation first-divergence probe compiles-279 and
  is now running-280; inspect original matrices, linear residual rejection and
  step histories before changing either ODE or provider settings.
  Independently, tight-rtol Dense Host/GPU Urca trajectories PASS-271/273:
  BE_NR normalized endpoint error 3.3544e-8, BD/ROS4 near roundoff; five evidence
  reader controls PASS-272. Coarse rtol=1e-7 is correctly rejected by the new
  scientific budget-275, a retained negative control, not a release failure
  waiver. The archive runner reuses the common process logging and provenance
  helpers. No final application, long sparse weak trajectory or release claim.
- Original Suzuki-data Urca oracle PASS-267/269 (10 s / 0.1 s): independent
  SciPy regular-grid interpolation, constrained two-isotope balance and
  DOP853/Radau agree at roundoff for 10 s. No ARCH mathematical body is used.
  For the controlled constant-cv case, establish a 1e-7 maximum absolute error
  on (X, T/T_initial, signed_source/(cv*T_initial)), separate from the unchanged
  2e-10 Host/CUDA parity budget and 2e-11 independent-integrator agreement.
  Do not confuse local ODE rtol with a global trajectory bound: the original
  rtol=1e-7 BE_NR trajectory has about 3.36e-6 normalized endpoint error and
  does not pass this new scientific control. Tight-tolerance/refinement runs
  are next, without modifying production defaults or loosening either gate.
  All eight generated weak EOS delegates and the actual Host control TU
  compile PASS-264 (132.659 s, peak 427,168 KiB, zero swap growth), not an
  application link or cold-core build qualification.
- Production typed weak owner/cell paths PASS-257 for BE_NR/BD/ROS4 with both
  DenseLU and actual cuDSS. Current generator enables recognized explicit weak
  views only after complete lowering. New package-258 is registered by CMake-260;
  focused build-261 PASS (71.486 s, peak 786,688 KiB, zero swap growth), five
  CTests PASS-262, expanded factory/stream/invalid-upload controls PASS-263 and
  10 s trajectories PASS-265. Tooling 131 PASS-259. Generated eight EOS delegates
  plus the actual Host control TU are compiling serially-264 using their exact
  CMake commands; this is an object audit, not a final application link.
  Next: independent original-table Urca trajectories, then real large-network
  production trajectories and the remaining ordered release gates. Curated
  public docs now describe weak binding and the full 31-equation cutoff while
  explicitly retaining scientific/final-application qualification as open.
- Real Urca trajectories PASS-246/250 for BE_NR/BD/ROS4 on Host and actual
  GPU at rho=4e9, T=5e8 over 0.1 s / 10 s. These use a constant-cv test EOS,
  check signed losses and energy closure, and preserve the 2e-10 state parity
  budget; they are not independent weak-trajectory or full application evidence.
  Relevant-state full mathematical comparison PASS-248; layout/header audit
  95 PASS-249. Backend factory binding now carries explicit immutable network
  values, with a persistent dense-owner slot and a sparse-pool-owned upload.
  These last factory edits remain uncompiled/unqualified; the generated weak
  registry flag stays disabled pending owner reuse/failure and launch tests.
- Real weak table math compiles-242 and runs on actual GPU PASS-243 with the
  immutable device owner (N=4, nnz=12, unchanged 2e-10 budget). First compile
  239 caught old aion_inv/zion Host-array references after portable lowering;
  generator fix reuses the common small-constant accessors. Tooling 131 PASS-241.
  This run uses the generic low-density state, not full Urca trajectories.
  The package is deliberately still CPU-only in the production registry until
  backend owner/launch binding and relevant-state checks pass. Device allocation
  error/fence helpers moved from the complete runtime TU into their existing
  narrow header; no duplicate definition remains. Null/default CUDA streams
  now receive the same completion fence before an allocation is retired.
- Explicit network views now travel through the same BE_NR/BD/ROS4
  continuations and synchronous entry points. Two immutable rate bindings of
  one type remain independent and pass analytic Host/actual CUDA controls-237;
  existing thermal/energy/CSR tests remain passing. CPU factory selects the
  Host view; CUDA must supply a backend-owned view, with no device global.
  Real weak N+2 registry/factory/driver contracts also PASS-235 (three tests).
  Now lowering the embedded weak-table data into an explicitly borrowed flat
  store, keeping the upstream interpolation and newly corrected derivative
  bodies. Production CUDA owner binding and real trajectories remain OPEN.
- Exact runtime-layout focused CTests PASS-229 (four tests, actual CUDA),
  native GPU capability/Auto boundary contract PASS-230, tooling 129 PASS-231.
  Real weak Urca package now declares N+2 and auxiliary metadata (generation-232);
  an isolated CPU configuration registers it as CPU-only, checks header extent
  against the manifest, and builds the existing factory/continuation/driver
  contracts PASS-234. This is not yet a nonzero-duration weak trajectory or GPU
  table-owner qualification. The next gate is shared immutable table views and
  real weak / large-network trajectories; final application rebuild remains open.
- Two real Host weak packages coexist and produce identical RHS/Jacobian
  values (build-221 / run-222); 129 tooling tests PASS-218. Now wiring exact
  ODE extents through CPU packing, metadata, dense workspace and CSR coupling.
  Auto must use the complete 31/32-equation boundary, not isotope count when
  a passive source integral is present. Generated temperature differentiation
  must likewise index the physical temperature, not the last auxiliary state.
  Production weak quadrature is still disabled until these consumers pass.
- Weak-table coordinate primitive passes independent analytic power-law,
  clamp/knot, nonfinite-domain and real-table difference-refinement checks-212;
  36 original RHS/energy sample records are bitwise unchanged-214. Shared
  bilinear expressions use the existing `timmes::Dual`, not another interpolator.
  The generated Host mathematical header now adds the weak rho*Ye composition
  chain rule and nonconservative energy gradient. Real Urca diagnostic-217
  reduces the finest-difference maximum discrepancy from 1.0 to 3.69e-9.
  This remains a CPU-only package with N+1 runtime state: the independently
  tested N+2 quadrature, data owners and dispatch are not yet connected.
  Host weak headerization also needs per-package include guards; the common
  guard/loop-depth transforms are reused, with multiple-package controls next.
- Shared signed energy quadrature now passes independent Host/CUDA cooling and
  reaction/cooling trajectories for BE_NR/BD/ROS4, time refinement, exact
  Jacobian structure and failed-provider rollback (build-206, tests-207,
  detailed metrics-208). Before the change, the same new Jacobian contract
  fails-201. Existing no-loss paths retain their original N+1 layout; the
  optional N+2 test layout is not yet enabled in production weak packages.
  Driver packing, registry metadata, exact matrix-size selection, CSR coupling
  and device owners still need wiring before any weak-network capability claim.
  Next: reuse the existing dual-number utility in the upstream table coordinate
  expression, verify exact values/independent gradients, then connect the
  missing rho*Ye/energy derivatives and ownership/runtime interfaces.
- Audit200 serial build PASS-188: 1789.286 s, peak owned RSS 3,495,040 KiB,
  minimum available 3,474,852 KiB, no swap growth. Actual GPU math PASS-199,
  neq=201 / nnz=3625 at the unchanged 2e-10 budget. Together with 31/150,
  this closes these selected mathematical comparisons, not production ODE
  trajectories, current core package registration or final release identity.
- CPU-only generated packages lacked the scalar `aion` / `energy_weight`
  interface required by shared ODEs. Real weak Urca package compile fails-193
  before the generator fix and passes-194 afterward; zero-duration interface
  run passes-195 for all three ODEs, not a trajectory. Portable output replaces
  these Host bodies instead of duplicating them; 122 tooling tests PASS-191.
  Real Na23/Ne23 table diagnostic-197 also proves missing rho*Ye composition
  dependence and nonconservative energy derivatives in its generated Jacobian.
  Differences persist under three step halvings. No GPU port can close this
  shared mathematical gap merely by uploading table arrays.
- Focused current-constant CTests PASS again-198, no skips, before/after identity
  unchanged; archived under `validation/eos/results/current-constants-20260906`.
  EOS module entry pages are now curated; the historical raw source-table
  assessment is preserved under `results/`. No final application qualification.
  Next: integrate a signed nonconservative-energy quadrature through the same
  ODE stages (independent analytic controls first), then shared table data/views,
  complete derivatives and actual large production trajectories. An auxiliary
  quadrature is not a second ODE algorithm or permission to change timestep or
  energy tolerances. All launch/state extents and actual matrix-size dispatch
  must be reviewed before enabling generated weak packages.
- Current-constant focused scientific tests now PASS-187: CPU constants/sparse
  continuation, actual CUDA thermal math, EOS owners/parity and four-network
  NSE. The independent EOS/NSE data are reproduced by the validation scripts;
  no production output generated their expectations. Old reaction snapshots,
  actual Host/Device comparison budgets and failure-preservation checks remain.
  Reference-contract reconciliation is explicit in the ownership ledger
  (cv/analytic fallback rounding, exact inverse energy and the existing general
  differenced sound-speed criterion); do not describe it as preserving every
  old same-implementation raw-bit margin. Audit200 serial retry is running-188
  using the conserved-energy package, with no other heavy build active.
  Next: actual 200 math, weak ownership/losses and full trajectories; current
  ARCH application/restart, complete geometry, capacity and release runs remain.
- Build-158 PASS: the CUDA backend library, compile probe, EOS/NSE, thermal
  math and CPU constant/continuation targets complete 78/78. Guarded wall time
  3773.331 s, peak owned RSS 2,345,240 KiB, minimum system available 1,894,968
  KiB, zero swap growth. Earlier overlap with attempt-156 makes this NOT a
  controlled build benchmark; the ARCH application itself still needs relink
  and final runtime validation. Actual affected CTests are now running-182.
  Independent NSE 70/90-digit references PASS; current Host NSE agrees below
  2.04e-13 abundance / 4.27e-14 energy (log-174), and original HEAD is rejected
  by the same oracle (negative-179). Independent Helm endpoint-fit references
  and inverse/entropy-path results PASS-175/180/181; the current Host diagnostic
  agrees (log-177). Next: integrate these scientific expectations without
  weakening backend parity, retry audit200 serially, then remaining ordered gates.
- Documentation audience separation is implemented: public backend guides now
  describe capabilities, common policies and explicit limitations; the original
  detailed reports are preserved in `CudaBackendEvidence*.md` here. README links
  no longer promote one machine's compile-pool experiment as a requirement.
  Local Markdown links PASS; root tooling 121 PASS-168. Full validation-table
  curation still waits for current-artifact scientific evidence. Build-158 is at
  59/78, with no compiler error or observed swap growth. An independent NSE
  Saha-root oracle is being checked at 70/90 digits before it may replace only
  the constant-sensitive historical NSE expectations; reaction-data snapshots
  remain unchanged. Next: affected EOS/NSE tests, serial audit200 retry, then
  weak-table/loss and production trajectory gates.
- Current-constants increment (owner-approved, supersedes the earlier
  relocation-only contract): one disciplinary SI/CODATA 2022 set; no per-EOS or
  legacy profiles. Definitions and independent 70-digit derived values PASS,
  CPU sparse continuation PASS-159, actual existing CUDA constant kernel returns
  all 11 values bit-identically PASS-163 (6.011 s small build-161). Tooling
  121 PASS-162. Architecture audit initially caught a missing exact resource
  consumer registration for the BD controller witness-166; the registry and
  positive/foreign-source/object negative controls are now updated, 94 PASS-167.
  This does not qualify the affected EOS/NSE/burn trajectories or
  rewrite their historical snapshots. Core/EOS/NSE build-158 is still running.
- Current-constants independent built-in review completed-165: DOP853 and Radau
  agree to at most 3.75e-16 absolute composition and 4.32e-5 K (about 2e-14
  relative near 2e9 K). Correct-J ROS4 temporal refinement retains the previous
  behavior; aprox19 temperature error falls from 8.9473 K (one interval) to
  1.445e-4 K (16 intervals). This is diagnostic RHS/trajectory evidence, not a
  final application or accepted-energy/restart qualification. The separately
  built library is `build/release-current-constants-network-review.so` (log-164);
  it does not overwrite the pre-unification independent-review library.
- Audit150 actual math PASS-155 (neq=151, nnz=2665, unchanged 2e-10 budget).
  Audit200 build-156 was stopped safely by its memory guard while the core
  also compiled: 325.770 s, peak owned RSS 3,048,960 KiB, system available
  1,818,992 KiB at stop, zero swap growth. It is NOT a build pass and not a
  controlled single-build capacity measurement. Retry only after the core
  build finishes; do not overlap the two heavy jobs again on this WSL limit.

- BD repair in progress: absolute partial pivoting selected the temperature row
  ahead of a composition identity row. Recovering an approximately 1e-11
  abundance increment from approximately 0.067 thermal-size intermediates
  amplified rounding; same-input RHS matched at the observed first large
  divergence. This is a shared provider conditioning issue exposed on CUDA,
  not a second BD/EOS formula. Scaled pivot selection changes neither the
  equation nor the energy/controller definition; independent componentwise
  accuracy/factor-reuse/failure controls and full-program validation are being
  added. Frozen CPU reference and focused trajectory PASS are not yet a
  final-binary qualification.
- Independent compact-provider regression now PASS on CPU and CUDA (log-97):
  analytic mixed-unit subsystem, componentwise residual, equivalent row-unit
  changes, factor reuse, singular/NaN/Inf rejection. The unmodified HEAD
  provider fails the same independent oracle (negative-control log-96).
  Core application and all built-in policy routes are rebuilding in log-98.
- Core rebuild-98 PASS: 873.593 s, peak owned RSS 4,155,272 KiB, minimum system
  available 2,189,676 KiB, zero swap growth. A separate generated-temperature
  probe compiled concurrently, so this is NOT a controlled cold-build benchmark.
  Actual ARCH SHA-256 `c6e7305eaf55407bdb70d0d2443021cecc4e9ceddc4cb8a504b750046e7f6fe8`.
  BD report: `build/release-validation-bd-20260906/backend-validation-evidence.json`,
  original input/budgets and post-run identity verification PASS. Step 10 has
  zero controller/ENUC discrepancy, maximum conserved-field absolute difference
  1.11022e-16. Built-in policy log-102: all 16 tests PASS.
- Retained-layout audit150 reproduces the derivative failure (logs-99/100):
  CPU 8.1183645459822051e19, CUDA 8.1183645437069492e19, relative 2.8026e-10.
  A temperature-step scan demonstrates finite-difference cancellation.
  A separate metadata defect is now proven: SimpleCxx energy uses emitted
  `network::mion` (A_nuc times its mass unit), while the adapter independently
  reconverted Nucleus.mass with another constants edition. At the diagnostic
  state this changes reconstructed energy by about 9.17e-8 relative. The
  generator now reads the exact emitted mass/conversion authority; 18 fast
  generator contracts PASS. Actual regenerated-package checks are pending.
- Generated-network numerical increment: one shared fourth-order temperature
  difference policy (full RHS, including screening/energy), central stencil or
  fourth-order forward boundary stencil; precision-based relative step
  epsilon^(1/5). Existing `CompensatedSum` now supports explicit-FMA product
  residuals; the generated nuclear-energy dot product reuses it with unchanged
  upstream masses and reaction expressions. Host/CUDA analytic convergence,
  boundary, invalid-input and cancellation tests PASS (logs-106/107/111).
  Root tooling: 120 tests PASS; generator subset: 20 PASS.
- Regenerated audit31/150/200 assets are separate from registered packages:
  `/tmp/arch-network-derivative-final.qZQKqP`, generator hash
  `e6e3f6ded338159a5ec115b408e17ed2157cfa511ea9d18490a5e90e66131b7e`.
  Generation-109 all PASS. Audit31 Host/CUDA full math build-112 (24.083 s,
  peak 405,632 KiB) and comparison-113 PASS, neq=32/nnz=486. Audit150 build-115
  is running; audit200 and all actual production trajectories remain OPEN.
  None of these is a weak-loss quadrature or final release qualification.
- Header-core rebuild-57 PASS after fixing a direct include exposed by build-54.
  Nine CPU/GPU foundation and provider tests PASS (log-60). Fresh Cartesian/
  curved AMR and both eight-route restart suites PASS again (log-62).
- Real generated audit200 optimized math compilation AND GPU RHS/Jacobian/
  temperature comparison passed (logs-37/48). The compile took 1618.419 s;
  large-network compilation speed and production trajectories remain OPEN.
- The later RHS-row experiment compiled for 150/200. Audit150 GPU comparison
  FAILED only at the scalar energy temperature derivative (logs-66/67); audit200
  passed (log-77). The experiment is withdrawn: its 200-species compile/RSS
  measurements did not establish a benefit. Existing Jacobian boundaries remain.
  The 150-species derivative issue must be rechecked on the retained layout;
  neither the issue nor actual large-network production trajectories are qualified.
- Uniform matrix log-46 FAILED at `burn_bd_two_block`, step 10: stored burn
  timestep differs by 5.0094902e-7 relative against the existing 2e-8 budget.
  Final energy/ENUC are bitwise equal and conserved-field checks are within
  budget, but the controller failure is not waived. The full-runtime profile
  therefore has NOT passed; later cases in that matrix were not reached.
- Current-header follow-up log-64 reproduces the BD controller failure. Its
  separately run periodic-advection case completed 1016 steps with bitwise
  CPU/GPU physical-field parity; that single success is not a full uniform pass.
- Header/tooling contracts currently pass: 117 root tooling tests, 94 architecture
  tests, direct CUDA incomplete-Grid/EOS compile probe and actual core build.
  The latest generated-config include change passed separate Host/CUDA checks;
  it does not retroactively change the registered external audit31 package.
- Actual-program uniform external gravity passed CPU/CUDA RK2/RK3 parity and
  the pre-existing analytic 1e-12 Linf budget (logs-75/76). Each integrator's
  CPU/CUDA checkpoints are byte-identical. Full coupled qualification is OPEN.
- Weak-table ownership/energy consistency, actual large-network production
  trajectories, complete scientific validation, sanitizer and capacity gates
  remain OPEN. Do not publish a full CPU/GPU parity claim yet.
- The renewed owner direction makes the passing CPU BD behavior the reference.
  No burn semantic change or tolerance relaxation has been made to pass parity.

## Non-negotiable contract

- One mathematical/physical authority for CPU and CUDA. Backend-only storage,
  views, kernels, launches and third-party linear providers may differ. Keep
  registry/factory/policy dispatch; no case, isotope or backend-specific fixes.
- Full parity means all currently supported CPU production capabilities,
  including external gravity and generated networks. Self-gravity, Jeans,
  WENO5 and custom-network NSE are not new scope: neither production backend
  currently supports them. Built-in NSE is in scope.
- No hidden CPU fallback, tolerance widening, field masking or refreshed
  reference values merely to obtain a pass. Failed stages must not commit.
- The owner's 2026-09-06 request reopens shared geometry corrections previously
  deferred on 2026-09-05. Derive/document conventions and correct both backends;
  retain the project's 2-D spherical polar specialization.
- Constants use one current set organized by discipline, with units and sources.
  The owner's subsequent unification request authorizes replacing older numeric
  values, without maintaining per-consumer or legacy-version profiles. Derived
  constants reuse the fundamentals. Generated and Timmes reaction-network data
  may remain in their existing authority; never reinterpret table/mass data by
  replacing constants independently of their data contract.
- Splitting is by readable responsibility, following main's policy-specific
  dispatch translation units. Avoid a hand-maintained Cartesian product of
  tiny files. Generated instantiation wrappers contain no mathematical bodies.

## Five ordered work packages

Owner renewal (2026-09-06): continue to the public-release threshold, not another
smoke-only handoff. Keep this as the sole active plan and reread it every 20
minutes of active work, after resumption, at phase changes and before acceptance.
Do not equate a plan/doc update with completing implementation or qualification.

Documentation delivery has two distinct audiences:

- `README*.md`, `docs/Reference*.md`, `docs/CudaBackendStatus*.md`: user-facing
  features, common CPU/CUDA behavior, required dependencies, supported units,
  explicit limitations and reproducible usage. No raw benchmark dumps, local
  scratch paths, experiment chronology or hardware-specific capability claims.
- `docs/development/`: this execution plan, ownership/refactor map and interim
  numerical/build investigations. `validation/` remains the single quantitative
  evidence infrastructure, with curated module summaries and detailed artifacts
  under `results/`; do not create a competing validation tree.
- The final acceptance summary must link to reproducible evidence and distinguish
  numerical correctness, backend parity, sanitizer and capacity. Hardware metadata
  belongs in evidence; genuine minimum hardware/software requirements still
  belong in the user guide. Do not hide limitations to make the public guide tidy.

Next actions: the shared burn-energy, NSE fixed-point, Roe/HLLC and geometry
repairs have passing final-candidate scientific and regression evidence. Core
compilation, scoped capacity measurements, curved runtime coarsening, complete
final-artifact instrumentation and the explicit delivery review are complete.
Final index-930 closes all 41 required technical/source-readiness gates. Preserve
the frozen source and evidence for owner-controlled source release preparation;
do not start another refactor absent a new defect or request. Source selection,
commit/publication and upstream redistribution confirmation are separate actions
that have not been performed.
Audit150/audit200 long/large-workload qualification stays in the owner-deferred
external-machine queue and must not delay the local core profile.

Build safety and the small constants relocation are enabling work for package 1,
not reasons to postpone correctness until after expensive repeated builds.

### 1. Shared correctness and actual feature gaps — COMPLETE FOR THE RELEASE PROFILE

- [x] Resolve Lohner sensitivity using a documented shared field-scale/noise
  policy; test zero fields, closure roundoff, real trace gradients and units.
- [x] Correct shared spherical angular areas and physical CFL lengths after
  tracing hydro, source terms, diffusion, diagnostics, AMR and reflux consumers.
  Independent geometry/equilibrium/convergence evidence is required.
- [x] Add external-gravity CUDA execution using the CPU source-term authority.
- [x] Port generated weak-table ownership/views without copying interpolators;
  regenerate v3 packages as v4 where possible and document compatibility.
- [x] Resolve shared weak-loss Jacobian/acceptance-energy limitations before
  qualifying networks where those losses materially affect the solution.
  Final weak-cv-898 / weak-Helm-899 and independent burn-888 pass; the
  source-energy integral and thermodynamic derivatives have one authority.
- [x] Add central constants/units header and migrate eligible non-network uses.
- [x] Update the implementation-ownership ledger for every moved/new authority.
  The bounded 16:04 UTC review records the inspected shared owners and backend
  adapters, with unused Timmes hand Jacobians and small shared scalar-helper
  duplication kept as explicit cleanup candidates, not numerical forks.

### 2. Generated networks, build structure and evidence tooling — COMPLETE FOR THE RELEASE PROFILE

- [x] Compare main's dispatch split with actual costly CUDA translation units;
  remove accidental repeated instantiation before adding files.
- [x] Review splitting and header dependencies together: no catalogue-wide
  includes in generic mathematical templates, no include cycles, no empty
  forwarding layers without an explicit compatibility/ABI role. Compile direct
  consumers with their own required includes and inspect actual dependency
  records; header guards alone do not prevent unnecessary template expansion.
- [x] Measure representative cold and incremental core builds at safe candidate
  concurrency settings; record wall time, per-process RSS, MemAvailable and swap.
  Choose separate heavy-CUDA/light-C++ job pools from measurements. Do not call
  an unmeasured configuration a sweet spot or promise instant full builds.
  Final 909 records the cold/no-op/single-route and identical-work 1-vs-4-job
  pair, all with the original optimized commands and no guard stop. Use the
  measured heavy-2 / total-4 reference and retain configurable larger-machine
  concurrency; the paired workload does not establish a universal optimum.
- [x] Representative real generated CUDA compilation, RHS/Jacobian/temperature
  and trajectories, CPU KLU / GPU cuDSS production routes (local audit31 plus
  compact weak network). Manufactured matrices alone do not close this gate.
  Final Release-895, generated-875, weak-898/899 and real sparse-900 pass.
- [ ] DEFERRED / external-machine queue: actual audit150/audit200 complete
  compilation, long trajectories and large-workload scaling/capacity. Keep
  reproducible inputs and prior evidence; do not claim these pass locally.
- [x] Exact Dense 31-equation / sparse 32-equation dispatch boundary, all three
  ODEs, residual/factor reuse, singular/EOS failures and no-commit controls.
  Final Release-895 passes the complete configured boundary/provider/error
  controls; sparse-900 separately exercises real generated trajectories.
- [x] Release profile requires Cartesian AND curved AND uniform AND restart
  evidence, plus physics, sanitizer and resources; missing data fail closed.
  Final index-930 passes all required component gates and the shared runtime
  aggregation, without changing original scope or numerical budgets.
- [x] Restart evidence distinguishes process steps from actual source checkpoint
  step/phase; exercise intermediate post-regrid and terminal checkpoint resumes.
- [x] Fingerprint registered external generated math/metadata and actual linked
  cuDSS/KLU dependencies, not only their filesystem paths.

### 3. Freeze artifacts and produce complete validation — TECHNICAL COMPLETE; SOURCE COMMIT NOT PERFORMED

- [ ] Deliberately select/commit release sources and pin external assets. Record
  source, compiler, flags, dependency, binary, comparator and input identities.
  Source/binary/input/dependency identities and recoverable external assets are
  already pinned and checked by final index-930. The remaining part of this item
  is the owner's explicit source-selection/commit action, not another numerical
  campaign. No Git mutation was authorized for this final review. The reviewed
  dirty worktree includes 139 untracked maintained inputs; a HEAD-only archive
  is not the accepted source. Keep the original evidence identities after any
  later commit and establish the new source identity without relabeling reports.
- [x] Freeze and explicitly review the current complete worktree source/assets.
  The two manual gates and artifacts.freeze pass in index-930. This closes
  worktree source-readiness, not packaging, publishing or upstream permission.
- [x] Fresh CPU/CUDA Debug and Release regression; no inheritance from old H100
  binaries or earlier local smoke. Preserve one `validation/` infrastructure.
  Final Release-895 and Debug-910 pass 98/98 each without skips; CPU-only-897
  passes 31/31. Debug guard elapsed 1219.653 s, peak owned RSS 527564 KiB,
  minimum available 6702904 KiB, swap 115792 to 125008 KiB; no guard stop.
- [x] Hydro flux/reconstruction/limiter/time/boundary coverage and convergence.
  Release-895, uniform-874, independent Sedov-889 and temporal-879 pass.
- [x] Species/thermal/viscous diffusion, RKL1/RKL2 and coupled AMR/geometry.
  Cartesian-872 / curved-873, geometry-880 and sustained-901 pass. Complete
  Cartesian 3D lifecycle-919, its memcheck-914 and curved 2D lifecycle-923
  also pass; completed instrumentation is tracked in package 4.
- [x] Ideal/Helm/Tabular3D/Tabular4D EOS domains, derivatives, inversions,
  out-of-range errors and coupled hydro/burn behavior.
  Release-895, EOS applications-891 and physical balances-892 pass.
- [x] Built-in and generated burn networks, BE_NR/BD/ROS4, energy/composition,
  NSE transitions, tolerance/time refinement and long trajectories.
  Burn-878/888, NSE-890, generated-875, weak-898/899, sparse-900 and sustained-901
  pass for the owner-approved local profile; audit150/audit200 stay deferred.
- [x] Cartesian 1D/2D/3D and curved dynamic refine/derefine, selected indicators,
  ghost/reflux, common-mesh fields and physical-volume conservation.
  Original Cartesian/curved matrices and sustained-901 are supplemented by
  complete 3D lifecycle-919 and cylindrical/spherical 2D lifecycle-923.
  Three-dimensional memcheck-914/racecheck-915 also pass; instrumentation is
  recorded separately and is not inferred from ordinary runtime coverage.
- [x] All four restart directions, legacy schema degradation, ENUC/controller/
  output continuity, assets/species mismatches and corrupt-input rejection.
  Release-895 / CPU-only-897 and strict restart-876/877 pass; sustained-901
  adds exact native restoration and independently labelled forward evolution.
- [x] Independent analytic/manufactured/scientific references as well as
  backend parity. Store inputs, commands, metrics CSV/JSON and fixed budgets.
  Final burn-888, Sedov-889, NSE-890, EOS-891/892, gravity-893/894,
  Gaussian-871, temporal-879, geometry-880 and weak-898/899 records pass.

### 4. Sustained reliability and capacity — COMPLETE FOR THE RELEASE PROFILE

- [x] Final-artifact memcheck/racecheck on relevant complete execution paths.
  Focused memcheck-929/racecheck-903 each have 23 complete clean reports;
  dynamic 3D-914/915 each have seven, curved coupled-904/906 each two, and
  native burning restart-905/907 each six CUDA application reports. All original
  physics/restart controls pass. Only focused sparse racecheck uses the approved
  1e-12 observation; ordinary/memcheck retain 1e-10 with unchanged controls.
- [x] Repeated regrid/restart/burn/diffusion, error rollback and leak/lifetime tests.
  Sustained-901, full regression-895, the complete application instrumentation
  above and the scoped allocator-lifetime review-926 pass on the final source.
- [x] 16 GB host-machine envelope with explicitly declared mesh/species sizes;
  separate host RAM, swap and GPU VRAM peaks, including old/new stores, scratch
  and sparse-factor fill-in. No unbounded-size guarantee.
  Final 926 passes regrid with 4/41 species, the 16384-row real cuDSS matrix,
  all-three-ODE audit31 and five AMR application endpoints. Eight selected
  CUDA processes have closed dynamic lifetimes. Profiler allocation requests,
  static module objects, whole-device usage and host/swap telemetry are
  separately retained; exact PID/workload/tool identities are verified.
- [x] Every expensive local build/run uses memory protection. This WSL currently
  exposes about 7.7 GiB RAM and 2 GiB swap, not the host's full memory.
  Required completed campaigns retain their guard records. Earlier timeout,
  monitor and capacity-policy stops remain intact and are not counted as passes.

### 5. Final optimization, documentation and release decision — TECHNICAL COMPLETE; PUBLICATION PENDING

- [x] Measure whole-regrid transfers and time before optimizing remaining plans,
  survivor arenas and factor caches; preserve atomic publication/rollback.
  Final-source 901 and 919 retain whole-transaction measurements, separately
  from nested backend traces and initialization. In 919's 80-step CUDA lane,
  thirteen changed transactions total 12.267906768 s and transfer 534416328 B
  H2D / 5708 B D2H; 67 unchanged transactions total 17.615286900 s and transfer
  818915000 B H2D / 27900 B D2H. This is a measurement gate, not proof that
  metadata setup or communication is no longer a performance cost. Preserve
  the frozen correctness candidate; further metadata/plan caching is a measured
  performance follow-up, with no zero-transfer or universal speedup claim.
- [x] Record chosen build split/concurrency, cold/incremental timing and capacity
  limits. The added requirement makes usable local core compilation a gate;
  `-j6` itself is not the criterion.
  Optimized build-909 and scoped capacity-926 pass under resource protection;
  two heavy/four total jobs is the measured local reference, not a universal
  minimum or optimum. Larger-machine concurrency remains configurable.
- [x] Synchronize English/Chinese capabilities, parameter docs and validation
  status; distinguish passed, implemented-unverified and unsupported.
  Current manual.documentation explicitly reviews 38 documents, with fresh
  identities and independent bounded content/link checks. User entry points
  describe the supported profile and link the single central validation status.
- [ ] Release only when all in-scope gates have final-identity evidence. Any
  reduced scope needs an explicit project-owner decision, not relabeling.
  Technical decision: all 41 required gates pass in final index-930; audit150
  and audit200 retain their explicit owner-approved deferred state. Actual
  publication is not performed. Timmes contact/redistribution confirmation and
  deliberate source commit/package selection remain separate release actions.

## Progress/evidence rules

Update this ledger after each bounded change: authority changed, tests and exact
log path, outstanding gate, next action. A compile or smoke pass is not a physics
qualification. Historical evidence remains historical. Never check a box based
only on code presence. Reopen affected evidence whenever sources/inputs change.

Initial state: real CUDA build and focused AMR/burn/runtime smoke passed in the
previous phase; shared near-zero indicator parity, actual audit200 compile and
formal final-artifact qualification remain open. See [prior smoke](CudaRefactorSmoke.md).

2026-09-06 foundation increment: constant relocation, shared composition-noise
policy, physical metric/CFL/source corrections and external-gravity CUDA wiring
are implemented. Three new CPU contracts pass (`release-foundations-build-01.log`;
minimum MemAvailable 7,020,704 KiB, no swap growth). Final CUDA compilation and
runtime qualification remain pending; `release-foundations-build-02.log` is the
current guarded build. Do not infer completion of package 1 from these unit tests.
Provenance tooling now fingerprints registered external package contents and
configured sparse-library contents; 30 Python contracts pass.

Build-02 was deliberately stopped through its memory guard to measure concurrency;
it is NOT a completed build. Minimum available memory 3,962,552 KiB, no swap
growth. The CUDA curvilinear/source test had linked before the stop and passed
on the actual GPU, including new external-gravity momentum/work checks.

Controlled core-Hydro compile sample (same source/flags, ccache disabled):

| Log under `build/` | Work | Seconds | Peak summed owned RSS (KiB) | Minimum available (KiB) | Swap growth |
|---|---|---:|---:|---:|---:|
| `release-hydro-compile-j1-04.log` | Ideal + Helm Hydro, serial | 47.150 | 776864 | 6389184 | 0 |
| `release-hydro-compile-j2-05.log` | Same two objects, concurrent | 26.096 | 1527064 | 5674244 | 0 |

Objects were moved to `/tmp/arch-compile-benchmark.stE2Vf` before the second
sample, not served from compiler cache. Two-way Hydro compilation is a measured
candidate (1.81x in this sample), NOT yet a full burn/large-network memory
guarantee. The portable heavy-call boundary removes Helm's earlier forced
inlining expansion without a per-policy source-file explosion. Default heavy
pool remains 1; the local experiment uses `ARCH_CUDA_HEAVY_COMPILE_JOBS=2`.
The new generated-network package root is `build/release-network-packages`;
old external packages are preserved and must not be silently relabeled.

Actual audit200 math compilation attempt `release-audit200-math-build-07.log`
was stopped by the memory guard (184.868 s, peak summed owned RSS 5,883,264 KiB,
minimum available 1,238,228 KiB, no swap growth). Aggregate-function call
boundaries alone are insufficient. This is a failed capacity gate, not a
compiler/numerical pass. Next: bound individual generated reaction evaluators
and remove catalogue-wide includes from generic solver headers.

Builtin aprox19 Ideal + Helm, no-cache parallel-2 compile after include cleanup:
`release-burn-compile-j2-11.log`, 212.908 s, peak summed owned RSS 5,001,824 KiB,
minimum available 2,103,996 KiB, zero swap growth. Passed but considerably heavier
than Hydro. Next optimization adds the same shared heavy-call annotation to
the existing Timmes RHS/Jacobian/temperature interface, without selecting the
unused alternative analytical-Jacobian implementation.

With that common Timmes call boundary, the same pair passed in 171.621 s,
peak summed owned RSS 4,276,328 KiB, minimum available 2,876,420 KiB, zero swap
growth (`release-burn-compile-j2-14.log`). Actual Ninja dependency records show
only `NetAprox19.h` plus its support, not other built-in/custom networks.

The audit200 reaction-call-only retry (`release-audit200-math-build-12.log`)
also hit the memory guard (150.857 s, peak 5,837,056 KiB); not a pass. The next
generated variant moves each Jacobian row verbatim into a shared bounded call
inside the SAME header. This preserves statement/write order and creates no
hand-maintained files; unfamiliar upstream layouts fail closed. Fifteen
generator contracts pass, actual compile remains pending.

The evidence gate now has `--profile full-runtime`: it requires all four
canonical Cartesian-AMR, curved-AMR, uniform and generated-network matrices plus both restart
suites with one artifact identity. Missing/duplicate matrices fail closed.
Output still says `release_qualified: false`: independent physics, sanitizer
and capacity gates are not silently replaced by runtime parity. Provenance/
qualification contracts: 32 PASS.

Header/dependency follow-up: architecture audit now checks local include cycles
and transitive catalogue leakage; 84 contracts pass. The now-unused CUDA
network-type-map header was removed (recoverable in Git). Host workspace sizing
uses the real compact matrix ABI for runtime species count, not reaction headers.
The generated audit packages were moved from `build/release-network-packages`
to `/tmp/arch-release-networks.ziRkmT`: the existing external-package scope guard
correctly rejects audit roots elsewhere inside the source tree. Old generator
backups remain under `build/release-network-packages/.backup`.

Enabling KLU exposed a CMake contract-inheritance cycle into fetched SuiteSparse.
Isolating the dependency's directory link property fixes configure/export without
changing ARCH's numerical flags. The current main build enables KLU AND cuDSS,
registers the new audit31 package and uses two guarded compile slots:
`release-core-build-17.log` (IN PROGRESS, not a build pass).

Sparse provenance now inspects the actual final link command (Ninja or Unix
Makefiles) and hashes explicitly linked KLU/AMD/COLAMD/BTF/SuiteSparseConfig and
cuDSS/BLAS artifacts. This is not an OS/driver dynamic-loader attestation.
33 provenance/qualification contracts pass; actual final build observation pending.

Jacobian-row audit200 attempt `release-audit200-math-build-15.log` still reached
the memory guard: 134.859 s, peak owned RSS 5,884,416 KiB, minimum available
1,258,252 KiB, no swap growth. Large-network optimized device compilation remains
an OPEN gate; neither source presence nor the compiler-safe stop is a pass.

Core build-17 was deliberately stopped after 628.371 s, not completed, to remove
forced NSE duplication before compiling the rest. Minimum available memory
2,041,920 KiB, zero swap growth; completed objects retained. Shared NSE projection
and solver entries now use the common heavy-call annotation. Actual aprox19
Ideal/Helm no-cache pair: 98.359 s, peak owned RSS 3,271,612 KiB, minimum available
3,971,444 KiB, no swap growth (`release-nse-boundary-compile-18.log`). Unlike the
earlier pair, this build also enables KLU, so do not present it as an otherwise
identical benchmark. PTX inspection confirms separate shared RHS/Jacobian calls.

The next standalone audit200 attempt uses `--split-compile=2` with only ONE
outer compile and the memory guard. This NVIDIA option partitions device
optimization work by functions; it does not establish a memory bound or replace
measurement ([CUDA 12.3 compiler manual](https://docs.nvidia.com/cuda/archive/12.3.1/cuda-compiler-driver-nvcc/index.html#split-compile-number-split-compile)).
No global `-G`, fast-math or tolerance shortcut was introduced.

Split-compiler audit200 attempt also hit the guard (`release-audit200-split-compiler-19.log`):
120.717 s, peak owned RSS 5,850,624 KiB, minimum available 1,268,112 KiB, no swap
growth. Next diagnostic tries standalone device-debug `-G` / PTX O0 to separate
optimizer capacity from mathematical compile/execution correctness. It is NOT
the production build configuration and cannot qualify optimized compilation.

Device-debug diagnostic was deliberately stopped after 392.940 s while the
compiler was still active (peak owned RSS 885,724 KiB, minimum available
6,208,976 KiB, zero swap growth). It is not a compile pass. A more specific
remaining expansion site was found in `actual_log_screen`: one long screening
calculation was expanded across hundreds of charge pairs. Its shared entry is
now annotated alongside rate/RHS/Jacobian calls; regeneration is in progress.

KLU and its minimal dependencies built successfully without installation or
sudo (`release-foundations-klu-build-21.log`, 8.044 s, peak owned RSS 172,792 KiB).
Four CPU foundation/ODE tests pass with KLU enabled. The separate real KLU
161-equation regression reports max absolute solve error 2.22045e-16 (481 nnz).
This solver fixture is not an actual generated network trajectory.

## Efficient navigation

Header-layer increment: the shared cell burn policy is now separate from CPU
grid iteration. EOS view aliases live in the existing `eos.h` interface; launch
ABIs no longer import concrete EOS bodies. Generated/handwritten instantiation
owners explicitly include their selected EOS. The existing CUDA allocation
class moved to `cuda/common/DeviceAllocation.h`, so sparse mathematical owners
do not import the whole runtime layout merely to allocate arrays. Only two
responsibility-bearing headers were added, not a per-policy file catalogue.
Six separate Host header syntax checks and four CPU foundation/ODE tests pass;
the existing CUDA compile probe now verifies incomplete EOS declarations.
Final core compile remains pending (`release-interface-compile-31.log`).

That focused CUDA probe/thin Ideal+Helm/Ideal Hydro build passed in 38.245 s
(peak owned RSS 792,808 KiB, minimum available 6,448,780 KiB, zero swap growth).
The actual Ninja dependency record for Ideal Hydro contains IdealGas, not Helm
or either Tabular implementation. Root Python tooling contracts: 110 PASS;
architecture contracts: 88 PASS. Full core rebuild is now
`release-core-build-32.log`, still IN PROGRESS, not a release/build pass.

Build-32 failed after 447.699 s on a genuine direct-include omission exposed by
the narrowed ABI: `CudaBackendMicrophysicsControl.cpp` uses `DriverBurn` but had
relied on the old transitive include. It now includes `DriverBurnPolicy.h`
directly; no umbrella implementation was restored. Minimum available memory
3,091,552 KiB, peak owned RSS 4,150,160 KiB, zero swap growth. Completed objects
are reused by `release-core-build-33.log` (IN PROGRESS).

Build-33 completed successfully: ARCH, checkpoint comparator, CUDA indicator,
curvilinear and compile-probe targets linked. Continuation time 1119.689 s,
peak owned RSS 4,455,688 KiB, minimum available 2,784,160 KiB, zero swap growth.
This is an incremental/resumed build, NOT a cold-core benchmark. The three
targeted CUDA tests pass on the RTX 3060 Ti (`release-core-gpu-units-35.log`).
Four CPU foundation/ODE tests pass after expanding metric checks across northern,
equatorial and southern angular cells (`release-geometry-angles-build-34.log`).

Before fresh runtime matrices, shared provenance now records only selected
OpenMP/CUDA execution-control environment variables, not the entire environment
or private tokens; unspecified values remain explicit nulls. 34 provenance
contracts pass. Formal local runs use `OMP_NUM_THREADS=2`, `OMP_DYNAMIC=FALSE`.
Curved AMR runtime: `release-curved-amr-36.log` (IN PROGRESS). Actual audit200
normal-optimized compilation with the screening-factor boundary:
`release-audit200-factor-boundary-37.log` (IN PROGRESS).

Further header evidence: the real aprox19/Ideal sparse object's dependency list
contains only its selected network/EOS plus the shared allocation owner, not
`CudaBackendInternal.h`. Missing explicit relative includes now fail the lexical
audit; generated/SDK names still require the actual compiler. Architecture
contracts: 90 PASS. This does not claim every external header was statically resolved.

Three accepted composition-energy loops were moved verbatim into existing
`OdeMath::integrated_composition_energy`; its independent exact dyadic test passes
(`release-energy-helper-build-26.log`). This is deduplication only: weak-loss
Jacobian and time quadrature remain OPEN.

The screened audit200 attempt reached PTX assembly but was stopped after
1127.088 s without finishing (`release-audit200-screen-boundary-25.log`, peak
owned RSS 3,875,968 KiB, minimum available 3,164,364 KiB, zero swap growth).
The 22 MiB diagnostic PTX is retained as `build/release-audit200-screen-boundary-25.ptx`.
Its two screening aggregate functions each contained about 98,588 PTX lines:
the value-returning `calculate_screen_factor` still expanded transcendental
expressions at every pair. Its body is unchanged but now has the same common
heavy-call boundary. Packages regenerate under `release-network-factor-generation-*-30.log`.
Use `/home/shiroakane/miniconda3/envs/p311/bin/python` for generation; the unrelated
`yt_env` does not contain pynucastro (failed environment probe in log-29).

Weak-table scope correction from direct upstream-code inspection: installed
pynucastro 2.12.0 `BaseCxxNetwork._declare_tables` emits table metadata plus
initialized 1-D axes and Fortran-ordered 3-D arrays. Therefore `num_tables > 0`
is not proof of runtime file loading. The current portable adapter rejects ALL
nonzero-table packages, including these immutable generated tables. The open
gate is table storage/views and shared weak-energy consistency, not merely a
file loader. Do not claim that immutable weak-table packages already work on
CUDA or add a second interpolation implementation to port them.

### Navigation practice

Latest runtime increment: curved matrix log-36 passed all four cases and
Cartesian log-38 passed all ten cases with the original budgets. Curved worst
normalized field error was 0.0957756 (budget 1). These reports under
`build/release-validation-20260906` predate the following comparator changes;
retain them as working-stage evidence, not final-identity reports. Concurrent
audit200 assembly and runtime increased system swap by 8,796 KiB; parity passed
but this is not a zero-swap capacity qualification.

Restart log-39 exposed a validation filename assumption on CPU-to-CPU terminal
resume: the correct step-4 file was index 3, not the hardcoded index 2. The runner
now selects using actual step/phase metadata. Terminal source CHK/PLT counters
must increase by exactly one; the comparator reads both actual source files and
checks the corresponding final counter difference. Ordinary comparisons still
require equal counters; no physics, topology or controller budget was relaxed.
Comparator builds log-40 and log-41 pass (6.037 and 6.049 s). Root tooling 114
and architecture 90 tests pass. Fresh smooth restart log-42 passes all eight
routes (7.029 s, peak owned RSS 133,820 KiB, no swap growth).
Current-identity reports go into `build/release-validation-current-20260906`;
ENUC restart log-43 and the remaining fresh matrices are still pending.

ENUC restart log-43 subsequently passed all eight routes: 292.874 s, peak owned
RSS 306,920 KiB, zero swap growth. Fresh runtime log-46 passed Cartesian/curved
again but failed the uniform BD controller check described at the top. Its
296.930 s run observed 256 KiB additional system swap while audit200 assembly
was also active. This batch did not publish a uniform success report. Direct
HDF analysis found exact equality of energy/ENUC and a maximum normalized
rhoX error 0.0285168 against the original field budget. Both stored limiter
minima are smaller than the last-half-step candidate reconstructed from final
ENUC: inspect the earlier Strang half-step, not just the final field. Numerical
cancellation is a candidate explanation, not a completed root-cause proof.
No input, comparator budget, controller value or reference was altered to pass.

Actual audit200 log-37 completed: normal Debug PTX O1, strict floating point,
1618.419 s, peak owned RSS 3,194,496 KiB, minimum available 3,676,976 KiB,
system swap increase 9,052 KiB during concurrent validation. GPU math log-48:
`GENERATED_NETWORK_MATH_PASS neq=201 nnz=3625`. This tests actual generated
RHS/Jacobian/temperature derivatives, not a manufactured matrix or ODE trajectory.
Binary SHA-256 `4bb625f9d2961a21b6110ed7f06e1b5171144c9f2b3a5648cfa09394abbe2bb2`;
audit200 manifest SHA-256 `509b7741683c9455e2244805962e3a4cbe53b3bead24790fbfc1354c577cf62a`.
The original package remains in `/tmp/arch-release-networks.ziRkmT/audit200`.

Actual cuDSS provider and registered-network sparse factory tests pass again
in log-49 (1.29 s CTest total). The factory covers builtin Iso7 and the ONE
registered custom audit31 network, all three ODEs with Ideal EOS. It does not
claim audit150/audit200 trajectories or all EOS combinations. Builds logs-44/45
passed, 2.018/6.032 s respectively.

Further dependency split: shared diffusion records/mapping moved verbatim to
`DiffusionTypes.h`; the empty `DiffusionConfigViewAdapter.h` and its warning/
audit exemption were removed. Raw device-view headers no longer pull Host Grid
through a field-binding helper or unused complete AMR flux plan. ABI pointer
types are forward-declared; genuine plan owners include complete definitions.
CUDA compile probe log-53 passes with Grid and all four EOS types incomplete.
The strengthened probe correctly failed logs-51/52 until those remaining
transitive imports were removed. Root tooling 114 and architecture 92 contracts
pass before the subsequent RHS-generator increment.

Same direct Host configuration/diffusion-ABI syntax check: GNU time records
CPU user time 2.45 -> 0.62 s and maximum RSS 395,564 -> 124,504 KiB in logs-47/55.
Concurrent load differed; this is a header-cost diagnostic, NOT a cold-core
speedup claim. Very short runs can finish between guard samples, so their
sampled RSS is not a peak-memory bound. Full core rebuild log-54 uses the same
two-way heavy pool under the memory guard and is still pending.

Next generated variant splits RHS by isotope balance, alongside Jacobian rows,
in the SAME numerical header; no formula/statement order changes or extra
generated source-file catalogue. Sixteen generator contracts pass. The new
150/200 packages are generated separately at
`/tmp/arch-release-network-rows.sFCD11` (log-56), preserving the passed package
and binary. Their compile/runtime results remain pending and cannot inherit
log-48's pass.

Header build-54 stopped on an incomplete AMR plan record in the actual flux
kernel (468.812 s, minimum available 2,787,232 KiB, peak owned RSS 4,151,200 KiB).
The kernel now explicitly includes its consumed shared plan definition; no
complete plan was restored to the pointer-only launch ABI. Build-57 resumes
the remaining objects. Root tooling 115 and architecture 92 contracts pass.

Row-split generation log-56 passed for both new packages (145.499 s, peak owned
RSS 524,484 KiB; concurrent core compilation means this is not isolated capacity).
The existing Host numeric-dump test was compiled against before/after packages
under strict O0 flags in log-58 (54.243 s, peak owned RSS 830,368 KiB, no additional
swap). Actual finite numeric output is bitwise identical at both existing test
temperatures: audit150 46,506 values, audit200 82,006 values, covering RHS, energy,
Jacobian and temperature derivatives. Output SHA-256 values respectively:
`09110a506443b613fd2f3de5dc15f8418833749391fd750f32e94b10e4fb1b07` and
`e3fcafc2f317865f247ebfba9496c8f45875ba18b8d499ca7a4fbc2b30972180`.
Executables are retained under `/tmp/arch-row-reference.dOSWIC`; row-split CUDA
compilation/runtime remains pending. This is a source-transformation regression,
not a new independent scientific reference.

Header core build-57 PASS: 1058.148 s for the remaining 64 edges, peak owned
RSS 4,455,516 KiB, minimum available 2,841,960 KiB, system swap growth 768 KiB.
Together with the earlier stopped/failed increment this is not a cold-build
benchmark. Current ARCH SHA-256:
`662a775cb7ca73f1e4295417dc8ba14914d9402a605ef865f97e8cab97fc052a`.
Nine focused CPU/GPU tests passed in log-60 (2.14 s CTest total), including real
cuDSS and registered audit31 sparse factory coverage. The actual probe dependency
record contains `DiffusionTypes.h` but none of Grid, concrete EOS, DiffFlux,
TimeIntegratorHelper or complete AMR flux execution-plan headers.

Fresh header-build runtime log-62 PASS: Cartesian 10 cases, curved 4 cases,
smooth and ENUC eight-route restart suites. Reports:
`build/release-validation-headers-20260906`. Elapsed 380.143 s, peak owned RSS
674,296 KiB, minimum available 4,797,896 KiB, no swap growth while audit150
compiled concurrently. Focused actual-device refinement-indicator memcheck
log-63 PASS: zero errors and zero leaked bytes. This confirms that sanitizer
attachment works here; it is not a complete application/sanitizer qualification.

The new row-split audit150 compiled in log-61 with normal Debug PTX O1 and
strict floating point: 790.706 s, peak owned RSS 2,175,360 KiB, minimum available
4,792,920 KiB, no swap growth. The reused temporary diagnostic target has a legacy
audit200 basename; its cache explicitly selects `ARCH_AUDIT_NETWORK_ID=audit150`
and `/tmp/arch-release-network-rows.sFCD11/audit150`. Do not infer species count
from that executable's filename. Configurations for 150/200 passed in log-59;
neither configuration nor a finished build establishes a numerical pass.

Actual audit150 math runtime log-66 FAILED. Read-only GDB log-67 identifies only
the scalar `denuc_dT` mismatch: CPU `8.1183645459822051e19`, GPU
`8.1183645437069492e19`, about 2.8026e-10 relative against the unchanged 2e-10
budget. All preceding CSR, RHS, composition-energy derivative and RHS-temperature
derivative array comparisons passed. The adapter subtracts two finite-difference
energy evaluations; cancellation is under investigation, not a proven excuse.
Do not enable upstream rate derivatives blindly: this generated SimpleCxx
screening body does not populate `dlog_screen_dT`, despite a rate-derivative
record existing. Preserve full screening dependence and one numerical authority.
No derivative formula or tolerance has been changed for this investigation.

Uniform follow-up log-64 completed (197.685 s, peak owned RSS 303,376 KiB,
minimum available 4,991,248 KiB, no swap growth). Periodic advection passed:
1016 accepted steps to time 2.05, four root blocks, bitwise CPU/GPU physical
fields, analytic L1/L2/Linf errors 5.07085e-6 / 5.63171e-6 / 7.95472e-6.
The subsequent canonical BD step-10 comparison FAILED again on the current
binary. Neither canonical input nor budget was changed. The partial report
cannot qualify the full uniform matrix.

The dependency audit now also rejects missing quoted headers under existing
project source namespaces, not only explicit `../` paths. Unrelated SDK prefixes
and bare generated headers remain compiler-owned. Root tooling 116 and
architecture 94 tests pass, plus the actual source audit and `git diff --check`.
Generated adapter setup now imports `GlobalDefs.h` directly instead of
`RuntimeParams.h`, for both CPU-only and portable packages. A separate regenerated
audit31 package lives at `/tmp/arch-release-network-includes.XeOiKJ/audit31`;
generation log-68 and Host build/parity logs-69/70 PASS. All 2238 finite numeric
values at the two existing test temperatures are bitwise identical to the old
audit31 variant (including the row split); output SHA-256
`7d9e4cb2a4a88791523c855549c24c0615127f09ae3622585afe97d9456da3c2`.
Its actual Host dependency record includes `GlobalDefs.h`, not `RuntimeParams.h`.
Standalone CUDA configuration/build/runtime logs-71/72/73 PASS:
`GENERATED_NETWORK_MATH_PASS neq=32 nnz=486`. Build elapsed 27.126 s, peak owned
RSS 351,232 KiB, minimum available 3,869,704 KiB, no swap growth while audit200
was compiling. Actual NVCC dependencies also include `GlobalDefs.h`, not
`RuntimeParams.h`. This separate package is not installed into the core registry
and does not inherit its passes. Both reference manuals now distinguish shared
external-gravity source coverage from pending production qualification, and
correctly describe all nonzero weak-table packages as CPU-only.

Additional actual-program external-gravity check: log-74 stopped after RK2
parity because the runner read `time` from an absent optional qualification
record. No solver failure or tolerance relaxation occurred. Terminal-time/step
validation now always uses the existing parameter-bound checkpoint metadata
reader, records the actual rendered parameter identity, and rejects nonfinite
time or parameter drift. Root tooling 117 PASS, including negative controls.
Fresh log-75 PASS for RK2/RK3 to time 0.1, unchanged canonical gravity inputs,
OMP_NUM_THREADS=2: 6.026 s, peak owned RSS 220,768 KiB, minimum available
4,050,084 KiB, no swap growth. Reports are under
`build/release-validation-headers-20260906/gravity-metadata`; failed log-74
artifacts are preserved separately under `gravity`.

Independent uniform-acceleration check log-76 PASS, using the already documented
1e-12 Linf limit for density, all velocity components, pressure and total energy.
RK2 energy/pressure errors are 4.441e-16 / 2.220e-16; RK3 8.882e-16 / 4.441e-16;
density error is zero and maximum velocity error 8.327e-17. Both backends match,
and each integrator produces byte-identical CPU/CUDA checkpoints. The one-off
diagnostic `/tmp/arch-gravity-parity.fJQwDf/check_exact.py` uses the common
parameter reader and an independent constant-acceleration law, not production
source helpers. SHA-256 `ed1400b5ac3134057ebefffec91b69003f4bce91bd836812be7eb8a56f504fd8`;
input/checkpoint/report identities are retained in log-76. This is real program
evidence for this uniform problem, not final profile/curved/AMR/restart/long-run
qualification. The historical gravity metrics table is retained unchanged.

RHS-row experiment decision: actual audit200 compile log-65 PASS, 1731.497 s,
peak owned RSS 3,219,456 KiB, minimum available 3,762,304 KiB, no swap growth.
GPU runtime log-77 PASS, `neq=201 nnz=3625`. Against the earlier unsplit-RHS
1618.419 s / 3,194,496 KiB observation, this is not evidence of improved compile
time or memory; concurrent loads differ, so do not infer a precise regression
either. Remove the extra RHS-row transform and its wrapper machinery rather
than retain an unqualified tuning knob. Jacobian-row, major-stage boundaries,
bounded template depth and the header-dependency fixes are retained.
The experimental packages/binaries/logs remain intact, including the audit150
failure. This withdrawal does NOT close its derivative investigation or change
any numerical tolerance. The final generator was rechecked separately on audit31
under `/tmp/arch-network-header-final.384QIT`; no running compiler input or
registered package was modified during an attempt. Logs-78/79/80/82 PASS:
generation, standalone configuration, Host/CUDA build, GPU math (`neq=32 nnz=486`)
and bitwise Host comparison against the preserved original. The output SHA-256
remains `7d9e4cb2a4a88791523c855549c24c0615127f09ae3622585afe97d9456da3c2`.
Combined Host/CUDA build guard elapsed 24.119 s, peak owned RSS 355,840 KiB,
minimum available 6,785,632 KiB, no swap growth during concurrent periodic runtime.
Actual final NVCC dependencies include `GlobalDefs.h`, not `RuntimeParams.h`.

The metadata-reader fix also passed the canonical 1016-step periodic-advection
scientific case again (log-81, 11.071 s, no swap growth), with the actual checkpoint
time/step and rendered parameter identities recorded. Its report is under
`build/release-validation-headers-20260906/periodic-metadata`. All final local
build/test commands are complete; no background compiler is being left running.
The current generator/tooling contracts are 117 root tests and 94 architecture
tests, with source audit and diff whitespace checks passing. Release gates listed
at the top remain open; none of these focused checks is a full parity release.

Read this file and [ImplementationOwnership.md](ImplementationOwnership.md)
first, then only the relevant authority/consumer/test files. Use scoped `rg`
queries and build dependency logs; do not repeatedly dump the entire tree.
The available generic skills do not supply ARCH ownership knowledge. A future
small repository skill may point to these documents rather than duplicate them;
progressive disclosure is supported by the
[official skill documentation](https://learn.chatgpt.com/docs/build-skills).
