# User boundaries: scientific and execution-cost acceptance

This record covers compiled physical/potential callbacks, their shared boundary
mathematics, AMR/stage ownership, portable restart, device safety and complete
application cost. The user interface is described in the
[boundary guide](../../../../docs/guides/UserBoundaries.md); implementation and
review details are in the [development record](../../../../docs/development/UserBoundaryImplementation.zh-CN.md).

## Scope and scientific criteria

Physical callbacks prescribe EOS-valid primitive states and enabled temperature,
velocity or species channels. Potential callbacks prescribe Dirichlet, outward
Neumann, coercive linear Robin or paired periodic faces. Both use fixed
same-directory source names and the existing two public headers. Cartesian,
cylindrical and spherical native geometries in one to three dimensions, coordinate
joins, AMR changes, stage times and CPU/CUDA continuation are covered.
The current 2D cylindrical/equatorial spherical conventions remain `(r,phi)`.

Independent mathematical checks cover manufactured potentials, force and Gauss
balance, the zero-mean gauge, operator invalidation and extreme finite-value
flux evaluation. The Neumann counterexample `PhiA=2^54, c=1` requires a gradient
of 1; the corrected shared scalar, packed Host and CUDA execution satisfy it.
An incompatible positive-mass/zero-Neumann source is rejected without subtracting
a hidden background. Existing analytic, residual and rounding budgets remain.

Runtime checks integrate exact native cell volumes independently of the solver.
They require finite states, positive density, zero recorded floor repairs, actual
RK/RKL surface mass/energy budgets, time-dependent heat-flux integration, final
field agreement and valid restart identity. Boundary mass closure uses the
existing `128*epsilon*step_count` scaled budget; backend field comparisons use
`rtol=2e-11, atol=2e-13`. Gravity energy exchange is between adjacent published
fields; it does not establish macro-step total-energy conservation. Native
curvilinear momentum is not a Cartesian global conservation diagnostic.

## Results

WSL Ubuntu, Intel i7-10700 (8 physical / 16 logical cores), RTX 3060 Ti;
CPU GCC 13.3, CUDA Host GCC 12.4 / Toolkit 12.3, strict-FP Release O3/LTO.
Application hashes and selected source hashes are retained in `summary.json`.
Records reused after the final Host surface-loop change retain their original
image identities and state which unchanged execution owners justify reuse.

| Check | Accepted evidence |
| --- | --- |
| CPU / CUDA CTest | 61/61 in 58.88 s; 125/125 in 180.26 s; full inventory, no skips |
| Tooling / architecture | 359/359 without skips; architecture check passes |
| CPU boundaries | 24 records covering native geometries, stages, budgets, AMR, restart and rejection |
| CUDA boundaries | 45 records, Host8: nine backend comparisons, two restart directions and rejection; science records equal Host1 |
| Existing gravity / selected four-module coupling | 42 accepted records; original execution identities retained |
| Device safety | 20 boundary memchecks, 14 racechecks, plus one region-lifecycle memcheck; clean reports |
| Exact transfer coverage | 1,500 layouts / 3,000 regions / 419,314 independent cell-membership checks; actual 1D/2D/3D padded device sentinels pass |
| Parallel Host transaction | Exact serial/four-thread ghost and control-plane parity; exceptions reject before scatter |

The CUDA burn policy inventory groups 15 route/status entries into five matrices,
retaining every subcase, fixture, helper, backend route, error budget and fresh
CUDA context. Total registrations change from 135 to 125. Before/after total
times were 184.45/184.68 s; the final 180.26 s does not establish a stable
wall-time improvement. The [coverage map](coverage-map.json) records each route.
No workflow trigger changes, long benchmark CI lane or external-solver dependency
were added.

## Complete application cost

Each lane reaches the same prescribed physical endpoint, with no accepted-step
cap. CPU thread selection uses 1/4/8-thread warmups; five subsequent repetitions
alternate lane order. Input physics, CFL, resolution, output settings, endpoint,
state/residual/floor gates and final-field budgets remain fixed within a case.
GPU compilation, profiling and sanitizer work were separate from timing; the
user confirmed other GPU load had ended. CPU/GPU ratios use one CUDA-enabled
executable. The separate pure-CPU regression pair uses GNU 13.3 builds of the
candidate and release commit `25adec4224497981a0c124a3485f786194975be4`.

| 64³ workload | CPU median (range), s | CUDA median (range), s | CPU / CUDA |
| --- | --- | --- | ---: |
| Built-in periodic self-gravity, `t=0.02 s`, 15 steps | 22.960 (22.562–23.692), CPU8 | 18.420 (18.336–18.628), Host1 | 1.247 |
| Physical/potential callbacks + heat diffusion, `t=0.0003 s`, 3 steps | 8.911 (8.885–10.033), CPU8 | 10.833 (10.565–11.436), Host8 | 0.823 |

The pure-CPU candidate/release medians are 22.459/22.793 s, ratio 0.985;
no sustained >5% built-in regression remains in this representative workload.
The short callback case still has negative whole-task benefit, despite Poisson
medians of 2.302/1.521 s (1.514x). It retains a checkpoint every step; output
medians are 0.327/1.131 s. Ordinary callbacks require Host evaluation and surface
round trips, so a positive result on built-in boundaries is not a guarantee for
this execution mode.

Earlier qualified callback medians were CPU8/CUDA Host1 8.893/20.446 s, then
8.665/12.316 s after exact cuboid transfers; these negative results and every
measurement are retained. Host parallelism accounts for part of the final gain
and is not a GPU kernel optimization. CUDA API profiling reports region-copy
submission cost falling from 254,464 2D calls / 7.681 s to 15,232 3D calls /
0.630 s. This WSL capture lacks the kernel/memory timeline; these are API
submission measurements, not GPU execution attribution.

## Reproduction and evidence

Use the current [gravity validation entry points](../../README.md) with new local
output directories. Formal cost commands from the repository root are:

```bash
python validation/gravity/check_cuda_compatibility.py \
  --cpu-arch bin/ARCH --cuda-arch build-cuda/bin/ARCH \
  --benchmark-only --benchmark-case large-periodic --benchmark-time 0.02 \
  --benchmark-repeats 5 --output output/periodic-cost
python validation/gravity/check_cuda_compatibility.py \
  --cpu-arch bin/ARCH --cuda-arch build-cuda/bin/ARCH \
  --benchmark-only --benchmark-case large-user-boundary --benchmark-time 0.0003 \
  --benchmark-cuda-host-threads 8 --benchmark-repeats 5 \
  --output output/user-boundary-cost
```

Choose executable paths from the actual build; presets normally use
`build-cpu/bin/ARCH`. An optional archived same-compiler CPU executable can be
supplied through `--baseline-cpu-arch` for the separate regression pair.

<details>
<summary>Processed machine-readable evidence</summary>

- [Acceptance, identities and all timing repetitions](summary.json)
- [Test coverage mapping](coverage-map.json)
- [Bounded worker reviews and conditional verification trial](delegation.json)

</details>

Raw HDF5, checkpoints, build caches, profiler databases and instrumentation logs
remain local. Racecheck establishes only its covered kernel hazard checks.
These short trajectories do not establish long-time scientific reliability;
that work follows the separately frozen validation plan.
