# aprox13 one-zone burn

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

The results on this page refer to the scientific acceptance version identified
in the [central Validation index](../README.md); subsequent directory maintenance
and new-build checks are recorded separately in the
[maintenance record](../backend/results/maintenance-freeze-20260908/).

CPU and CUDA pass the original cross-solver and composition-closure checks.
The [current-candidate application record](results/application-first-law-20260907/release-878/evidence.json)
contains all six executions and their source/binary identities. The same
candidate also passes [native burn restart](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json).
Whole-project acceptance is tracked in the [validation index](../README.md).

The `BurnOneZone` implementation remains in `simulation/BurnOneZone/`; the
immutable parameter files owned by this record are in [`inputs/`](inputs/).
They run the production burn driver at
\(\rho=10^7\,\mathrm{g\,cm^{-3}}\), \(T=3\times10^9\,\mathrm{K}\), initial
`C12=0.5`, `O16=0.5`, and \(t=10^{-10}\,\mathrm{s}\). The strict BE_NR input
(`rtol=1e-10`, `atol=1e-14`) supplies an internal converged reference; BD and
ROS4 use `rtol=1e-6`, `atol=1e-10`. This is solver cross-verification, not an
independent physical validation of aprox13 rates.

## Helmholtz table identity

The only source used for this record is the `helm_table.dat` member of the
`helmholtz.tar.xz` package downloaded from the
[Timmes EOS website](https://cococubed.com/code_pages/eos.shtml). The runtime
path is `EOS_toolkit/tables/helmholtz/helm_table.dat`; Git LFS must
materialize it before the run.

| Property | Required value |
| --- | --- |
| Table shape | 541 density rows × 201 temperature columns |
| File size | 60,242,514 bytes |
| SHA-256 | `c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1` |

The loader requires all four blocks of the fixed 541×201 table and rejects
truncated or nonnumeric input. A Git LFS pointer file is not usable input.

## Reproduce

Use a testing-enabled build with `ARCH` and `arch_cuda_single_level_validation`,
and a Python environment with NumPy and h5py. Verify the table identity, then
run the archived recipe with a new output directory:

```bash
git lfs pull --include="EOS_toolkit/tables/helmholtz/helm_table.dat"
sha256sum EOS_toolkit/tables/helmholtz/helm_table.dat
python3 validation/burn/results/application-first-law-20260907/replay.py \
  --build-dir build-cuda \
  --output-dir validation/burn/results/application-new
```

Acceptance relative to BE_NR requires species Linf and relative total-energy
error at most \(10^{-8}\), plus abundance-sum residual at most \(10^{-12}\).
Across the 13 species, L1 is the mean absolute difference and L2 is the root
mean-square difference; Linf is the maximum absolute difference. Thermodynamic
quantities use \(\lvert q-q_{ref}\rvert/\lvert q_{ref}\rvert\).

| Backend | Solver | Species L1 | Species L2 | Species Linf | Relative energy | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPU | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | pass |
| CUDA | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | pass |
| CPU | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | pass |
| CUDA | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | pass |

The [metrics CSV](metrics.csv) also includes both BE_NR runs. Their CPU/CUDA
species Linf difference is 5.551e-16; the largest abundance-sum residual across
all six runs is 2.221e-16.

Both tested solutions close the abundance sum to roundoff. ROS4 uses the
matched four-stage L-stable coefficient set and one shared Jacobian matrix per
internal step. The stage equation and coefficient set follow the
[L-stable ROS4 formulation](https://link.springer.com/article/10.1007/s10915-023-02232-3)
and were cross-checked against the
[OpenFOAM Rosenbrock34 implementation](https://api.openfoam.com/2212/Rosenbrock34_8C_source.html).
This application record verifies one state and time interval; the independent
temporal and first-law controls below complement the cross-solver comparison.
Network data and implementation tests are described in the
[Timmes technical note](../../docs/physics/TimmesNetworks.md).

## Built-in temporal-accuracy controls

The four built-in networks also have immutable endpoints from independent
DOP853 time integration, cross-checked by Radau and a refined time-step ceiling.
The [current-candidate reference review](results/independent-time-final-20260907/release-888/evidence.json) checks all
four networks and independently evaluates endpoint energy with the existing
high-precision Helmholtz monomial-fit model. Reaction rates still come from the
shared ARCH RHS; this is independent **time integration**, not independent
nuclear-data validation. The sixteen reviewed trajectories cover two independent
integrators and two step ceilings for each network. The largest species Linf
difference is \(1.666\times10^{-16}\), relative temperature difference
\(2.121\times10^{-14}\), and independent endpoint EOS discrepancy
\(2.221\times10^{-16}\). Every first-law check passes its original budget.
The [complete Release regression](../backend/results/final-first-law-20260907/release-regression-895/evidence.json)
includes all twelve Host network/ODE controls, their negative controls and the
separate CUDA policy checks. [Built-in NSE application results](results/nse-application-native-20260907/release-890/evidence.json)
cover sixteen cases and thirty-two physical endpoints on the same candidate.

Build `arch_burn_mainline_reference` with testing enabled. Its ordinary invocation
checks all twelve network/ODE routes against these endpoints, with species Linf
and relative total-energy errors at most `1e-8`, and abundance closure `1e-12`.
Temperature uses the same dimensionless `1e-8` target; a separate heating-signal
check excludes a no-op for very small releases. The strict validation input is
`rtol=1e-13`, `atol=1e-17`, `max_substeps=3000000`; these are not application
defaults. A local integration tolerance is not a guaranteed global error bound.

For an independent, read-only review, run [time_reference.py](time_reference.py)
with `--binary <build>/arch_burn_mainline_reference --build-dir <build>` using
NumPy/SciPy/mpmath. It queries only the shared RHS/EOS, not ARCH's ODE algorithm
or Jacobian, and never refreshes fixtures. Use the common memory guard for runs.

The old main's method-dependent numerical snapshots remain unchanged as
historical data with their original checker/negative controls. They are no
longer treated as exact solutions after independently demonstrated Jacobian and
time-controller corrections. The short-step CUDA policy tests separately retain
their strict backend-parity budgets, complementing the reference and application
checks above.
