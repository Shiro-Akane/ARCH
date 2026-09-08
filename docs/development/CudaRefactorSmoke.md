# Shared AMR, sparse burn, and coordinate refactor

Audience: contributors. Historical experiment and smoke evidence, not the
current user-facing capability guide or final release acceptance.

Status: full build, BD fix and initial AMR runtime smoke passed; shared AMR
noise-sensitivity review remains open, 2026-09-05. This is a short-smoke record, not
scientific validation or release qualification. Historical H100 results do not
qualify this working tree.

Historical scope notice (2026-09-06): the current acceptance contract and source
status are tracked in [CudaReleaseStandard.md](CudaReleaseStandard.md). That
round reopens shared geometry corrections, adds external-gravity execution,
and changes header/call boundaries. The old results and frozen-formula statements
below describe the September 5 artifacts; they do not qualify the newer sources.

## Single mathematical authority

| Facility | Common implementation | Backend-specific work |
| --- | --- | --- |
| AMR indicators | `RefinementIndicatorMath.h`: selection, Lohner error, thresholds | Device evaluation and compact result transfer |
| AMR topology | CPU `AmrTree`, Morton codes, logical plans, transaction coordinator | Device store staging and retirement |
| Refine/derefine | `RegridTransferMath.h`: conservative interpolation, admissibility, composition projection, volume-weighted restriction | Device gather/scatter and workspace |
| Burn and ODE | `DriverBurn`, network RHS/Jacobian, BE_NR/BD/ROS4 continuations | Device kernels suspend for a host-invoked cuDSS solve on device buffers |
| Sparse structure | `CsrPattern`, `CsrMatrixView`, declared structural entries and temperature coupling | cuDSS descriptors, analysis, factors, stream ownership |
| Coordinates | `GridGeometryView`, `GridMetrics`, `GeometricSources`, `DiffFlux` | Device views and launches |

Crucially, there is no second, duplicate copy of the reaction, ODE, interpolation, or coordinate formulas. We strictly preserve the existing CPU conventions and numeric floors, including its dimension-specific cylindrical and spherical specializations. This design choice establishes *implementation parity* across backends, rather than serving as an independent physical validation of those conventions themselves.

### Functional compilation boundaries

Frozen main resolves burn policy behind `BurnerHandle` before hydro flux
dispatch. CUDA follows that ownership boundary: upper-level control keeps a
selected execution interface, and network/EOS delegates instantiate the same
burn kernels and ODE definitions. There is no second CPU/CUDA algorithm tree.

`CudaBurnNetworks.cmake` is the shared compile inventory for dense and sparse
delegates. The generated network/EOS translation units contain only bindings;
they are not manually maintained copies of mathematical implementations.
Existing Tabular per-network boundaries are retained. The obsolete all-network
Ideal/Helm instantiation header is replaced by lightweight registered dispatch.
Resources, migration, indicator evaluation, and microphysics control remain
grouped by responsibility, not arbitrary file-length limits.

## AMR communication and memory

- Regrid decisions download one scalar indicator per block, not all conserved
  and species fields. CPU topology decisions remain authoritative.
- Changed topology migrates on device: survivor Current uses D2D; children and
  parents use shared prolongation/restriction. Physical, same-level, and
  coarse/fine ghosts are completed in the staged store before publication.
- Failure leaves the accepted device generation intact. Indicator scratch
  retains capacity; migration scratch is reused across groups; transport scratch
  is shared across blocks and bounded by useful launch work and device resources.
- Plot/checkpoint and explicit Host consumers still materialize state on demand.
- Geometry caches are initialized by a device setup kernel invoking the same
  `GridMetrics` leaves. This replaces seven full-block Host metric arrays and
  their `7 * total_size * sizeof(double)` H2D uploads, including for survivors
  whose runtime objects are recreated. Ghost/padding slots retain positive zero.
  It does not remove the seven device arrays or their allocation cost.
- Old and new stores coexist until safe publication/retirement. Survivor arena
  reuse is not implemented. Communication was reduced; no benchmark yet proves
  that AMR can never be a bottleneck or an arbitrary hierarchy fits in 16 GiB.

Boundary, ghost-transfer, and flux/reflux descriptors still require metadata
uploads. The later release repair counts these uploads through the existing
backend resource owner, together with grid-metric kernels and flux-construction
fences. The driver still has no complete regrid transfer trace span. The
transaction test's four-byte status check snapshots counters after staging;
it describes the migration phase, not all regrid communication. Whole-regrid
aggregation and end-to-end timing remain necessary before a performance claim.
See the current [release ledger](CudaReleaseStandard.md) for the repair and
exact-byte regression evidence; this document preserves the earlier smoke scope.

## Sparse execution and limits

`linear_solver = Auto` retains the shared small-network DenseLU threshold.
Larger CPU networks select KLU; larger CUDA networks select cuDSS. Explicit
CPU+cuDSS and CUDA+KLU are rejected. Missing providers/bindings are errors, not
hidden execution-time CPU fallbacks.

The CUDA numerical pool scales with sparse nonzeros and network-sized ODE
vectors, not a dense matrix for every mesh cell. It is reused across blocks and
chunks. One resident cuDSS factor owner bounds simultaneous fill-in; switching
lanes can require refactorization even when an ODE's own Jacobian is unchanged.
That preserves the method but may cost time. Factor-memory estimates are checked
before factorization; infrastructure/resource failures are not adaptive ODE
retries. An individual sparse factorization must still fit available memory.
There is no unlimited-size or speedup claim.

cuDSS has host-callable APIs, but coefficient values, RHS, corrections, and ODE
state remain on the GPU. The provider disables hybrid CPU numerical execution
and Host factor spill. Only small scheduling/status messages cross the boundary.
See [NVIDIA's cuDSS API and memory-estimate contract](https://docs.nvidia.com/cuda/cudss/types.html).

The generator emits a single host/device mathematical header for recognized
immutable-table packages and records `device_callable_math` in the manifest.
Registered policies select generated network/EOS delegates. Packages without a
device-callable math interface and runtime-loaded weak-rate tables remain CPU-only; the latter
still need a backend table owner. Generated networks do not gain NSE.

## Build and smoke protocol

Local environment: WSL, approximately 7.7 GiB RAM, 2 GiB swap, CUDA 12.3, GCC 12,
RTX 3060 Ti with 8 GiB device memory. The initial observed swap baseline was
524 KiB, not zero; each later attempt records its own baseline and peak.
Builds are serial and guarded, for example:

```bash
python3 tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 \
  --log build/unique-smoke-build.log -- \
  cmake --build build/review-cuda-debug --target ARCH --parallel 1
```

These limits are operator-selected safety settings, not physical constants or
16 GiB capacity qualification. Logs are created exclusively, not overwritten;
the guard tracks and terminates only its own descendants, including compiler
children that create new process groups. The configurable Debug setting
`ARCH_CUDA_DEBUG_PTXAS_OPT_LEVEL` controls assembler optimization independently
of the strict floating-point contract. This local smoke build and the default
select `1`; Release retains its normal optimization. Earlier full
Debug attempts were deliberately interrupted to apply the functional split and
local assembler setting; those partial builds are not reported as passes.
An experiment with assembler level `0` used more memory for a dense network
unit: the guard stopped it at 1,474,384 KiB available, before WSL disconnection.
Level `1` was restored; the safety thresholds were not relaxed.

Block metric initialization and boundary uploads use member-owned storage and
are fenced by the enclosing construction/publication batch, or before member
retirement on a construction exception. There is no normal-path explicit fence
per block merely to retire temporary Host metrics. AMR flux-plan uploads still
fence their constructor-local buffers on success and exception. This does not
remove implicit allocation/pageable-transfer synchronization or change metric
calculations. Hydro,
diffusion, and burn status downloads likewise fence before their local Host
buffers can retire on an exception.

Tabular EOS errors are latched at their existing shared thermodynamic failure
boundary, including intermediate inversion failures that later produce a finite
temperature. CPU exceptions and all mathematical values/floors remain unchanged.
CUDA hydro, AMR indicators, diffusion and burn bind the same optional status on
a temporary EOS view. Failed burn cells do not commit; sparse continuations
report `EosFailure`, never a stale success from a previous pool use.

The reviewed optional dependency is cuDSS 0.8. Its local isolated prefix is
`build/dependencies/cudss-cu12/nvidia/cu12`; configure `-DCUDSS_ROOT=<prefix>` for
another installation. No system CUDA replacement is required. CMake publishes
provider capability only with the complete production dispatch and provider link.

Completed focused checks at this integration checkpoint:

- all three ODE continuations against frozen-main CPU references;
- device sparse linear algebra and ODE tests at 32, 151, and 201 equations;
- actual generated 31-species RHS/Jacobian/temperature-derivative CPU/GPU parity;
- generated CPU output comparison: 2,238 values bit-identical;
- multidimensional/curvilinear AMR indicator and migration tests, including
  large passive composition and invalid-input cases;
- curvilinear hydro/diffusion and boundary short tests;
- generator negative controls and validation-tool contracts.

Latest focused rerun (`build/refactor-eos-suite-smoke-10.log`): 6/6 tests passed
in 11.42 s, including frozen-main burn references, all three sparse ODEs through
201 equations, and real-device hydro/burn/AMR EOS-failure controls. The dedicated
cuDSS and compensated-sum run also passed 5/5 (`refactor-sparse-smoke-06.log`).
Diffusion EOS-failure controls are compiled with its existing parity target and
passed in the subsequent 16-test AMR/geometry run described below.

The subsequent production build (`refactor-cuda-debug-build-11.log`) completed
the Tabular dense-burn delegates, then failed in NVCC's generated Host C++ for
the nested hydro dispatch lambdas. The guard did not trigger: minimum available
memory was 2,776,056 KiB and swap use remained at 68,504 KiB. This is a compile
failure, not a completed executable build. The focused Host checkpoint-metric,
AMR-plan, topology-transaction, residency, and compensated-sum checks passed 5/5
after that attempt.

The hydro registry compatibility change uses named visitors without changing
policy registration or mathematical instantiation. Its independent CUDA-compiler
regression passed all 60 registered combinations and five negative/boundary
controls (`refactor-hydro-dispatch-build-13.log`, CTest `cuda_hydro_dispatch`).
The next build (`refactor-cuda-debug-build-14.log`) compiled all four production
hydro EOS owners and linked `libarch_cuda_backend.a`, then failed because the
regrid test did not link the CUDA runtime's include usage requirements. That
test target now explicitly links `CUDA::cudart`. The guard did not trigger:
minimum available memory was 2,393,048 KiB, and swap rose from 42,136 to 48,512 KiB.
The subsequent incremental build `refactor-cuda-debug-build-15.log` completed
all 64 steps and linked the full executable. Minimum available memory was
5,805,260 KiB; swap stayed at 48,512 KiB. All heavy production CUDA objects
were reused rather than recompiled.

The real-device run `refactor-amr-smoke-16.log` passed 16/16 tests in 36.85 s:
Host plan/residency/checkpoint contracts, production regrid transactions,
27 geometry-cache layouts and negative controls, migration, curvilinear
geometry, EOS failure handling, indicators/composition, diffusion/boundary,
single-level runtime, and 1D/2D/3D AMR exchange.

`build/refactor-amr-runtime-smoke-16/smoke-report.json` records 10/10 full-ARCH
development lanes passing (CPU/CUDA Sedov dynamic AMR and cylindrical/spherical
diffusion with same-backend restart). Actual HDF5 steps were checked: Sedov
reached 3, curved sources reached 2 and resumes reached 3. Sedov CUDA epochs
were 1, 2, 3, proving post-construction topology publication. All four resume
logs explicitly restored the designated checkpoint at step 2 and advanced in
time; this was checked in addition to the runner's terminal-step gate. Binary
and checkpoint-validator identities remained unchanged during this run.
These checks do not establish field parity, cross-backend restart or scientific
qualification. In particular, the curved runs ended with different CPU/CUDA
leaf counts; see the single-step diagnosis below. After the BD provider change,
the final executable was relinked successfully and all ten lanes passed again
in `build/refactor-amr-runtime-smoke-19/smoke-report.json`, with unchanged binary
identities during the run and all four step-2 restore records verified. This
final-binary run does not remove the shared indicator limitation.

Manufactured 150/200-species ODE fixtures test solver scaling; they are not real
150/200-isotope burn trajectories.

### BD / cuDSS precision diagnosis

The first production factory smoke passed Iso7's three ODE routes and audit31
BE_NR but rejected all five audit31 BD cells without committing any state.
Audit31 has 31 isotopes **plus temperature**, i.e. 32 equations; it is not a
31-by-31 matrix mistakenly sent through Auto's sparse branch. Exact cutoff and
explicit-debug-selection regression checks preserve the original dispatch.

The failure-only probe `refactor-bd-probe-smoke-17.log` drove the same CPU BD
continuation through the actual cuDSS provider. At extrapolation level 1 it
reproduced a componentwise backward error of 8.16009e-13, exceeding the unchanged
64*(32+1)*epsilon gate. cuDSS library/CUDA/numerical statuses were all successful.
The CPU dense solution of that same matrix also failed the residual gate;
the original synchronous CPU executor does not perform this extra check.
This isolates the observed failure from GPU BD state advancement or lane-token
reuse. BD midpoint/extrapolation/acceptance formulas were not changed.

The provider now enables device-side iterative refinement, configured by the
private CMake setting `ARCH_CUDSS_IR_STEPS` (default 2). cuDSS's separate global
two-norm tolerance remains disabled; the existing componentwise gate, physical
ODE budgets and no-commit behavior remain unchanged. The one-pass experiment
passed all six Iso7/audit31 ODE factory routes, CPU parity, pool reuse and
failure controls (`refactor-bd-ir1-smoke-18.log`). The final two-pass regression
passed 7/7 tests in 13.93 s (`refactor-bd-ir2-smoke-19.log`): frozen-main CPU burn
references, common continuations, exact solver cutoffs, all six actual factory
routes, all three device ODEs through 201 equations, singular/residual retries,
EOS failure/no-commit, and a new multiscale analytic linear system with factor
reuse and a deliberately perturbed tiny-component rejection. No tolerance or
ODE budget was relaxed. The build changed only the Host provider/test objects
and executable links; production CUDA mathematical instantiations were reused.
Minimum available memory for this final incremental build was 7,011,296 KiB;
swap remained 48,512 KiB. These small cases are not large-network qualification.

### Shared AMR near-zero sensitivity: unresolved parity limitation

The curved smoke starts from bit-identical six-leaf CPU/CUDA checkpoints.
Four additional one-step lanes in `build/amr-pre-regrid-diagnostic.Ls1r7A/run`
stop before the first post-step regrid: all still have six leaves and identical
timesteps. Composition differences are at most 8.88e-16 (cylindrical) and
1.55e-15 (spherical). Nevertheless, roundoff in the initially zero final species
causes the shared, scale-relative Lohner estimator to exceed 0.7 on different
coarse blocks. The internally sampled trigger locations predict the observed
CPU/GPU seven-versus-eight leaf counts at step 2.

The final difference is not merely roundoff: conservative aggregation onto the
common coarse grid gives a maximum main-species rhoX difference about 1.24e-3
(cylindrical) and 1.30e-3 (spherical), despite equal total mass/energy and species
integral differences below 4.45e-16. Passing execution/restart smoke therefore
does **not** establish field or topology parity. This is a shared indicator
policy issue exposed by roundoff, not evidence for maintaining a CUDA-only
formula or masking a particular species. No threshold, field selection, or
frozen mathematical formula was changed to hide it. Address it in the
[shared AMR noise-sensitivity review](../physics/AmrIndicatorNoiseReview.md)
before formal CPU/CUDA AMR parity qualification.

The real generated `audit200` mathematical-leaf build was attempted separately,
without registering it into the production smoke executable. Its initial linear
`constexpr_for` recursion exceeded NVCC's template depth. The generator now uses
ordered subdivision, preserving index types/order and the shared numerical body;
13 generator tests pass. A retry passed that diagnostic but exceeded this WSL
guest's memory safety budget and was stopped by the guard (observed minimum
available memory 943,628 KiB; no extra swap growth). **Actual audit200 CUDA
compilation and numerical parity remain unverified.** See local attempt logs
`build/refactor-audit200-math-build-07.log` and `-09.log`. The 201-equation solver
test is separate evidence and cannot close this compilation gate.

## After the smoke gate

Create formal validation data only after compile/basic runtime checks pass and
the final source/binary identity is recorded: dynamic AMR conservation,
curvilinear hydro/all diffusion modes, real large-network burning trajectories,
CPU/CUDA and cross-backend restart, long-running memory and sanitizer coverage.
Reuse the validation/provenance infrastructure; never relabel old H100 logs or
refresh independent reference values just to make results pass.

Still outside current CUDA capabilities: external gravity, self-gravity/Jeans,
and device owners for generated runtime-loaded weak-rate tables. Self-gravity is
deferred by project direction. Build throughput, multi-entry factor caching,
and survivor arena reuse remain optimization work.

### Frozen-main coordinate questions (not silently changed)

Two existing CPU conventions need an independent scientific review before
claiming multidimensional curvilinear qualification:

- `GridMetrics::FaceArea` uses `radial_shell_volume` for the angular faces in
  three-dimensional spherical geometry. This has length-cubed dimensions rather
  than the usual length-squared orthonormal face area.
- `DriverUtils::compute_cfl_candidate` uses native `dx2`/`dx3` directly; it does
  not convert angular increments to physical lengths such as `r*dtheta` or
  `r*sin(theta)*dphi`.

The CUDA backend currently follows these same shared CPU forms. A parity pass
cannot validate the underlying physics, and the new one-dimensional annular
cases do not exercise either angular issue. Changing these shared formulas is
a separate CPU/GPU scientific correction. The project explicitly chose to keep
the frozen formulas for this refactor and track that correction separately;
no reference values were refreshed to hide the issue.

The deferred questions, shared implementation boundary, and independent
acceptance gates are tracked in
[CurvilinearMetricReview.md](../physics/CurvilinearMetricReview.md).
