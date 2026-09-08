# Constant external gravity

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

The results on this page refer to the scientific acceptance version identified
in the [central Validation index](../README.md); subsequent directory maintenance
and new-build checks are recorded separately in the
[maintenance record](../backend/results/maintenance-freeze-20260908/).

CPU and CUDA use the same external-gravity stage operator. Verification starts
from a periodic state with \(\rho=1\), \(p=1\), \(u=0\) and constant \(g_x=1\).
At \(t=0.1\), the exact solution has \(u=g_xt=0.1\), unchanged density and
pressure, and \(E=p/(\gamma-1)+\rho u^2/2\). Gravity supplies momentum and
energy; their changes are checked against this analytic solution, while mass
and passive species are conserved.

## Coupled AMR and species diffusion

The [Release application record](results/coupled-final-20260907/runtime-893/backend-validation-evidence.json)
passes three cases, 24 CPU/CUDA executions and twelve backend comparisons.
The [endpoint audit](results/coupled-final-20260907/endpoints-894/evidence.json)
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

The same candidate also passes the uniform-grid source tests below. Overall
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
uniform mesh. The [current-candidate application record](../backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json)
reproduces the values below and in [metrics.csv](metrics.csv), with both backends
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
