# Archived AMR development notes

Snapshot preserved on 2026-09-07 JST before separating user summaries from
historical diagnostics. Status statements and commands below describe their
original checkpoints, not the current release decision. See the [current AMR
summary](../../README.md) and [active release plan](../../../../docs/development/CudaReleaseStandard.md).

# AMR conservation and refinement behavior

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

Current release validation is in progress. The sections below distinguish
focused checks from full application acceptance; earlier device runs remain
available in the [historical evidence](../../results/h100-sm90-20260903/README.md).

This record separates conservation from refinement efficiency. The historic
CPU measurements below established the original baseline. The current AMR path
replaces the old piecewise-constant coarse-to-fine fill with one shared,
conservative limited-linear reconstruction and retains the focused interface
regression that exposed the old false-refinement signal.

## Current application checks

The current Release candidate passes all ten cases in the Cartesian AMR matrix,
covering 54 CPU/CUDA executions with the prescribed topology, field and
conservation checks. The [application evidence](../../results/release-cartesian-shared-helm-20260907/backend-validation-evidence.json)
records the inputs and actual build identities.

Dynamic restart passes all four CPU/CUDA directions for both
[smooth advection](../../results/restart-smooth-shared-helm-20260907/restart-validation-evidence.json)
and [burning with ENUC-driven refinement](../../results/restart-burn-shared-helm-20260907/restart-validation-evidence.json).
Each set includes intermediate post-regrid and terminal checkpoints, with
energy, timestep-controller and output-history continuity checks.

The [Gaussian initialization and thermal-activity check](../../results/gaussian-initialization-20260907/evidence.json)
covers cylindrical and spherical initial states in one, two and three dimensions.
A coupled thermal on/off control produces a resolved energy change on both
backends at identical time and topology. The complete evolved curvilinear
matrix is the next application check.

## Focused shared-geometry checks

The [geometry and diffusion evidence](../../results/diffusion-capacity-release-20260907/evidence.json)
checks thin radial shells and cells near both spherical poles against independent
70/90-digit integrals. CPU and CUDA also pass spatial refinement tests for thermal/species
diffusion and viscous momentum/energy fluxes in all three coordinate systems and
dimensions. Viscous tests include uniform Cartesian velocity, quadratic fields,
variable density and the one-dimensional radial specializations: 30 profiles at
three spacings per backend. The largest finest-grid normalized error is
`7.27e-5`, within the fixed `1e-4` budget. Linear radial flow balances at the
origin in 18 configurations per backend. Six actual radial operator matrices
per backend satisfy forward-Euler contraction. Nine additional matrices per
backend check density contrasts of 1, 10 and 100 using the actual face transport
and cell capacities. Thermal/species tests add 54
paired spatial samples, with finest absolute error below `1.15e-7` against a
`1e-6` budget. CUDA memcheck reports zero errors. Coupled time-evolved AMR is evaluated
separately. Spherical 2-D uses the project's polar `(r,phi)` convention.

Build `arch_curvilinear_metrics` and `arch_cuda_curvilinear_geometry_smoke` in a
testing-enabled build, then reproduce the references and archive both executors
with Python + mpmath:

```bash
OMP_NUM_THREADS=2 python3 -B validation/amr/geometry_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-geometry-run
```

The output directory must be new. The evidence records commands, complete spatial
coverage, numerical budgets and source/build identities before and after the run.

## Fixed cases

The committed inputs are:

- `smooth_uniform80.par`: 80-cell uniform control;
- `smooth_uniform160.par`: uniform reference at the AMR finest spacing;
- `smooth_amr80_l1.par`: 80-cell root grid, one refinement level, a smooth
  entropy wave crossing moving coarse/fine interfaces;
- `sedov_amr_species.par`: regularized 2D Sedov blast with one advected species.

All cases use Cartesian geometry, the ideal-gas EOS, HLLC, and RK3. The smooth
case uses MUSCL-MC and regrids every two steps. The Sedov case uses PPM and
exercises multidimensional regrid, reflux, and species fluxes.

## Historic CPU audit environment

The audited working tree was based on
`affde827fcbf317382ed45372912b562652a71c5` plus the changes recorded here. It
used GCC 13.3.0, the CPU backend, Release flags
`-O3 -march=native -ffast-math -DNDEBUG`, and two OpenMP threads on an
Intel Core i7-10700 under x86_64 WSL2.

## Quantitative result

The norms use physical cell volumes. The AMR-versus-uniform comparison first
restricts the 160-cell reference to the AMR leaf covering, then evaluates both
fields on that common mesh.

| Case | Measurement | Result | Decision |
| --- | ---: | ---: | --- |
| smooth AMR | maximum mass drift | `7.33e-15` | pass |
| smooth AMR | maximum momentum drift | `7.33e-15` | pass |
| smooth AMR | maximum energy drift | `2.22e-14` | pass |
| smooth AMR | pressure Linf deviation | `1.41e-14` | pass |
| smooth AMR | density L1 / L2 / Linf versus analytic state | `7.04e-5 / 1.19e-4 / 4.56e-4` | pass |
| uniform 160 | density L1 / L2 / Linf versus analytic state | `2.47e-5 / 5.13e-5 / 2.58e-4` | reference |
| AMR vs restricted uniform 160 | density L1 / L2 / Linf | `5.32e-5 / 1.01e-4 / 4.43e-4` | pass |
| Sedov AMR | mass / species-mass drift | `1.11e-16 / 1.11e-16` | pass |
| Sedov AMR | x/y momentum drift | `1.00e-18 / 1.00e-18` | pass |
| Sedov AMR | energy drift, relative | `3.55e-15`, `3.43e-15` | pass |
| Sedov AMR | energy centroid | `(0.499924, 0.499924)` | pass |
| Sedov AMR | radial anisotropy | `6.14e-6` | pass |

The Sedov topology remains 12 level-0 plus 16 level-1 leaves through the run.
Its deposited energy is sampled at cell centers, so the invariant is drift from
the numerically initialized energy (`1.0361899940878605`), not equality to the
continuum input value `1.0`.

An isolated transfer audit (not retained as a repository test target) covered
conserved fluid variables and two `rho X` fields. Prolongation integral errors
are `2.60e-18`, `3.33e-16`, and `2.61e-14` in Cartesian 1D/2D/3D, and
`2.22e-16` and `1.11e-15` in cylindrical 2D and spherical 3D. Corresponding
restriction errors are `1.71e-16`, `9.00e-19`, `5.55e-17`, `1.11e-16`, and
`1.11e-16`; curvilinear checks use physical volumes. Fine-cell composition
closure is within `3.33e-16`. A strong-gradient
case that previously produced `sum(X)` errors up to `0.321` now preserves
species integrals, non-negativity, and closure to `1.11e-16`.

The same probe included an adversarial Euler state whose independently limited
candidate had internal-energy density `-0.605`. The common convex limiter kept
every refined cell above `min_eint = 1e-10`, with a minimum specific internal
energy of `1.000036e-10`, while the five conserved physical-volume averages
changed by zero in Cartesian geometry and `4.44e-16` in a nonuniform-volume
cylindrical check.

## Refinement follow-up status

The earlier audit found that piecewise-constant coarse-to-fine ghost filling
could raise an interface Lohner indicator from `0.00775/0.00954` to
`0.06727/0.04230`. That implementation has been replaced by shared
limited-linear conservative-variable reconstruction. It reconstructs `rho X`
before recovering composition, applies one convex physical-state limiter, and
uses volume-aware fine-to-coarse restriction.

The retained transfer and AMR exchange tests now cover Cartesian 1D/2D/3D,
curvilinear Host volume weighting, X/Y/Z faces, both interface orientations,
and `Current`/`Next`/`Scratch`. CUDA consumes the Host-lowered plan and the same
scalar interpolation mathematics; it does not keep a second formula.

Checkpoint v3 persists ENUC and both backends transfer it through regrid. The
archived real-device restart matrix for `refine_var = ENUC` passed uninterrupted
and all four split-run backend routes for its recorded binary; it is not
final-artifact qualification for the current changes.
Legacy v1/v2 checkpoints still initialize ENUC
to zero and therefore cannot establish ENUC split-run equivalence.

## Final-artifact qualification

Use an unchanged Git checkout and build configuration throughout all required
suites. Both runtime validators require `--build-dir`, which must contain
`CMakeCache.txt` and `compile_commands.json` and belong to `--source-root`.
Build `arch_cuda_single_level_validation` as well as `ARCH`; merely building
the application with `BUILD_TESTING=OFF` does not provide the comparator.

The common `tools/validation_provenance.py` facility records full source commit,
dirty source content, build configuration/options, compile-command hash, and
SHA-256 for both executables. Its documented `code-and-validation-inputs-v1`
scope hashes code and validation inputs under `src`, `simulation`, `tests`,
`tools`, `cmake`, and `validation`, plus root CMake configuration. It excludes
documentation, archived results, build/output trees and unrelated untracked
user files. Untracked or Git-ignored source files are **included**, because
CMake may compile them; in particular, do not silently omit a local simulation
`.cpp`. This records the observed checkout, not a reproducible-build attestation.
Source/build changes, executable replacement, and executable edit-then-restore
during a suite prevent publication of a successful report.
Each case also fingerprints the effective runtime EOS table, including
`eos_table_path` overrides and external files resolved from ARCH's working
directory. The canonical `.par` alone is insufficient because it stores only
the table path. Table identities are rechecked after execution and by the
aggregate gate. Qualification requires build-local configured executables:
`ARCH_RUNTIME_OUTPUT_DIRECTORY` must be beneath `--build-dir`, `--arch` must
name that configured `ARCH`, and the comparator must be the selected build's
`arch_cuda_single_level_validation`. A Debug binary from a different configured
build path cannot be paired with a Release cache; this path binding is not a
reproducible-build proof and cannot detect an arbitrary same-path replacement
made before evidence capture.

Example for Debug (adapt the CUDA architecture and dependency setup to the
host; repeat in a separate Release build and evidence directory):

~~~bash
amr_build="$PWD/build-cuda-debug"
amr_output="$(mktemp -d /tmp/arch-amr-debug-XXXXXX)"
cmake -S . -B "$amr_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DARCH_ENABLE_CUDA=ON -DBUILD_TESTING=ON \
  -DARCH_ENABLE_KLU=OFF -DARCH_FETCH_SUITESPARSE=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$amr_build/bin"
cmake --build "$amr_build" --target ARCH arch_cuda_single_level_validation --parallel 1

python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json --source-root . \
  --arch "$amr_build/bin/ARCH" \
  --checkpoint-validator "$amr_build/arch_cuda_single_level_validation" \
  --build-dir "$amr_build" --output-root "$amr_output/matrix"
python3 tools/validate_cuda_amr_restart.py \
  --source-root . --arch "$amr_build/bin/ARCH" \
  --checkpoint-validator "$amr_build/arch_cuda_single_level_validation" \
  --build-dir "$amr_build" --output-root "$amr_output/restart-smooth"
python3 tools/validate_cuda_amr_restart.py \
  --source-root . --arch "$amr_build/bin/ARCH" \
  --checkpoint-validator "$amr_build/arch_cuda_single_level_validation" \
  --build-dir "$amr_build" --problem BurnGradient \
  --input validation/amr/inputs/burn_enuc_amr.par \
  --output-root "$amr_output/restart-enuc"

sha256sum "$amr_build/bin/ARCH" "$amr_build/libarch_cuda_backend.a" \
  > "$amr_output/final-artifacts.sha256"
python3 tools/qualify_cuda_amr_evidence.py \
  --source-root . --build-dir "$amr_build" --arch "$amr_build/bin/ARCH" \
  --checkpoint-validator "$amr_build/arch_cuda_single_level_validation" \
  --final-artifacts "$amr_output/final-artifacts.sha256" \
  --matrix "$amr_output/matrix/backend-validation-evidence.json" \
  --restart-smooth "$amr_output/restart-smooth/restart-validation-evidence.json" \
  --restart-enuc "$amr_output/restart-enuc/restart-validation-evidence.json"
~~~

For multi-configuration generators, pass the same explicit `--configuration`
to all three validators and the final gate, and use the actual configured
executable paths. The gate rejects different source/build identities, comparator
or application hashes, partial matrix/route coverage, and historical reports
without the post-run identity check. Reports must retain resolved plans,
checkpoint hashes, trace and RKL completion summaries, exact comparison
policies, and all requested scientific/conservation/topology metrics. The gate
reuses the runtime validators' shared checks to revalidate those metrics; a
sparse `passed: true` record is not sufficient. It is an evidence consistency gate, not
a replacement for the runtime suites, frozen-main burn regressions or sanitizer.
Keep historical JSON/logs unchanged; publish each new qualification separately.

The current restart runner executes both checkpoint phases: intermediate step 2
after regrid (from a process completing step 3), and terminal step 3. Each has
all four CPU/CUDA restart directions. Per problem this means 12 ARCH executions
and 9 comparisons, with actual checkpoint metadata and a restore-log witness.
Checkpoint selection uses the recorded step and restart phase, not a fixed
filename index. The terminal source performs one additional forced CHK and PLT
output relative to the uninterrupted route. Terminal comparisons derive that
exact counter offset from the actual intermediate/terminal source files and
require it to be one for both counters; ordinary comparisons require zero.
Physical fields, controller state and topology retain their original checks.

The default qualifier profile covers AMR only. For the full runtime profile, add
`--profile full-runtime --curved-matrix <report> --uniform-matrix <report>`
and `--generated-matrix <report>` to the command above. Produce these reports
with the same `validate_backend_results.py`, using the curved AMR, uniform
backend and generated-network manifests: `validation/amr/gpu_curvilinear_cases.json`,
`validation/backend/cases.json` and `validation/network/runtime_cases.json`.
All six reports must match one final build identity. Even this profile emits
`release_qualified: false`: independent physics, sanitizer and capacity evidence
are additional gates in [CudaReleaseStandard.md](../../../../docs/development/CudaReleaseStandard.md).

## Curvilinear application matrix

[`gpu_curvilinear_cases.json`](../../gpu_curvilinear_cases.json) defines 24 cases:
species-only diffusion and coupled thermal, viscous and species diffusion, each
in cylindrical/spherical 1D, 2D and 3D with RKL1 and RKL2. Full application
qualification is in progress. The Cartesian matrix remains a separate suite.

Each CPU/CUDA pair renders the same canonical
[`gaussian_diffusion_amr.par`](../../inputs/gaussian_diffusion_amr.par), with shared
overrides for cylindrical or spherical annuli and wedges. The pulse is centred
at physical Cartesian position `(1.5, 0, 0)` and uses refinement levels 0--1.
The 1D domain is `1 <= r <= 2`, with four root blocks and capacity 32. The 2D/3D
domains extend to `r = 3` at the same radial spacing, leaving quiet outer blocks
to exercise coarse/fine interfaces: 2D uses 8-by-2 roots/capacity 128; 3D uses
8-by-2-by-2 roots/capacity 512. All active boundaries are reflecting.

Species-only cases retain uniform pressure and zero velocity. Coupled cases
add a 20% pressure pulse and nonzero native velocity components, with thermal
diffusivity 0.2 and kinematic viscosity 0.1. Both groups retain species
diffusivity 0.5, the ideal-gas EOS and the same species refinement thresholds.
The Gaussian problem evaluates one envelope from Grid's physical Cartesian
coordinates, including all three directions on spherical/cylindrical 3D grids.
Its ordinary parameters `xc`, `yc`, `zc` and `width` define the centre and scale;
`pressure_amplitude` is fractional and `u_amplitude`, `v_amplitude`,
`w_amplitude` are native orthonormal velocities. All four perturbation
amplitudes default to zero. CPU and CUDA use the same initialization.
These passive-gas cases select `network_name = none` and `gas_cv = 717.5`,
so a pressure perturbation produces a nonuniform temperature and real thermal
transport. Explicit nuclear-network selections remain available for other cases.

The focused [initialization and activity check](../../gaussian_reference.py) verifies
the six curved initial states independently, then switches thermal diffusion
on/off at identical time and topology on each backend. The energy change must
be larger than roundoff. Reproduce it after building ARCH and the checkpoint
comparator, with NumPy and h5py installed:

```bash
OMP_NUM_THREADS=4 python3 -B validation/amr/gaussian_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-gaussian-check
```

RKL1 and RKL2 each run to 2 and 5 accepted steps, with hydro RK2 still enabled
and regrid checked every step. The stage-cap-5 policies retain the existing
four-stage RKL1 and five-stage RKL2 Strang half-lane checks. The inherited
localized initial pulse is intended to produce a mixed initial hierarchy;
the existing manifest gate requires refined and mixed leaves in the observed
checkpoints. Refine/derefine transitions and restart continuity have separate
tests. Independent spatial convergence and origin balance are checked by the
focused geometry suite above; this matrix checks their integration into a
time-evolving AMR application.

Conservation uses physical-volume integrals of **mass, energy, and every
`rhoX` only**. Radial momentum has a geometric source and wall forces, so it is
compared between CPU/CUDA but is not imposed as a global invariant. The budgets
use the same numeric values as the existing 1D AMR/RKL cases: conservation
`rtol = 2e-12`, `atol = 2e-11`; field parity `rtol = 1e-8`, `atol = 5e-12`.
Do not tune them after observing results or apply the Cartesian analytic
Gaussian reference to these radial equations.

The new manifest explicitly requests
`conservation_policy.measure = "physical_cell_volume"`. Its metrics require
`arch_cuda_single_level_validation --metrics CHECKPOINT --parameters ACTUAL_RUN.par`:
version-3 checkpoints do not store domain bounds or root block counts. The
validator supplies each lane's actual rendered parameters, including overrides,
and records their SHA-256 and the measure. Geometry reconstruction uses the
existing `RuntimeParams`, `Block::InitGeometry`, and `GridMetrics::CellVolume`;
missing/mismatched parameters or measures fail closed. Rebuild the comparator
before running this pending matrix. The no-parameter Cartesian CLI and old
manifests retain their historical `2^(-dimension*level)` normalized weights and
absolute-budget units; they are not silently reinterpreted as physical volumes.
The new physical-volume budgets are declared separately with the numeric values
above, not claimed to have identical units to the old normalized budgets.
The aggregate final-artifact gate also still expects the original Cartesian
matrix; retain any future curvilinear report separately as additional evidence.

Input/schema checks only (neither command runs ARCH):

~~~bash
python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_curvilinear_cases.json --unit-test
python3 tests/test_curvilinear_manifest.py
~~~

After both prerequisites are met, use the same runtime validator and final
build-local executables described above, with this manifest and a **fresh**
output directory. No physical-validation result is claimed by preparing or
parsing these inputs.

## Reproduce historic CPU cases

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform80.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform160.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_amr80_l1.par
./bin/ARCH Sedov validation/amr/inputs/sedov_amr_species.par
~~~

The retained scalar results are in [metrics.csv](../../metrics.csv). Raw HDF5 output
is intentionally excluded from version control. The historic plots below remain
qualitative diagnostics and are not inputs to the acceptance decision.

| Historic case | Modules visible | Archive |
| --- | --- | --- |
| Sedov | hydro, shock-driven refinement | [figure](../../figures/legacy/sedov.png) |
| Gaussian | diffusion, moving refinement pattern | [figure](../../figures/legacy/gaussian.png) |
| Rayleigh--Taylor | hydro, gravity, diffusion | [figure](../../figures/legacy/rayleigh_taylor.png) |
| Cellular burn | hydro, burning | [figure](../../figures/legacy/cellular_burn.png) |

PPM uses MUSCL-MinMod at coarse/fine faces, so this record does not claim
third-order AMR-wide spatial convergence.
