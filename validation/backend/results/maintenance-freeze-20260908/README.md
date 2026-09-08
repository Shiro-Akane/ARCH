# Maintenance review — 2026-09-08 JST

Chinese summary: [README.zh-CN.md](README.zh-CN.md).

This contributor record covers source organization, interface cleanup and their
verification. The governing scope is the
[release standard](../../../../docs/development/CudaReleaseStandard.md).
The scientific acceptance measurements retain their own tested-source identities.

Status: the latest full runtime campaign's optimized CUDA build, all 289 Python controls, 98 Release
tests, ten development-smoke lanes and all four normal/instrumented restart
suites pass. The matched local AMR timing also passes its consistency,
conservation, dynamic-topology and identity checks; CPU is faster on both
measured sizes. This is a functionally verified source selection for the
owner-authorized integration, not a claim of GPU speedup. The
existing [identity and verification record](freeze-review.json) is the single
inventory for this maintenance work; it distinguishes new checks from the
scientific and source-organization results cited below.

That runtime campaign tests source
`abd02d1eab0887e366ebf24506882efb1a49166e2607ff47ba1a89937107cedf`.
Its own execution and final source, binary and test-artifact checks pass; the
results are not inherited from an earlier maintenance build.

## Build-module organization

The subsequent CMake-only cleanup is recorded in [cmake-review.json](cmake-review.json).
The top-level file now describes build order; seven functional modules own
options, application targets, generated networks, CUDA, dependencies and tests.
Shared helpers and loops remove repeated settings while keeping special source
languages, object ownership and provider checks explicit. KLU and cuDSS discovery
and the numerical optimization flags remain unchanged.

The configured CUDA graph matches the preceding build: 281 compile commands,
698 effective build commands, 758 dependency edges, 98 CTest definitions,
226 rule/global/pool declarations and 44 generated-file contents agree. CMake
declaration locations and regeneration metadata are the only excluded fields.
The record also contains the small host rebuild and focused checks; it does not
claim another full NVCC build or another execution of the full runtime campaign.
Mathematical, physical, simulation, test and tooling source bytes are unchanged.

Two small acceptance-review self-test inputs are included in source delivery:
[allocation lifetimes](../device-memory-first-law-20260907/test_allocation_lifetimes.py)
and [process identity](../device-memory-first-law-20260907/test_process_identity.py).
They support reviewer checks, not another scientific trajectory. The original
scientific and runtime records below retain their actual execution identities.

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

The hydro launcher now requires its resolved plan, requirements, backend,
startup state and checkpoint identity by reference. The unused config-parsing
selector chain has been removed; the registry mappings and active route visitor
are unchanged. The two-dimensional curved-grid startup display names its angle
`phi`, matching the shared grid convention. No numerical or physical formula changes.

The configured Release inventory includes `checkpoint_compatibility`,
`resolved_execution_plan`, `shared_stage_scheduler`, `boundary_plan`,
`amr_operation_plans` and `state_residency`. They cover complete ARCH state restore,
invalid-input rejection, shared policy selection, stage/AMR planning and state
ownership. The source review separately checks the allowed interface changes
and layout/comment equivalence; the runtime checks exercise the resulting build.

The unified Python run passes all 289 tooling and architecture controls in
29.336 seconds with zero skips, including seven configure-helper controls.
Four fixed provenance protocol inputs have moved to
[test fixtures](../../../../tests/fixtures/validation_provenance/README.md)
with their bytes preserved. Their recorded H100 identities serve parser and
rejection controls, not scientific acceptance of the current implementation.

## Current build and runtime checks

The optimized CUDA application and all configured test targets build with the
representative generated networks and real KLU/cuDSS providers. This cleanup
changes two production files: the dispatch entry boundary and one axis-label
literal. Mathematical, physical, AMR, grid and CUDA implementation files remain
unchanged. All 281 normalized compiler commands retain their previous arguments;
the configure-helper and test updates do not change optimization flags.

Configuration completes in 8.041 seconds. The incremental build completes in
59.005 seconds, with 2,294,800 KiB peak owned RSS, at least 4,867,264 KiB available
RAM and no swap growth or guard stop. A final 1.013-second build check reports
no work to do. These are incremental maintenance observations, not a
replacement for the separate 2,213.445-second
[cold-core measurement](../cold-core-first-law-20260907/release-909/README.md)
or a new minimum hardware requirement.

The current runtime campaign passes all 98 Release tests with zero skips and
all ten development-smoke lanes. Normal smooth restart, normal burning restart,
burning-restart memcheck and burning-restart racecheck each pass twelve
application runs and nine strict comparisons. Each instrumented suite includes
six actual CUDA processes with complete clean reports. All backend directions,
native fields, ENUC, controller/output phase and source/artifact identities
retain the original runners and budgets.

The guarded runtime recipe completes in 551.022 seconds, with 576,376 KiB peak
owned RSS and at least 4,849,544 KiB available RAM. Swap grows slightly from
250,288 to 253,632 KiB; the guard does not stop the run and pressure observations
are complete. These campaign timings include instrumentation and are not a
CPU/CUDA speed comparison.

The [execution recipe](run_maintenance_checks.py) reuses the existing CTest,
smoke, restart and provenance facilities. Its structural positive/negative
controls test the recipe, not the physical trajectories.
Source-checkout reproduction starts with the [test guide](../../../../tests/README.md).

## Matched local AMR timing

The [timing summary](amr-timing-review.json) and complete
[small](sedov-timing-small.json) / [medium](sedov-timing-medium.json) reports
compare the same optimized executable on CPU and CUDA. The host is WSL2 on an
i7-10700 with 16 GB installed RAM and an RTX 3060 Ti with 8 GB VRAM; the WSL
guest had about 7.7 GiB RAM assigned. Both backend processes use the same
eight-thread OpenMP environment. Each scale has one warmup and three measured
runs per backend, executed serially.

Both sizes use two refinement levels and the same two-dimensional Sedov
physics, HLLC/PPM/RK3 methods, thresholds and final physical time `0.02`.
That observation window includes actual runtime refinement and coarsening,
rather than only the initial mesh setup. Warmups and cost pilots are excluded
from the following medians.

| Base blocks | Accepted steps | Initial → final leaves | CPU end-to-end | CUDA end-to-end | CUDA / CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 × 4 | 354 | 40 → 88 | 12.011 s | 39.670 s | 3.30 |
| 8 × 8 | 711 | 88 → 208 | 54.186 s | 185.632 s | 3.43 |

CUDA takes 3.30 and 3.43 times the CPU elapsed time on these workloads; neither
size demonstrates acceleration. CPU/CUDA fields, conservation, accepted steps,
runtime topology sequences and source/recipe identities pass the existing
checks. The field budgets remain `rtol=1e-8`, `atol=1e-11`; conservation retains
`rtol=2e-12`, `atol=2e-10`. The small case has three runtime topology changes
(two net leaf-count increases and one decrease); the medium case has twelve
(eight increases and four decreases). These event labels count net changes,
not individual parent/child operations within a transaction.

End-to-end time runs from ARCH process creation to exit, including setup,
computation, AMR, endpoint output and shutdown; validation is outside the timer.
The separately recorded runtime regrid-transaction totals have medians of
1.404 s CPU / 2.279 s CUDA for the small case and 6.664 s / 10.890 s for the
medium case. Initialization is separate, and overlapping nested traces are not
added. There is no separate solver or kernel timer: subtracting regrid from
end-to-end time does not measure hydrodynamics alone. Both guarded campaigns
finish without a guard stop or swap growth.

Reproduce with `BUILD_TESTING=ON` and the application and checkpoint comparator
from the same CUDA-enabled build. Run from the repository root and choose an
absent or empty output directory:

```bash
python3 tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  python3 validation/backend/results/maintenance-freeze-20260908/run_sedov_amr_timing.py \
    --arch build-cuda/bin/ARCH \
    --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
    --output-root build/sedov-amr-timing --blocks 4 8 --levels 2 \
    --time .02 --threads 8 --backend cpu cuda --warmups 1 --repeats 3 \
    --timeout 1200
```

The owner accepts integration as a functionally equivalent CPU/CUDA version
with these performance results disclosed. Runtime performance optimization is
separate follow-up work; no production implementation or scientific budget was
changed to obtain these measurements.

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
[runtime record](runtime-review.json) record the current campaign and its
before/after identity checks. Referenced source-organization
measurements keep their recorded identities; they are not the evidence for the
interface changes described above.

User guides, source navigation and validation summaries now lead with maintained
behavior and the effective acceptance data. Publication actions and the owner's
pending third-party redistribution confirmation remain separate decisions.
