# aprox13 one-zone burn

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

Burning changes isotope abundances and exchanges nuclear binding energy with
thermal energy. A one-zone calculation isolates this reaction and temperature
evolution from fluid transport. The checks below compare time integrators,
composition constraints and energy balance; nuclear statistical equilibrium
(NSE) checks additionally compare the equilibrium state of the selected built-in
network with independent references.

Each detailed record identifies its tested source, executable and inputs.
The [validation overview](../README.md) brings together the module results.

CPU and CUDA pass the original cross-solver and composition-closure checks.
The application record
contains all six executions and their source/binary identities. The same
build also passes native burn restart.
Whole-project acceptance is tracked in the [validation index](../README.md).

The `BurnOneZone` implementation remains in `simulation/BurnOneZone/`; the
immutable parameter files owned by this record are in [`inputs/`](inputs).
They run the production burn driver at
$\rho=10^7\,\mathrm{g\,cm^{-3}}$, $T=3\times10^9\,\mathrm{K}$, initial
`C12=0.5`, `O16=0.5`, and $t=10^{-10}\,\mathrm{s}$. The strict BE_NR input
(`rtol=1e-10`, `atol=1e-14`) supplies an internal converged reference; BD and
ROS4 use `rtol=1e-6`, `atol=1e-10`. This is solver cross-verification, not an
independent physical validation of aprox13 rates.

## Current inputs and reference identity

The BE_NR, BD and ROS4 input files explicitly declare `min_eint=1e-10`,
`hll_wave_speed=roe` and `eos_coulomb_mult=1.0` for the current configuration
contract. [bd.par](inputs/bd.par) is also the shared controller and inspection
fixture. The density, temperature, initial composition, physical endpoint and
ODE tolerances retain the reference problem specified above.

Historical results retain their recorded source and input identities. Current
configuration loading alone does not establish a new scientific trajectory or CUDA
result. Replays use the recorded numerical/EOS controls and table identity;
longer physical endpoints need their own independent reference and budgets.

## Current CPU reliability check

The [one-zone CPU receipt](../backend/results/release-readiness/REL3CpuScienceAndReliability-20261014.json) records fresh
BE_NR, BD and ROS4 runs through the original one-zone endpoint and CPU
cross-solver/closure budgets. It preserves the historical records below and
does not qualify a new CUDA run.

The [CPU coupling and NSE record](../backend/results/release-readiness/REL3CpuScienceFollowup-20261014.json)
adds six BurnGradient combinations of the three ODE methods with RKL1/RKL2
species diffusion at the original `t=1e-10 s`. The original nuclear-mass energy,
mass and charge budgets of `1e-12`, pointwise state checks and actual AMR changes
pass. Sixteen application runs cover aprox13, aprox19, aprox21 and iso7, each
with an NSE-disabled BE_NR control and enabled BE_NR, BD and ROS4. They retain
the original `1e-12` energy/charge checks and composition contrast above `1e-3`.
These are CPU scopes; independent time references and new CUDA runs remain separate.

An earlier one-dimensional Cellular diagnostic explicitly restores eleven
former effective controls, including `use_nse=true`, and completes one step to
`t=1e-16 s` in 2.13 seconds of application wall time with zero repairs. This
input differs from the two-dimensional `CellularFlash2D.par` FLASH comparison.
The actual second burn half-step reports `NseSuccess`: one NSE attempt, no
NSE failure and no BD substep. Replaying its captured inputs through the same
shared BD/NSE implementation reproduces the packed state, report and energy
handoff bit for bit. This is a shared-code replay, not an independent NSE
reference. A separate independent Saha and Helmholtz-EOS audit at the actual
density, input composition and accepted temperature agrees at 60/80 digits.
Species Linf is `8.54e-15` and relative thermal-closure residual `2.28e-14`,
within the original `1e-12` limits. This audits the accepted output; it does
not measure the producer's internal Ye/old EOS energy or derive a unique
preferred thermal root. Pure-ODE integration does not represent this route.
The [current CPU receipt](../backend/results/release-readiness/REL3GeometryGravitySeparation-20261014.json)
extends this one-dimensional input to its original endpoint in 24 steps
(4.01 seconds, 25 checkpoints, zero repairs); its first saved state matches
the independently audited state exactly. It also qualifies six enabled-transport
endpoints and four active-ENUC continuations under the original budgets, after
repairing precision loss in the conservation observer without changing physics.
The original CPU/CUDA `enuc_rate` screening and the separate public/2D trajectories remain open;
the older cross-identity scalar difference does not close them. `ENUC`/`enuc_rate` has units
`erg g^-1 s^-1`: it is the last burn half-step's accepted energy increment
divided by that half-step's duration, not a whole-macro-step energy ledger.

## Helmholtz table identity

The only source used for this record is the `helm_table.dat` member of the
`helmholtz.tar.xz` package downloaded from the
[Timmes EOS website](https://cococubed.com/code_pages/eos.shtml). The runtime
path is `EOS_toolkit/tables/helmholtz/helm_table.dat`. Restore a matching,
verified local copy or materialize an existing local LFS object using the
[offline table guide](../../docs/guides/Build.md#offline-tables-and-lfs-bandwidth).
Keep a verified copy outside build/output directories and the LFS object store.
Report a missing object rather than automatically downloading it; reading a
materialized table uses no LFS bandwidth.

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
git lfs checkout EOS_toolkit/tables/helmholtz/helm_table.dat
sha256sum EOS_toolkit/tables/helmholtz/helm_table.dat
python3 validation/burn/results/application-first-law-20260907/replay.py \
  --build-dir build-cuda \
  --output-dir validation/burn/results/application-new
```

Acceptance relative to BE_NR requires species Linf and relative total-energy
error at most $10^{-8}$, plus abundance-sum residual at most $10^{-12}$.
Across the 13 species, L1 is the mean absolute difference and L2 is the root
mean-square difference; Linf is the maximum absolute difference. Thermodynamic
quantities use $\lvert q-q_{ref}\rvert/\lvert q_{ref}\rvert$.

| Backend | Solver | Species L1 | Species L2 | Species Linf | Relative energy | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| CPU | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | pass |
| CUDA | BD | 6.444e-13 | 1.385e-12 | 4.156e-12 | 2.450e-12 | pass |
| CPU | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | pass |
| CUDA | ROS4 | 6.462e-13 | 1.389e-12 | 4.168e-12 | 2.457e-12 | pass |

The metrics CSV also includes both BE_NR runs. Their CPU/CUDA
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
The independent reference review checks all
four networks and independently evaluates endpoint energy with the existing
high-precision Helmholtz monomial-fit model. Reaction rates still come from the
shared ARCH RHS; this is independent **time integration**, not independent
nuclear-data validation. The sixteen reviewed trajectories cover two independent
integrators and two step ceilings for each network. The largest species Linf
difference is $1.666\times10^{-16}$, relative temperature difference
$2.121\times10^{-14}$, and independent endpoint EOS discrepancy
$2.221\times10^{-16}$. Every first-law check passes its original budget.
The complete Release regression
includes all twelve Host network/ODE controls, their negative controls and the
separate CUDA policy checks. Built-in NSE application results
cover sixteen cases and thirty-two physical endpoints on the same tested build.

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

For a new physical endpoint, `--derive-reference --input <initial-state.json>` accepts explicit `network`, `rho`, `interval` and `state`. The state contains every registered mass fraction, followed by temperature. This uses the bound Helmholtz table and shared RHS to derive a reference for review; it never replaces old endpoints or qualifies a production trajectory. The new reacting C/O reference meets its preallocated cross-method and endpoint first-law budgets. Both step ceilings were inactive in this adaptive integration and do not establish active step refinement. The [processed record](results/release-closure-20261009/time-reference-summary.json) identifies inputs, independence and reference errors; actual long evolution/restart remains a separate gate.

Method-dependent numerical snapshots are retained as historical records with
their original checkers and negative controls. Accuracy is assessed with
independent references rather than treating these approximate endpoints as exact
solutions. The short-step CUDA policy tests separately retain
their strict backend-parity budgets, complementing the reference and application
checks above.

## Detailed verification records

<details>
<summary>Expand source identities, machine-readable data and execution logs</summary>

These data files support reproduction and independent review; they are not setup guides. Test methods, results and acceptance limits are explained above.

- [application record (JSON)](results/application-first-law-20260907/release-878/evidence.json)
- [native burn restart (JSON)](../amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json)
- [metrics CSV (CSV)](metrics.csv)
- [independent reference review (JSON)](results/independent-time-final-20260907/release-888/evidence.json)
- [complete Release regression (JSON)](../backend/results/final-first-law-20260907/release-regression-895/evidence.json)
- [Built-in NSE application results (JSON)](results/nse-application-native-20260907/release-890/evidence.json)

</details>

The active nonlinear endpoint and midpoint restart use `inputs/long_time_reference.par`: equal C/O fractions, rho=1e7, T0=3e9, physical endpoint1e-6 and midpoint checkpoint5e-7. Actual BD/ROS4 endpoints met the independent species/temperature/energy budgets; the BD restart retained20 numerical datasets bit for bit. See the [processed record](results/release-closure-20261009/production-summary.json). Local ODE tolerances do not bound global endpoint errors by themselves. This seconds-long run qualifies its physical endpoint and restart; wall-clock durability and long coupled trajectories have separate exits.
