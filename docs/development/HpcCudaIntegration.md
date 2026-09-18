# HPC-CUDA integration checklist

This is the working acceptance record for preparing the CUDA optimization for
integration into main. Update this file in place. Re-read it before starting a
new work phase and before reporting completion.

## Scope and source identities

- Optimization source: `3dab4e25f9877b6eb2112bbf59a8264618e8862a`.
- Main baseline: `ae83d4d6575a4db79d51c5ed376a4e7e4698fb3a`.
- Work branch: `codex/hpc-cuda-integration`; the existing main checkout is untouched.
- Persistent workspace: `/home/shiroakane/ARCH-hpc-cuda-integration`. The original
  `/tmp/arch-hpc-cuda-integration` alias preserves configured build and evidence
  paths; this is workspace relocation, not a software compatibility layer.
- Preserve shared mathematics, physical models, stage order and numerical budgets.
- Defer further 150/200-species performance experiments. Keep the qualified
  production sparse path; experimental providers are not production features.
- Publication target: `origin/codex/hpc-cuda-optimization`. The owner has
  authorized committing the prepared work and pushing to this original HPC
  branch. Main/frozen-branch updates, PR merge, dependency upgrades and
  destruction of original evidence remain outside this handoff.
- Trust the collaborator's documented results at their stated source and
  binary identities. Do not repeat the 1,188-run matrix, large-network builds
  or archived trajectories locally. Local checks cover new edits and specific
  integration gaps; revisit historical evidence only if a concrete inconsistency
  makes it unreliable.

## Acceptance sequence

1. Preserve the original evidence and inventory before moving material out of
   the user-facing tree. Retain concise results, reproducible inputs, validators
   and provenance; separate historical server operations and experimental code.
2. Review changed implementation bodies, shared ownership, include boundaries
   and file placement. Prefer bounded functional changes over a broad refactor.
3. Match implementation comments to `src/main.cpp`: explain purpose, inputs,
   ownership, failure/completion rules and equations where they clarify a method.
4. Reconcile main's documentation changes. Review maintained Markdown links and
   separate user instructions, developer contracts and validation reports.
5. Retain the active ENUC-limiter coupled AMR/restart regression. For release
   preparation, remove the optional predictive-AMR recorder and compare ordinary
   CPU/CUDA AMR with the preserved recorder-disabled executable.
6. Run bounded builds and targeted tests with RAM, swap and I/O monitoring.
   Small stable swap use is acceptable; sustained resource pressure is not.
7. Record exact passed, failed and unavailable checks. Do not transfer historical
   GPU/sanitizer qualification to an untested binary or call a blocked test passed.

## Release preparation for v1.1.0

The owner has confirmed that the collaborator backed up the optional recorder
and authorized removing it, including its directory, before main integration.
The feature exported patch/graph training data; it neither executed GNN
inference nor owned production refinement decisions.

The removal covers `src/runtime/predictive_amr`, the now-empty parent directory,
four dedicated parameters, two diagnostic `Block` fields, their rollback
snapshots, shared Driver calls, dedicated tests/validator and user-facing links.
No standard `.par` input depends on those parameters. `src/cuda/runtime`, the
real CUDA refinement-indicator evaluator, shared Lohner criteria, 2:1 balancing,
conservative migration and field-version checks remain. Generic pre-apply,
allocation and device-evaluation callbacks predate the recorder and are retained;
transaction-failure tests still exercise their allocation/rollback behavior.

| Release-preparation check | Status |
| --- | --- |
| Source dependency review | Complete; no dedicated recorder symbol remains in maintained source, configuration or tests |
| Architecture and tooling | Architecture audit passed; 350 tooling tests passed without skips |
| Affected build and focused contracts | Core/selected targets rebuilt with two total jobs and one heavy CUDA job; seven focused Host/CUDA CTests passed without skips |
| AMR baseline comparison and checkpoint continuation | 24 runs and 12 exact dataset comparisons passed across one, two and three dimensions on CPU/CUDA; six complete-run regrid event/counter comparisons matched; six baseline-checkpoint continuations passed |
| Active ENUC coupled restart | Existing BD/RKL2 all-transport owner passed 12 lanes and nine comparisons; ENUC binding witnessed on every lane |
| Focused AMR instrumentation | All four composition/regrid-transaction memcheck/racecheck runs passed with complete, clean reports |
| Release notes and links | English/Chinese v1.1.0 notes prepared; 155 maintained Markdown pages have no missing local targets |

The preserved baseline executable is copied under the ignored
`build-integration/v110-removal-baseline/` directory. Its SHA-256 is
`bd8384adc30495ce9dc9a824063220b0c336f662eab19c8aa42dafbddce83951`,
the accepted integration executable described below. The rebuilt candidate is
`7ecedae4eb468ca53c9fe3d8ff4d846a92cde9636fe22652e3bb0108de8b8944`;
the staged production/input fingerprint is
`3c353e8fc7495b4115c46ef851bdf8a3a2fabff923d658810ed939b282b7f2af`.
Source, build and executable identities remained unchanged across the AMR
comparison, coupled restart and instrumentation checks. The
[current compact record](../../validation/backend/results/hpc-cuda-optimization/integration.json)
is updated in place and links its source-pinned predecessor. This preparation
does not repeat the original performance campaign or add large-network builds.

The guarded rebuild took 4,636.522 seconds. Minimum Linux available memory was
2,147,292 KiB and sampled peak owned RSS was 3,875,004 KiB; swap remained unused.
Peak sampled memory full-stall pressure was zero and I/O full-stall pressure
was 15.718%, without a guard stop. A final incremental check rebuilt one Host
configuration-test target before the seven CTests passed. Ordinary AMR comparison
used 137.698 seconds across all 24 runs; this is regression duration, not a
performance comparison. Temporary HDF5, binaries and logs remain ignored under
`build-integration/v110-*`.

The current local reports are `v110-focused-ctest.xml`, `v110-tooling-tests.log`,
`v110-removal-parity/report.json`, `v110-active-enuc/evidence.json` and
`v110-amr-sanitizers/report.json`. Their hashes, tested artifacts and reproducible
short-case parameters are preserved in the compact record. The coupled check
took 336.854 seconds and the four instrumentation runs took 18.094 seconds;
neither used swap or triggered a resource stop. Instrumentation is scoped to
the two AMR tests, not the full application or a repeated sparse-network campaign.

Continuous baseline/candidate runs compare every checkpoint dataset and root
attribute exactly. Continuation checks compare all datasets and physical/controller
attributes, excluding only the output file indices because the interrupted
three-step recipe writes an extra final output. The separately maintained
coupled-restart owner retains its stricter output-phase checks. Regrid comparisons
exclude wall time but include decisions, block counts and device operation counts.

## Accepted integration baseline before recorder removal

The following checks identify the tree delivered in `efbca9f0`. Its optional
recorder checks establish historical integration behavior, not a feature of
the v1.1.0 package. The release-preparation results above have their own scope.

| Check | Status |
| --- | --- |
| Independent worktree and source pin | Prepared |
| Evidence archive and inventory | Preserved; 13,394 branch-added historical files moved outside the maintained tree |
| Production code and comments | Bounded cleanup implemented; core and selected test targets built successfully |
| Maintained documentation and link audit | Main documentation reconciled; 153 maintained Markdown files checked without missing local targets |
| CPU build and targeted contracts | All 352 maintained tooling tests passed without skips |
| CUDA build and integration | Core/selected targets built; all 12 focused CPU/CUDA CTests passed |
| Active ENUC limiter plus AMR/restart | Final binary passed BD/RKL2 with all transport: 12 lanes, 9 comparisons, binding ENUC witnessed on every lane |
| Predictive recorder CPU/CUDA checks | Final binary passed 24 runs and 12 exact dataset comparisons across 1D/2D/3D |
| CUDA memory/race checks | Final AMR composition, Hydro, burn, diffusion and cuDSS tests passed all 10 memcheck/racecheck runs with complete clean reports |

## Archive and source review

The original source archive is
`/home/shiroakane/ARCH-hpc-cuda-archives/ARCH-hpc-cuda-source-evidence-3dab4e25.tar.gz`,
SHA-256 `bbaaafaf793b41b80f5bf27d5799fd6535339f0f072a0c5d885b7d7f50f992c5`.
The recoverable file tree and inventory are under
`/home/shiroakane/ARCH-hpc-cuda-archives/arch-hpc-cuda-evidence-20260918/`.
The moved files total 701,844,719 bytes;
their original contents were checked against the pinned archive before moving.
Git LFS scientific assets retain their separate materialized storage contract.
The [maintained evidence index](../../validation/backend/results/hpc-cuda-optimization/README.md)
links the source-pinned remote originals; no raw trajectory was discarded.

The source review keeps the collaborator's functional boundaries: one scheduler,
physics library, AMR criterion and conservative transfer implementation. CUDA
batch kernels and memory capacity owners remain backend-specific. No experimental
native-wave/window provider is promoted into production.

Local implementation adjustments are deliberately small:

- Share the original sparse backward-error gate between `CsrMatrixView` and
  correction arithmetic, preserving the allowance and summation order.
- Use existing typed identity comparisons instead of duplicate CUDA-local
  block/storage equality helpers.
- Keep NSE reason strings in the existing policy metadata table. This avoids
  a reproduced CUDA 12.3/GCC 12 nested-generic-lambda compile failure without
  changing policy registration or factory selection.
- Clarify wave/workspace lifetime and observer comments. The original integration
  also checked the optional recorder's Boolean parsing and stream errors;
  that recorder is removed during v1.1.0 preparation.
- Reuse completed Device ghosts for the current field version. The optional
  CUDA recorder exposed a redundant boundary refresh that invalidated Host
  ghosts while leaving the interior synchronized; whole-state materialization
  correctly refused that mixed transfer request. The shared Driver boundary
  now checks freshness once, also removing the duplicate test from the Host
  synchronization caller. AMR decisions and transfer contracts are unchanged.

Reusable validation now lives in `validation/backend/`, not historical results
or the Python unit-test directory. It uses the existing process, provenance,
restart and scientific-comparison owners. CI installs NumPy/h5py for retained
protocol tests; historical server-retirement tools are archived separately.

### Implementation review boundaries

| Area | Retained design and review conclusion |
| --- | --- |
| Hydro and diffusion batches | Kernels reuse existing cell/face operations. Shared scheduler descriptors retain stage weights, ghost refresh, reflux and publication order. Batch success means completion, not just enqueueing. |
| Dense burn batches | Registered network/EOS/ODE bindings still call the shared cell policy and accepted-source handoff. No CUDA-owned physical model or independent ENUC formula was added. |
| AMR and composition | Topology/Morton decisions remain host-owned. Dominant-species closure and face normalization are shared CPU/CUDA mathematics, with the collaborator's coupled campaign providing the scientific evidence. |
| Storage reuse | Reusable allocation capacity is separate from topology-bound views. Owners must drain work before replacement, migration or destruction. |
| Sparse provider | cuDSS factors and bounded cache are backend-specific. Original-system residual acceptance is shared; optional correction does not relax the ODE acceptance budget. |
| Cross-project integration | Local batches preserve logical-key reduction and publication ownership. This does not implement MPI, distributed collectives or distributed AMR. |

Wide-species global scratch deliberately limits some kernels to one block per
wave to avoid aliasing. The production sparse provider still incurs host
orchestration and synchronization. These are understood performance boundaries,
not duplicated physics; changing them belongs to the next separately measured
large-network optimization task. No additional folder splitting is needed for
this stage: runtime files already follow control, hydro, burn, diffusion and AMR
responsibilities, while historical experiments no longer obscure that layout.

## Local resource observations

WSL exposes about 7.7 GiB RAM and 2 GiB swap. The actual GPU is an RTX 3060 Ti
with 8 GiB VRAM. A restricted-process NVML probe could not access it, but the
unrestricted probe, cuDSS execution and memcheck all succeeded. That sandbox
restriction is not a GPU or driver failure.

The Debug integration build uses GCC 12, CUDA 12.3, native architecture 86,
two total jobs and one heavy CUDA job, with the existing memory/pressure guard.
It reuses the installed HighFive, SuiteSparse and cuDSS dependencies and does
not generate or compile audit150/audit200. Production optimization flags are
unchanged. The successful guarded core build took 4,530.760 seconds, followed by
a 12.059-second final incremental build. Minimum system available memory during
the core build was 3,381,676 KiB; sampled peak owned RSS was 3,876,944 KiB. Swap
remained unused, memory full-stall pressure stayed at zero and peak sampled I/O
full-stall pressure was 5.893%. No guard stop occurred. This is a safe local
maintenance build observation, not a general compile-speed qualification.
The observer-boundary repair required only three host dispatch objects and
relinking, taking 76.243 seconds. It used no swap and triggered no resource stop;
its peak sampled memory full-stall pressure was 6.539%, below the configured
sustained-pressure guard.

## Active ENUC validation semantics

The initial short BD attempt retained the existing four-step restart protocol.
It reached different physical times on CPU and CUDA once the ENUC ceiling became
active, so the strict temporal comparator correctly rejected the comparison.
The failed attempt and logs are retained under `build-integration/active-enuc-bd`.
This does not invalidate the collaborator's fixed-protocol campaign.

The new optional `--terminal-time` mode in the existing restart owner compares
cross-backend trajectories at a prescribed common physical endpoint. Same-backend
split runs still require strict timestep/controller and output-history agreement;
all field, composition and topology budgets are unchanged. No solver timestep
formula is changed. The ENUC witness reads the stored checkpoint ceiling and
matches subsequent accepted steps: the console's `dt_burn` column reports the
executed Strang half-step and is not a limiter diagnostic.

The short witness uses BD/RKL2, all physical transport, `enucDtFactor=1e-11`
and terminal time `4.5e-16`. An earlier `3.5e-16` endpoint passed the restart
comparisons but clipped the only step after a terminal restore, so it could not
witness an actually ENUC-bound accepted step on every route. That attempt is
retained as an input-coverage failure, not a solver failure. The longer short
endpoint passed all 12 lanes again on the final observer-boundary repair.
BE_NR/ROS4 scientific campaigns are not repeated.

## Integration baseline evidence

The [source-pinned baseline record](https://github.com/Shiro-Akane/ARCH/blob/efbca9f075d208017cf3db8e514f2ad25aebdbca/validation/backend/results/hpc-cuda-optimization/integration.json)
contains the source fingerprint, tested binary hashes, exact test counts and
comparison modes. The recorder, coupled restart and sanitizer reports agree on
source fingerprint `a3807aabace447c178c60332d1c55391f5ea29e3914f23310c5349390cdd69c2`.
The tested ARCH SHA-256 is
`bd8384adc30495ce9dc9a824063220b0c336f662eab19c8aa42dafbddce83951`.
These identify the integration tree as tested before the publishing commit,
not a new runtime measurement or release tag. The compact record's
`changes_committed: false` describes that capture time and is intentionally
retained. The later recorder-removal code changes require the separate checks
listed under release preparation; they do not change what this baseline ran.

Detailed logs, failed input attempts and generated HDF5 remain outside the
maintained data set, under the ignored `build-integration/` directory. The final
records are `focused-ctest.xml`, `tooling-tests-final.log`,
`documentation-links.json`, `recorder-final/report.json`,
`active-enuc-accepted/evidence.json` and `focused-sanitizers/report.json`.

To reproduce the additional active-limiter gate with installed Python dependencies:

```bash
OMP_NUM_THREADS=4 OMP_DYNAMIC=FALSE python3 validation/backend/verify_microphysics_coupling.py \
  --build-dir build-integration --output-dir build-integration/enuc-recheck \
  --restart --all-transport --active-enuc-factor 1e-11 --terminal-time 4.5e-16 \
  --case coupled_bd_rkl2_all_transport
```

Use a fresh output directory; the
[backend validation guide](../../validation/backend/README.md) describes the
maintained entry points. Short kernel
instrumentation uses the existing `validation_sanitizer` owner and default small
inputs; it does not claim whole-program or large-network sanitizer coverage.

## Handoff

The baseline curation and the recorder-removal release-preparation gates are
complete. The publishing scope is a commit and fast-forward push to
`origin/codex/hpc-cuda-optimization`, leaving main unmerged; no tag or hosted
release is created. A later integration PR must check the then-current main
branch for independent changes. Git references establish the actual publication
outcome; the test record above retains its original capture identity.

The English/Chinese root READMEs and backend guides summarize measured GPU
scaling, including the 5.08× coupled-AMR result and the 150/200-isotope slowdown.
The maintained campaign index includes absolute times and immutable original
reports. These are the collaborator's accepted measurements, not a repeated
local performance campaign. Final publication checks cover numeric transcription,
maintained Markdown links, whitespace and the staged file inventory; large
HDF5 files, build outputs and recovery archives remain outside this commit.

The user-facing documentation uses functional names, not internal milestone
codes. Eight intermediate review/checklist documents are preserved under
`docs/development/archive/`; the active acceptance checklist is concise and
separate. Module validation summaries present methods, errors and conclusions
before an explicitly expandable evidence section. Raw data, dated repair notes
and historical performance references are not ordinary user-index destinations.
The root repository maps and English/Chinese documentation indexes reflect this
layout. Archiving changes presentation and relative links, not measurements.

The final documentation audit scanned 191 Markdown files for unexplained
milestone labels and balanced expandable sections, with no findings. The 155
guide, summary and contributor/archive pages passed local-link checks. Public
validation indexes contain no direct raw-result links outside their labeled
evidence sections. Numeric transcription checks matched 66 bilingual scaling
cells and 44 absolute-timing cells to the original reports, with all 11 timing
ratios recomputed successfully. These are publication checks, separate from the
earlier 153-page documentation count in the captured runtime verification record.

The collaborator's original scientific/performance evidence remains authoritative
for its identified campaign. Further production 150/200-species acceleration,
experimental sparse-provider promotion and MPI integration remain separate work;
this cleanup neither repeats those experiments nor claims to complete them.
