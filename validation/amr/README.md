# AMR conservation and refinement behavior

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

> CPU status: basic prolongation/restriction, dynamic regrid/reflux
> conservation, species transport, and two-dimensional symmetry pass. Local
> refinement retention is a known limitation. CUDA dynamic topology, device
> coarse/fine exchange, compact flux registration, and reflux are implemented
> and source/compile qualified; real-device validation is pending.

This record separates conservation from refinement efficiency. The current AMR
path preserves the tested integral quantities to roundoff, but its
piecewise-constant coarse-to-fine ghost fill can create a refinement indicator
at block interfaces and eventually refine a smooth periodic problem globally.
The latter behavior is recorded rather than hidden by a permissive threshold.

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

## Audit environment

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

## Refinement limitation

The smooth input deliberately uses `refine_threshold = 0.035`. Its topology
changes as follows:

| time | level-0 leaves | level-1 leaves |
| ---: | ---: | ---: |
| `0` | 4 | 2 |
| `0.00161450` | 3 | 4 |
| `0.00484349` | 2 | 6 |
| `0.00645799` | 1 | 8 |
| `0.00807248` | 0 | 10 |

The analytic Lohner indicator in the initially unrefined blocks is only about
`0.010--0.028`, below the threshold. A threshold of `0.05` produces no initial
refinement because the analytic maximum is about `0.037`. The current
coarse-to-fine face path in `GhostExchange::InterpolateFaceFromCoarse` copies a
coarse value into both fine ghost cells. A focused audit measured a fine-ghost
error of `3.44e-3` and interface indicators rising from
`0.00775/0.00954` to `0.06727/0.04230`. This explains the progressive global
refinement; there is no reliable threshold window for this case.

`AverageFaceFromFine` also averages mass fractions arithmetically instead of
volume-weighting `rho X`, and curved-coordinate face exchange does not yet use
physical-volume weights. These paths did not break the integral tests above,
but they prevent accepting local-refinement retention or curvilinear ghost
transfer as verified.

`ENUC` is persisted by checkpoint format v3 and participates in the implemented
Host and CUDA AMR transfer/exchange paths. The former missing-field limitation
therefore does not apply to new v3 checkpoints. Exact dynamic split-run
equivalence for `refine_var = ENUC` still needs CPU end-to-end and real-device
CUDA validation; legacy v1/v2 checkpoints initialize ENUC to zero and cannot
establish that equivalence.

The follow-up implementation should use limited-linear conservative-variable
reconstruction for coarse-to-fine ghosts, interpolate `rho X` before recovering
`X`, and volume-weight fine-to-coarse fluid and species states. Acceptance then
requires the smooth case to retain at least one level-0 leaf at `t = 0.01`.

## Reproduce

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform80.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_uniform160.par
./bin/ARCH SmoothAdvection validation/amr/inputs/smooth_amr80_l1.par
./bin/ARCH Sedov validation/amr/inputs/sedov_amr_species.par
~~~

The retained scalar results are in [metrics.csv](metrics.csv). Raw HDF5 output
is intentionally excluded from version control. The historic plots below remain
qualitative diagnostics and are not inputs to the acceptance decision.

| Historic case | Modules visible | Archive |
| --- | --- | --- |
| Sedov | hydro, shock-driven refinement | [figure](figures/legacy/sedov.png) |
| Gaussian | diffusion, moving refinement pattern | [figure](figures/legacy/gaussian.png) |
| Rayleigh--Taylor | hydro, gravity, diffusion | [figure](figures/legacy/rayleigh_taylor.png) |
| Cellular burn | hydro, burning | [figure](figures/legacy/cellular_burn.png) |

PPM uses MUSCL-MinMod at coarse/fine faces, so this record does not claim
third-order AMR-wide spatial convergence.
