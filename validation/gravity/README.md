# Gravity verification

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

The [P2 CPU Poisson record](results/p2-20260922/README.md) covers the standalone
uniform-grid field solver: periodic/Dirichlet analytic convergence, weak density
contrast, CGS scale invariance and failure handling. That P2 record alone does not establish the later production or GPU route.
Its fixed numerical decisions are recorded in the
[P2 contract](../../docs/development/P2PoissonMultigrid.zh-CN.md).

## Constant external gravity

The external-source tests below apply a prescribed acceleration; they do not solve
for the gravity produced by the fluid itself. The force should change momentum
and energy by the expected amounts without changing total mass. Coupled cases
check that this balance survives mesh refinement and species diffusion.

Each detailed record identifies its tested source, executable and inputs.
The [validation overview](../README.md) brings together the module results.

CPU and CUDA use the same external-gravity stage operator. Verification starts
from a periodic state with \(\rho=1\), \(p=1\), \(u=0\) and constant \(g_x=1\).
At \(t=0.1\), the exact solution has \(u=g_xt=0.1\), unchanged density and
pressure, and \(E=p/(\gamma-1)+\rho u^2/2\). Gravity supplies momentum and
energy; their changes are checked against this analytic solution, while mass
and passive species are conserved.

## Coupled AMR and species diffusion

The Release application record
passes three cases, 24 CPU/CUDA executions and twelve backend comparisons.
The endpoint audit
rechecks analytic source balance and mass/species conservation at all six
physical endpoints. Both reports identify the same source, executable,
comparator and dependencies and verify that they remained unchanged.

The [coupled manifest](coupled_cases.json) uses a one-dimensional Cartesian
mesh with a 64-cell root grid and one refinement level. A Gaussian passive-species
profile drives refinement while equal gas heat capacities and adiabatic indices
keep the density and pressure uniform. RK2 and RK3 run with gravity and AMR;
a third case adds RKL2 species diffusion to RK3, with five stages in each
diffusion half-step. Thermal and viscous diffusion are disabled in these cases.

The matrix checks mixed-level meshes after steps 1, 2 and 5 and compares the
physical solution at `t=0.1`. The gravity-only endpoints retain mixed levels;
the diffusion case coarsens to the root mesh by the endpoint.

| Coupled case | Velocity Linf | Pressure Linf | Energy Linf |
| --- | ---: | ---: | ---: |
| RK2 + AMR | `1.388e-17` | `4.441e-16` | `1.332e-15` |
| RK3 + AMR | `2.082e-16` | `1.998e-15` | `4.885e-15` |
| RK3 + RKL2 species diffusion + AMR | `6.384e-16` | `2.442e-14` | `6.084e-14` |

Values are the larger CPU/CUDA endpoint errors. Density error is zero, and
every analytic field error is below the original `1e-12` Linf budget.
The largest absolute backend field difference across the sampled comparisons
is `1.443e-15`, within `rtol=2e-10`, `atol=2e-12`.

Mass drift is zero; the largest relative species-integral drift is `2.153e-15`.
The original conservation budgets are `rtol=2e-12`, `atol=2e-11`.
These reports use level-weighted sums, proportional to physical-volume
integrals by the root-cell width on this Cartesian mesh. Relative drift is
unchanged by that normalization; the absolute budget applies to the recorded
sums. Species accuracy and diffusion convergence have separate
[diffusion records](../diffusion/README.md).

The same tested build also passes the uniform-grid source tests below. Overall
acceptance is tracked in the [validation index](../README.md).

## Reproduce the coupled checks

Use a testing-enabled CUDA build containing `ARCH` and
`arch_cuda_single_level_validation`. Run from the repository root with new
output directories:

```bash
export OMP_NUM_THREADS=4
python3 tools/validate_backend_results.py \
  --manifest validation/gravity/coupled_cases.json \
  --arch build-cuda/bin/ARCH \
  --checkpoint-validator build-cuda/arch_cuda_single_level_validation \
  --build-dir build-cuda --source-root . \
  --output-root validation/gravity/results/coupled-new
python3 validation/gravity/results/coupled-final-20260907/check_terminal.py \
  --build-dir build-cuda \
  --report validation/gravity/results/coupled-new/backend-validation-evidence.json \
  --output-dir validation/gravity/results/coupled-endpoints-new
```

The endpoint checker reads the saved initial and prescribed-time checkpoints,
recomputes the source and conservation checks, and verifies the application
report and input identities. Keep the same build and data for both commands.

## Uniform source test

The `ExternalGravity` problem in `simulation/ExternalGravity/` and the
immutable parameters in [`inputs/`](inputs/) isolate the gravity source on a
uniform mesh. The application record
reproduces the values below and in metrics.csv, with both backends
reaching the prescribed physical endpoint.

RK2 and RK3 use the same `1e-12` Linf budget for density, velocity, pressure
and energy. Density error is zero. Since the state is spatially uniform, each
listed Linf also equals its L1 and L2.

| Integrator (CPU and CUDA) | velocity Linf | pressure Linf | energy Linf | Result |
| --- | ---: | ---: | ---: | --- |
| RK2 | 0 | 2.220e-16 | 4.441e-16 | pass |
| RK3 | 8.327e-17 | 4.441e-16 | 8.882e-16 | pass |

To repeat only these uniform cases, use the application command above with
`--manifest validation/backend/cases.json` and
`--case external_gravity_rk2 --case external_gravity_rk3`, choosing a different
output directory. The coupled endpoint checker is specific to its own manifest.

These tests verify constant prescribed acceleration, not hydrostatic balance
or self-gravity. The retained Euler input exposes the expected first-order
source-energy error; independent temporal convergence is described in the
[hydro record](../hydro/README.md).

## Detailed verification records

<details>
<summary>Expand source identities, machine-readable data and execution logs</summary>

These data files support reproduction and independent review; they are not setup guides. Test methods, results and acceptance limits are explained above.

- [Release application record (JSON)](results/coupled-final-20260907/runtime-893/backend-validation-evidence.json)
- [endpoint audit (JSON)](results/coupled-final-20260907/endpoints-894/evidence.json)
- [application record (JSON)](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
- [metrics.csv (CSV)](metrics.csv)

</details>

## Production self-gravity

`gravity_type=self` runs composite-AMR Poisson solves on CPU and CUDA for Cartesian
periodic 1D–3D and isolated 3D domains. Tested isolated spherical/cylindrical
1D and full-azimuth 2D polar and 3D cylindrical/spherical domains, including
origin, axis and pole joins, also run on both backends. Hydro, burning and
thermal-diffusion combinations have been checked. CPU manufactured solutions,
independent boundary and Gauss checks, AMR coupling and restart are recorded in
the [P11/P12 record](results/p11-p12-20260923/README.md); CUDA analytic radial
checks, same-input curved four-module parity, bidirectional restart and local
performance are in the [P13 record](results/p13-20260924/README.md). Partial
azimuth domains, external mass and a Jeans-specific refinement indicator remain
unsupported for self-gravity. Start with [GravityBox](../../simulation/GravityBox/README.md)
for reusable inputs. The [P5–P7 acceptance](../../docs/development/P5P7GravityAcceptance.zh-CN.md)
records numerical, coupling, restart and device checks. The [P8–P10 1D radial record](results/p8-p10-20260923/README.md) preserves CPU elliptic and AMR evidence. The earlier
[P3/P4 record](../../docs/development/P3P4CompositeGravity.zh-CN.md) retains its CPU
periodic scope.

`run_self_gravity.py --arch <ARCH> --output <new directory>` checks Jeans waves,
energy, time order, dynamic AMR, restart, isolated boundaries, radial 1D
field/domain checks and selected coupling (numpy/h5py). `--quick` is the existing CTest analytic/rejection subset.
`arch_composite_poisson 3` checks three-dimensional uniform and composite
manufactured solutions; `contract` checks failure and hierarchy invariants.
`check_cuda_compatibility.py --cpu-arch <CPU> --cuda-arch <CUDA> --output <new directory>`
qualifies actual device gravity; `--benchmark-only` measures representative local workloads.

The [2D/3D four-module example](../../simulation/SNIaCoupled/README.md)
checks Hydro/self-gravity/burn/thermal-diffusion execution with AMR. Its original
[Cartesian CPU/CUDA smoke record](results/snia2d-20260923/README.md),
[P11/P12 CPU curved record](results/p11-p12-20260923/README.md) and
[P13 curved CUDA record](results/p13-20260924/README.md) do not constitute
analytic SN Ia validation. The P11/P12 record also reports a controlled comparison
against the user-provided FLASH 4.8 archive's Cellular case in 2D and 3D matched controls.
The archive contains local initialization extensions. The current
[comparison assessment](flash/O5OptimizationReport.zh-CN.md) records its
fixed-temperature burn, optional face-EOS work, workload differences and
remaining qualification gaps. The representative four-module route is
HLLC/MUSCL/MC + RK2 + RKL2 thermal + BD/DenseLU + MG + AMR with
Helmholtz/aprox13 and NSE disabled; this is not an all-policy coupling matrix.
