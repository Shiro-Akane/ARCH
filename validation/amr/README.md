# AMR conservation and refinement

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
authoritative.

Adaptive mesh refinement (AMR) adds cells where more detail is needed and merges
them where a coarser mesh is sufficient. Changing the mesh must preserve the
physical quantities carried by those cells. The records therefore check both
the refine/coarsen sequence and the fields after transfer, using physical cell
volumes when computing conservation in curved coordinates.

The results on this page belong to the scientific acceptance snapshot identified
in the [central Validation index](../README.md). Source organization and build
verification have a separate
[maintenance record](../backend/results/maintenance-freeze-20260908/).

ARCH uses the same refinement indicators, conservative transfers, geometric
measures and flux corrections on CPU and CUDA. The CPU manages topology and
Morton ordering; the CUDA backend keeps field migration and numerical updates
on the GPU. Overall acceptance is tracked in the [validation index](../README.md).

AMR restart requires a complete ARCH checkpoint, including `ENUC` and
native composition. The [restart guide](../restart/README.md) describes that
contract. The [focused interface checks](../../docs/development/ImplementationOwnership.md#current-interface-and-compatibility-review)
cover this reader contract separately; the scientific results below retain the
source identities of the runs that produced them.

## What the tests cover

| Test group | Coverage |
|---|---|
| [Cartesian application matrix](gpu_cases.json) | Ten cases: Euler/RK2/RK3, RKL1/RKL2, one to three dimensions, mixed-level fields, conservation, and a complete one-dimensional refine/coarsen cycle |
| [Three-dimensional runtime lifecycle](results/dynamic-3d-final-20260907/release-919/evidence.json) | Cartesian Sedov evolution with complete eight-child refinement and coarsening, seven checkpoints and fourteen CPU/CUDA executions |
| [Curvilinear application matrix](gpu_curvilinear_cases.json) | 24 cases: cylindrical/spherical, one to three dimensions, RKL1/RKL2, species diffusion alone and coupled thermal/viscous/species diffusion |
| [Curved two-dimensional runtime lifecycles](results/dynamic-curved-final-20260907/release-923/evidence.json) | Cylindrical and spherical RKL1 species diffusion with complete parent/four-child refinement and coarsening; five checkpoints per geometry |
| Gaussian reference | Independent initial fields in both curved geometries and all three dimensions; thermal on/off activity at matching time and topology |
| [Restart](../restart/README.md) | All four CPU/CUDA directions for smooth advection and ENUC-driven burning; intermediate post-regrid and terminal checkpoints |
| Geometry and transfer tests | Independent metric and diffusion references, origin balance, physical-volume restriction, composition, all coordinate directions and three state slots |

PPM uses MUSCL-MinMod at coarse/fine faces. Uniform-grid PPM convergence and
AMR-wide spatial accuracy are therefore measured separately. Spherical 2-D
uses the project's polar `(r,phi)` convention.

<a id="completed-checks-on-the-release-candidate"></a>

## Accepted CPU/CUDA checks

The [Cartesian application record](results/cartesian-native-20260907/release-872/backend-validation-evidence.json)
passes ten cases, 54 CPU/CUDA executions and 27 comparisons. The largest
absolute field difference is `1.332e-15`; the largest relative energy drift is
`3.730e-15`. The regrid-cycle case changes its leaf count from 6 to 7 to 9 to 8
at steps 1, 5, 10 and 20, exercising both refinement and coarsening.
All topology, field and conservation checks pass their original case budgets.

The [three-dimensional runtime record](results/dynamic-3d-final-20260907/release-919/evidence.json)
adds seven checkpoints through step 80 and fourteen CPU/CUDA executions.
It verifies complete eight-child refinement between steps 20 and 40, coarsening
from 40 to 41, and refinement again from 41 to 80. Each backend records thirteen
runtime topology changes, excluding initialization. The largest absolute field
difference is `4.235e-21`, within the original `rtol=2e-8`, `atol=2e-11` budget;
topology and conservation checks also pass. This completes the ordinary
three-dimensional application lifecycle check.

The three-dimensional [memcheck record](results/dynamic-3d-final-20260907/memcheck-914/evidence.json)
and [racecheck record](results/dynamic-3d-final-20260907/racecheck-915/evidence.json)
each pass all seven CUDA executions and seven CPU reference runs on the same
tested build. Every memcheck report records zero errors and zero leaked bytes or
allocations; every racecheck report records zero hazards, errors or warnings.
Both campaigns retain the complete eight-child refine/coarsen/refine transitions
and pass the original field and conservation budgets. In the racecheck
snapshots, the same six parents coarsen between steps 40 and 41 and have their
complete child sets again at step 80.

Cartesian conservation uses level-weighted sums, proportional to physical-volume
integrals by the root-cell volume. Its relative budget remains `2e-12`, with
absolute budgets `2e-11`, `2e-10` and `1e-9` for the one-, two- and three-dimensional
cases respectively. Field budgets remain those specified by each manifest case.

The [curvilinear application record](results/curved-native-20260907/release-873/backend-validation-evidence.json)
passes 24 cases, 96 CPU/CUDA executions and 48 comparisons, including thermal,
viscous and species coupling on mixed-level meshes. The largest absolute field
difference is `2.309e-14`. The largest relative physical-volume integral drifts
are `1.332e-15` for mass, `3.730e-15` for energy and `1.737e-15` for species.
Every comparison passes the unchanged budgets given below.
The two-dimensional RKL1 cases also record runtime refinement in both curved
geometries, with and without thermal/viscous coupling.

The coupled curved [memcheck record](results/curved-native-20260907/memcheck-904/backend-validation-evidence.json)
and [racecheck record](results/curved-native-20260907/racecheck-906/backend-validation-evidence.json)
each pass the spherical three-dimensional thermal/viscous/species case at steps
2 and 5. The two memcheck CUDA executions report zero errors and zero leaked
bytes or allocations; the two racecheck executions report zero hazards, errors
or warnings. The resolved CUDA route uses the same RK2, ideal EOS and five-stage
RKL2 selections as CPU, with no backend fallback. The largest absolute field
difference is `2.220e-14`; the largest relative energy drift is `6.626e-16`.
All original field, physical-volume conservation, RKL2 cache and transfer-trace
checks pass in both campaigns. These observations retain the 144 mixed-level
leaves created during initialization; runtime refine/coarsen coverage is
provided by the separate lifecycle records.

The [curved runtime lifecycle record](results/dynamic-curved-final-20260907/release-923/evidence.json)
adds cylindrical and spherical two-dimensional species-diffusion cases. Each
geometry samples steps 2, 5, 20, 80 and 160: ten CPU/CUDA executions per geometry,
twenty in total. Both verify complete parent/four-child refinement and coarsening.
At step 160, each backend records nine runtime topology changes per geometry,
excluding initialization; at most 64 leaves are observed under the unchanged
128-block capacity. The largest absolute field difference is `8.882e-16`.
Field comparisons retain `rtol=1e-8`, `atol=5e-12`; physical-volume conservation
retains `rtol=2e-12`, `atol=2e-11`. All original four-stage RKL1 checks pass.
This supplements the separate Cartesian three-dimensional lifecycle above.

The [Gaussian initialization and thermal-activity record](results/gaussian-final-20260907/release-871/evidence.json)
checks all six curved-coordinate initial states independently. Its largest
field error is `4.441e-16`, below `2e-12`. At matching time and topology, thermal
on/off controls produce a relative energy change of `1.058e-2` on each backend,
above the `256`-machine-epsilon activity threshold. This checks that thermal
transport changes the solution; convergence is evaluated separately.

These reports identify the same source, executable, comparator and
dependencies and verify that they remained unchanged during execution.
The independent geometry record below checks the same source separately.
Restart qualification is documented in the [restart guide](../restart/README.md),
and sustained AMR checks are summarized below.

The exchange tests cover Cartesian, cylindrical and spherical geometry in all
three dimensions, for `Current`, `Next` and `Scratch`.
Coarse/fine restriction uses physical-volume weights on both backends and the
same shared restriction functions. Invalid prolongation inputs are rejected
before destination fields are written.

## Independent geometry and diffusion checks

The [geometry record](results/geometry-native-20260907/release-880/evidence.json)
passes 12 thin-shell and near-pole references checked with independent 70/90-digit integrals.
Spatial refinement checks cover thermal/species diffusion and viscous
momentum/work fluxes in all coordinate systems and dimensions. The CPU and CUDA
test executables use the same tested source and are identified in the report.

Viscous tests use 30 profiles at three spacings per backend, including uniform
Cartesian velocity, quadratic fields, variable density and radial flow. The
largest finest-grid normalized error is `7.26751e-5`, within the `1e-4` budget.
Thermal/species tests include 54 paired samples (108 backend evaluations), with
maximum finest-grid absolute error `1.14163e-7` against a `1e-6` budget.
Scalar errors and viscous errors above `1e-10` meet the original requirement of
at least a 3.5-fold reduction when spacing is halved.

Each backend passes 18 origin-balance samples, six radial stability matrices
and nine matrices with density contrasts of 1, 10 and 100. The largest origin
residual is `2.93099e-14`, below `2e-11`. Euler-update matrix entries are
nonnegative and maximum row sums do not exceed one, within the original
`2e-12` roundoff allowance.

## Sustained regridding and continuation

The [sustained-execution record](results/sustained-first-law-20260907/release-901/evidence.json)
passes 500 one-dimensional hydro steps and 100 five-stage RKL2 diffusion steps
on both backends.
Including initialization, these runs record 501 and 101 regrid checks. Hydro
records 490 topology changes and explicit refinement and coarsening transitions;
diffusion retains six mixed-level leaves at its sampled checkpoints. The two
matrices contain 22 executions and eleven backend comparisons.

The largest absolute field differences are `1.510e-14` for hydro and
`1.776e-15` for diffusion. Hydro mass and energy relative drifts remain below
`2.931e-14` and `2.878e-14`; the largest diffusion species-integral drift is
`5.286e-15`. Conservation retains `rtol=2e-12`, `atol=2e-11` on the Cartesian
level-weighted sums described above. Field comparisons retain `atol=5e-12`,
with `rtol=5e-9` for hydro and `1e-8` for diffusion.

The same record passes twelve alternating smooth-advection restores and twelve
cycles in each CPU, CUDA and alternating-backend burn chain. Its 72 Host/CUDA
native burn-state replay comparisons have zero field and controller differences;
all seven prescribed-time burn comparisons also pass. The
[restart guide](../restart/README.md#sustained-native-restoration) separates exact
restoration, forward-step diagnostics and physical-time acceptance.

## Curvilinear application matrix

Both backends use [the same Gaussian input](inputs/gaussian_diffusion_amr.par)
and the scientific overrides declared in the manifest. Cylindrical and spherical
annuli/wedges use levels 0–1 and reflecting boundaries. Species-only cases have
uniform pressure and zero velocity. Coupled cases add a pressure pulse and
native velocity components, with thermal, viscous and species transport active.

RKL1 and RKL2 run to two and five accepted steps with hydro RK2 enabled and
regridding checked every step. The matrix checks mixed-level topology, both
Strang half-steps, four-stage RKL1 and five-stage RKL2. Separate records above
cover Cartesian three-dimensional and curved two-dimensional runtime
refinement/coarsening; restart continuity has its own suite.

Conservation uses physical-volume integrals of mass, energy and each `rhoX`.
Radial momentum is compared between backends but is not a global invariant:
geometry and wall forces contribute to its evolution. The fixed conservation
budgets are `rtol = 2e-12`, `atol = 2e-11`; field comparison uses
`rtol = 1e-8`, `atol = 5e-12`. The validator reads each run's actual parameters
to reconstruct physical volumes through the common grid-metric library.

## Reproduce the checks

Use a testing-enabled CUDA build with `ARCH`, `arch_cuda_single_level_validation`,
`arch_curvilinear_metrics` and `arch_cuda_curvilinear_geometry_smoke` available.
See the [build guide](../../README.md#build) for dependencies and memory-protected
compilation. Python checks require NumPy and h5py; independent geometry also
requires mpmath. Run from the repository root and choose new output directories:

~~~bash
python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json --source-root . \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --output-root validation/amr/results/my-cartesian-run

python3 validation/amr/geometry_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-geometry-run

python3 validation/amr/gaussian_reference.py \
  --build-dir build-cuda --output-dir validation/amr/results/my-initialization-run
~~~

For the curved matrix, use `validation/amr/gpu_curvilinear_cases.json` in the
first command and a separate output directory. The [restart guide](../restart/README.md)
describes the corresponding continuation checks.

To instrument the coupled three-dimensional curved case, point to an installed
Compute Sanitizer and use a new output directory. CPU remains the ordinary
reference; the CUDA runs retain the same numerical checks:

~~~bash
python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_curvilinear_cases.json --source-root . \
  --case spherical_coupled_amr_rkl2_wedge_3d \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --output-root validation/amr/results/my-curved-memcheck \
  --cuda-sanitizer /path/to/compute-sanitizer --sanitizer-tool memcheck
~~~

Repeat with `--sanitizer-tool racecheck` and another new output directory for
the corresponding race check. Instrumented timings are not performance benchmarks.

## Final-artifact qualification

Keep sources, build options, external tables, generated networks and executables
unchanged while collecting a qualification set. The shared evidence tools hash
these inputs before and after execution and retain plans, numerical metrics and
execution traces.

`tools/qualify_cuda_amr_evidence.py --profile full-runtime` checks that the
Cartesian, curved, uniform-grid and generated-network matrices and both restart
reports belong to one build and contain every required comparison. Independent
scientific, memory-safety and capacity checks are combined with these application
results in the [validation index](../README.md).
