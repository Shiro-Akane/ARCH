# Maintenance review — 2026-09-08 JST

Chinese summary: [README.zh-CN.md](README.zh-CN.md).

This contributor record covers source organization, interface cleanup and their
verification. The governing scope is the
[release standard](../../../../docs/development/CudaReleaseStandard.md).
The scientific acceptance measurements retain their own tested-source identities.

Status: the optimized CUDA build, all 98 configured Release tests, ten CPU/CUDA
development-smoke lanes, both normal restart suites and burning-restart
memcheck/racecheck pass. The 282 Python controls, architecture audit and focused
interface checks also pass. The
existing [identity and verification record](freeze-review.json) is the single
inventory for this maintenance work; it distinguishes new checks from the
scientific and source-organization results cited below.

The completed runtime campaign verifies unchanged source, application,
comparator and test-artifact identities before and after execution. Its source
fingerprint is `02f52db849e5212642576e5b945d594bf96b6b692292e5c21c2a7b407979b781`.

## Current interface cleanup

The shared reader and writer enforce the ARCH checkpoint contract. Scientific
identity, controller state, ENUC and native composition for active species are
required; missing state is rejected rather than reconstructed. The complete
checkpoint payload and its field/identity checks remain intact. A fresh simulation
still initializes its own `RunState`.

Burn, diffusion and gravity factories now consume the policy IDs selected by the
common resolver. Redundant config/string overloads and unused solver-selection
members have been removed. The retained factories, physics and mathematics are
unchanged. Parameter aliases, both case-registration interfaces, EOS setup/data
representations and generated-package CPU/CUDA eligibility remain supported.
The [ownership index](../../../../docs/development/ImplementationOwnership.md#current-interface-and-compatibility-review)
records these boundaries without introducing another audit document.

The passing 98-test Release inventory includes `checkpoint_compatibility`,
`resolved_execution_plan`, `shared_stage_scheduler`, `boundary_plan`,
`amr_operation_plans` and `state_residency`. They cover complete ARCH state restore,
invalid-input rejection, shared policy selection, stage/AMR planning and state
ownership. The source review separately checks the allowed interface changes
and layout/comment equivalence; the runtime checks exercise the resulting build.

The unified Python run passes all 282 tooling and architecture controls in
9.140 seconds with zero skips; the standalone architecture audit also passes.
Four fixed provenance protocol inputs have moved to
[test fixtures](../../../../tests/fixtures/validation_provenance/README.md)
with their bytes preserved. Their recorded H100 identities serve parser and
rejection controls, not scientific acceptance of the current implementation.

## Current build and runtime checks

The optimized CUDA application and all configured test targets build with the
representative generated networks and real KLU/cuDSS providers. The layout
review accounts for 37 test-file moves across 77 adjusted files and checks that
260 production files remain unchanged apart from the exactly recorded comments.
The 280 existing normalized
compiler commands are unchanged; the checkpoint integration test additionally
links the shared `ChkIO.cpp` implementation. These checks do not equate the
intentional checkpoint/factory interface changes with comment-only edits.

The build reused completed objects while concurrency followed available RAM:

| Phase | Heavy / total jobs | Elapsed | Outcome |
| --- | --- | --- | --- |
| Initial build | 2 / 4 | 209.871 s | Guard stopped at the available-memory reserve |
| Reduced-concurrency continuation | 1 / 2 | 3,033.388 s | Deliberately interrupted after system memory became available |
| Adaptive continuation | 2 / 4 | 1,852.722 s | All configured targets built successfully |

The successful continuation peaked at 3,756,708 KiB owned RSS, retained at least
2,652,536 KiB available RAM, and observed 512 KiB of swap growth without a guard
stop. A final 1.019-second build check reported no work to do. Neither interrupted
phase was a compiler correctness failure. These are incremental maintenance
observations, not a replacement for the separate 2,213.445-second
[cold-core measurement](../cold-core-first-law-20260907/release-909/README.md)
or a new minimum hardware requirement.

On this build, all 98 Release tests pass without failures or skips and all ten
development-smoke lanes pass. Smooth and burning restart each complete twelve
application executions and nine strict comparisons, covering all four backend
directions with native fields, ENUC, controller and output-phase checks.
Burning-restart memcheck and racecheck each repeat all twelve executions and
nine comparisons, including six actual instrumented CUDA processes per tool.
Every sanitizer report is complete and clean. The original runners and budgets
are retained; final source and artifact identity checks pass.

The guarded runtime campaign completes in 503.512 seconds, with 574,420 KiB peak
owned RSS, at least 5,868,904 KiB available RAM and no swap growth or guard stop.
This instrumented regression time is not a simulation-performance benchmark.

The [execution recipe](run_maintenance_checks.py) reuses the existing CTest,
smoke, restart and provenance facilities. Its 60 structural positive/negative
controls pass; these controls test the recipe, not the physical trajectories.
Source-checkout reproduction starts with the [test guide](../../../../tests/README.md).

## Evidence continuity

The original scientific acceptance belongs to source
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`.
Its [acceptance index](../final-acceptance-20260907/release-73a9cf50/index-final-930/README.md)
records 41 required passes and two owner-deferred large-network workloads.
Those measurements and their budgets remain the scientific evidence; this
maintenance work does not relabel them as executions of the current source.

[baseline.json](baseline.json) identifies the original source inventory and
recoverable archive. [document-baseline.json](document-baseline.json) preserves
the identities and archived versions of the previously reviewed documentation.
The JSON records identify the source/document archives. Numerical measurements
and failed-attempt identities are not rewritten when user navigation is simplified.

The [preservation review](preservation-review.json) verifies the original four
critical acceptance artifacts and all members of the source/document archives.
The [navigation review](document-navigation-review.json) checks 138 Markdown
files, 1,318 local links, 59 heading anchors and README coverage for 99 functional
directories at its recorded document identity. External websites were not
fetched. These counts describe that recorded review, not a fresh scan after
every subsequent documentation edit.

## Referenced source-organization checks

The path-organization checks tested source
`4f4220712d6f76386bc126a0ba32b0d09f45e058b9e1c5f02fc25c7935dc9282`.
Their [44-path map](runtime-path-map.json) and
[independent comparison](relocation-review.json) account for 452 source inputs.
After resolving moved includes, bodies and literals were unchanged; two ODE
blocks received indentation cleanup and two tabular-EOS comments were corrected.
Architecture checks and 95 associated regression controls passed.

The comparison preserved 280 normalized compiler commands, 98 CTest
registrations, linker settings and the heavy compilation pool. It did not assert
byte-identical Ninja metadata. The [comparison recipe](review_relocation.py)
contains 19 positive and negative controls and uses the existing identity helpers.

The effective [build record](build-review.json) and
[runtime record](runtime-review.json) identify the current executions and their
before/after checks. Referenced source-organization
measurements keep their recorded identities; they are not the evidence for the
interface changes described above.

User guides, source navigation and validation summaries now lead with maintained
behavior and the effective acceptance data. Publication actions and the owner's
pending third-party redistribution confirmation remain separate decisions.
