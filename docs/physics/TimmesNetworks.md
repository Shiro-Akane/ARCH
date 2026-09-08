# Timmes C++ Nuclear Networks: Implementation, Validation, and Use

Chinese translation: [TimmesNetworks.zh-CN.md](TimmesNetworks.zh-CN.md).
The English file is the authoritative source text.

A nuclear reaction network follows the amounts of selected isotopes and the
energy released or absorbed as reactions occur. The reaction-rate equations
provide time derivatives, often called the right-hand side (RHS). An ordinary
differential equation (ODE) solver advances them through time. Nuclear
statistical equilibrium (NSE) instead finds an equilibrium composition within
the network's isotope set when that model is selected.

This is the implementation and provenance note for contributors and users
choosing a burn model. For a first simulation, begin with the
[case guide](../guides/SimulationCase.md); use the sections below when you need
species ordering, solver conventions or the reference comparisons.

The four built-in networks share their reaction, ODE and NSE mathematics on
CPU and CUDA. Current validation includes independent time-integration and
energy checks, together with coupled NSE application tests. The original
Fortran comparisons are retained as translation records.

## 1. Source and implementation boundary

The `iso7`, `aprox13`, `aprox19`, and `aprox21` directories contain C++ translations of the classic, compact Timmes nuclear-network Fortran packages bundled with this project. We rigorously follow the corresponding `public_*.f90` source files for all reaction-rate formulas, forward/reverse relations, core nuclear data, screening corrections, approximate-equilibrium branches, RHS assembly, and energy-release conventions.

Provenance is recorded per file rather than inferred from directory placement.
Translated formulas and generated equations identify their Timmes source;
ARCH-authored adapters such as `Dual.h`, `RatePair.h`, and
`TimmesNetworkSupport.h` identify themselves as support code. The official
[reaction-network page](https://cococubed.com/code_pages/burn.shtml) requests
appropriate citation but does not state a standard SPDX license. Redistribution
scope and the EOS/NSE source links are recorded in
[`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).

The runtime contract is:

- network code lives in `src/physics/network/` and is compiled directly as C++;
- the runtime has no Fortran, Python, or pynucastro dependency;
- Helmholtz EOS runs require an actual Helmholtz table selected by the parameter file;
- reaction-network compatibility compares C++ and original Fortran output at
  identical states with the same EOS table.

Section 5 preserves the original translation baseline. Current time-integration
and thermodynamic acceptance are recorded in [burn validation](../../validation/burn/README.md).

## 2. Networks and species order

Species order is a fixed ABI for state arrays, HDF5 output, and Jacobian rows
and columns. Changing it requires an explicit data migration and new baselines.

| Network | Species | ODE equations, including temperature | Species order |
| --- | ---: | ---: | --- |
| `iso7` | 7 | 8 | He4, C12, O16, Ne20, Mg24, Si28, Ni56 |
| `aprox13` | 13 | 14 | He4, C12, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Fe52, Ni56 |
| `aprox19` | 19 | 20 | H1, He3, He4, C12, N14, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Fe52, Fe54, Ni56, neutron, proton |
| `aprox21` | 21 | 22 | H1, He3, He4, C12, N14, O16, Ne20, Mg24, Si28, S32, Ar36, Ca40, Ti44, Cr48, Cr56, Fe52, Fe54, Fe56, Ni56, neutron, proton |

## 3. Source layout

- `timmes_common/`: shared nuclear constants, temperature factors, dual-number
  automatic differentiation, screening, rate assembly, and RHS/Jacobian support;
- `<network>/TimmesRateLibrary.h`: reaction-rate formulas and temperature
  derivatives corresponding to the original Fortran;
- `<network>/TimmesRhs.inc`: RHS composition for the original network equations;
- `<network>/Net*.h`: species tables, network interface, screening and branch
  logic, and energy conventions;
- `aprox13/TimmesJacobian.inc`: mechanically extracted and adapted composition
  Jacobian from the explicit Jacobian section of `public_aprox13.f90`.

Each network exposes three interfaces to the burn solvers:

1. `eval_rhs`: mass-fraction RHS and nuclear-energy generation rate;
2. `eval_jacobian`: analytic composition-to-composition Jacobian;
3. `eval_temperature_derivative`: analytic temperature derivatives of the
   species RHS and energy-generation rate.

## 4. Temperature equation, Jacobian, and LHS conventions

The ODE state is

```text
U = [X_1, X_2, ..., X_N, T]^T
```

During the fixed-density burn substep, the selected EOS supplies specific
internal energy `e(rho,T,X)` and `cv`. With species rates `f_i=dX_i/dt` and
net specific heating `enuc`, the first law gives:

```text
f_T = dT/dt = (enuc - sum_i e_i * f_i) / cv
e_i = (partial e / partial X_i)_(rho,T)
```

Composition changes can change EOS energy even at fixed temperature, so that
energy change must be included in the thermal equation. For any state variable
`U_j`, the complete thermal Jacobian row is:

```text
J(i,j) = d(dX_i/dt) / dX_j
J(i,T) = d(dX_i/dt) / dT
J(T,j) = (partial_j enuc - sum_i e_i * J(i,j)
          - sum_i (partial_j e_i) * f_i - f_T * partial_j cv) / cv
```

All three ODE methods and both backends use this assembly. Ideal, Helmholtz and
tabular EOS views provide analytic thermal/composition derivatives; a common
numerical adapter supports other duck-typed EOS views. Reaction-rate derivatives
keep their network's declared screening convention. Generated weak networks
also carry a signed energy-source integral, used with the accepted composition
change when handing energy back to hydrodynamics.

For each accepted substep, the solver contracts its composition increment with
the network's nuclear-energy weights and adds any signed external-source
increment. These contributions accumulate into the specific energy change
`delta_e`. Hydrodynamics adds `rho * delta_e` to conserved energy and reports
`ENUC = delta_e / dt`; its burn time limiter uses that same integral. The
calculation uses the solver increments before rounding them into the endpoint
state, so small heat releases remain measurable against a large thermal
background. Rejected trials contribute no energy. The final EOS query checks
the thermodynamic state, and NSE supplies the energy of its accepted projection.
CPU and CUDA use the same accounting.

Backward Euler with Newton-Raphson solves:

```text
LHS = I - dt * J
LHS * delta_U = U_old - U_k + dt * RHS(U_k)
```

The screened composition Jacobian uses the original frozen-screening
convention. Any change to screening-factor differentiation must update the
Jacobian, LHS, and validation baseline together.

## 5. Original-Fortran numerical comparison

The historical translation comparison used the original thermal convention
and real Helmholtz table at six states:

```text
T   = 1e9, 2e9, 5e9 K
rho = 1e6, 1e8 g cm^-3
```

Each value below is the maximum relative error over all tested states and
relevant components. The acceptance threshold is `1e-12`.

| Network | RHS maximum relative error | Jacobian maximum relative error | LHS maximum relative error | Helmholtz cv maximum relative error | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| `iso7` | 2.383748949565e-15 | 2.940236373588e-15 | 2.856278354823e-15 | 5.705278821529e-16 | PASS |
| `aprox13` | 3.596869184109e-14 | 2.850246815720e-13 | 2.850309327978e-13 | 1.331631293002e-15 | PASS |
| `aprox19` | 9.991734850962e-15 | 5.467476955907e-15 | 5.492012250384e-15 | 3.753488177566e-16 | PASS |
| `aprox21` | 3.997143540823e-14 | 1.092264838562e-14 | 1.091781737534e-14 | 3.947012873172e-16 | PASS |

The largest discrepancy is the `aprox13` LHS error of
`2.850309327978e-13`, about 3.5 times below the threshold. The remaining
difference is attributable primarily to Fortran/C++ floating-point evaluation
order. These six states do not replace long-time conservation, convergence,
and resolution studies.

## 6. Online NSE solver

The high-temperature, high-density path uses `NSESolver<NetType>` in
`src/physics/nse/nse_solver.h`. Frank Timmes's `public_nse.tbz` contains a
47-species online Fortran solver. The C++ implementation translates its Saha
equations, mass and charge residuals, and 2 x 2 Newton-Raphson Jacobian while
replacing fixed species arrays with compile-time network data.

The solver reads only `NUM_SPECIES`, `AION`, `ZION`, `BINDING_E`, `SPIN`, and
the energy-conversion factor from `NetType`. `BINDING_E` is the total binding
energy per nucleus in MeV; a mass excess must be converted first. `X_old` and
`X_out` both contain mass fractions. Energy closure uses molar abundance
`Y_i = X_i/A_i` internally.

At fixed temperature and density, the equations are:

```text
X_i = A_i/(N_A rho) * G_i * (2 pi A_i m_u kT / h^2)^(3/2)
      * exp(((A_i-Z_i) mu_n + Z_i mu_p + B_i) / kT)

sum_i X_i = 1
sum_i (Z_i/A_i) X_i = Ye
```

Numerical safeguards include:

- dimensionless chemical potentials and log-sum-exp evaluation to avoid Saha
  exponential overflow and underflow;
- an analytic 2 x 2 Jacobian, Cramer's rule, step limiting, and backtracking;
- a bounded one-dimensional fallback that fixes `eta_p-eta_n`, solves mass
  normalization, and then solves the charge chemical potential when proton or
  neutron boundaries make the 2 x 2 Jacobian ill-conditioned;
- at most 100 Newton iterations, a `1e-12` residual target, and a final mass and
  charge conservation check;
- `omp simd` species loops with solver state local to each call, allowing an
  enclosing OpenMP cell loop;
- degeneracy handling for `iso7` and `aprox13`, whose species all have
  `Z/A=0.5`: the solver fixes `mu_n=mu_p` and solves one normalization equation,
  so constrained NSE exists only at `Ye=0.5`;
- exclusion of the duplicate `h1` bookkeeping entry from NSE statistics in
  `aprox19` and `aprox21`; the equilibrium free-proton abundance is stored in
  `prot`, preventing a duplicated proton degeneracy.

The result is network-constrained NSE over the selected species set. `iso7` and
`aprox13` omit free nucleons and neutron-rich nuclei and support only the
`Ye=0.5` constrained solution. Full physical NSE requires an independent,
sufficiently broad species set and conservation mapping.
CPU and CUDA use the same solver for the four built-in networks and eligible
generated packages. Generated data preserve pynucastro's mass and
detailed-balance convention instead of borrowing Timmes's constants. The
[package contract](../../src/physics/network/custom/README.md) defines eligibility
and the current unscreened, weak-free, ground-state model boundary.

`use_nse=auto` resolves network capability at startup. True and auto share
`T > nseTempThreshold` and `rho > nseDensThreshold`, with unchanged defaults
`4.5e9 K` and `1e6 g/cm^3`. No additional dynamic activation criterion is
introduced. Both modes still require valid nuclear/EOS data and a conserving
thermal solution; a solution below the threshold is not forced back onto it.

NSE projection solves:

```text
e_EOS(rho, T_new, X_NSE(T_new)) - e_old - enuc(X_old -> X_NSE) = 0
```

The fast path uses EOS `cv` followed by a secant slope; bounded bisection is the
fallback. A state is accepted only when composition conservation and relative
energy closure both reach `1e-12`. Failure preserves the original state and
returns to the ordinary ODE path.

Before iterating, the solver checks whether the input already satisfies the
Saha relations and mass/charge constraints at these same tolerances. Such a
state is preserved exactly, so repeated equilibrium projection does not create
heat from roundoff. Changes in temperature, density or composition that violate
the equilibrium residuals trigger the ordinary nonlinear solve.

The current [NSE application validation](../../validation/burn/results/nse-application-native-20260907/release-890/evidence.json)
passes sixteen one-zone cases across all four built-in networks, with
thirty-two CPU/CUDA endpoints at the prescribed physical time. Each network
exercises NSE through BE_NR, BD and ROS4, alongside an NSE-disabled BE_NR
control. The tests check backend agreement, positive states, species
normalization and a resolved composition change when NSE is enabled. An
independent binding-energy balance checks the energy returned to the fluid;
relative energy closure and absolute charge drift both meet `1e-12`.

The following historical comparisons document the original implementation
and its numerical conventions:

- against a strict-residual copy of the 47-species `public_nse.f90`, eight
  states over `T=2.5e9--1e10 K`, `rho=1e6--1e9 g cm^-3`, and
  `Ye=0.47--0.55` have a maximum species mass-fraction absolute error of
  `4.897193761622e-13`;
- all 48 four-network combinations at `T=4.5e9, 5e9, 7e9, 1e10 K` and
  `rho=1e6, 1e7, 1e9 g cm^-3` converge with `sum(X)` and `Ye` within `1e-12`;
- separate 84-state scans for each of `aprox19` and `aprox21` over
  `Ye=0.40--0.60`, `T=4.5e9--1e10 K`, and `rho=1e6--1e10 g cm^-3` have a
  worst mass-normalization error of `6.49e-16` and charge-conservation error
  of `6.98e-13`;
- `aprox19` with the real Helmholtz EOS at `rho=4.322e7 g cm^-3` and
  `T_old=4.67e9 K`, starting from He4/C12, gives
  `T_new=6.7610109665e9 K` and relative energy-closure residual
  `7.5678598227e-14`;
- a 64 x 16 Cellular-driver high-temperature step completes for species counts
  7, 13, 19, and 21; the `aprox19` OpenMP 1-thread and 16-thread HDF5 outputs
  contain the same 26 datasets and are bitwise identical.

## 7. OpenMP parallel model

The CPU target requires OpenMP by default:

```text
ARCH_ENABLE_OPENMP=ON
OpenMP_CXX_FLAGS=-fopenmp
```

Parallelism is organized as follows:

- the outer grid-cell loop uses `parallel for schedule(dynamic, 1)`;
- every cell owns independent ODE state, network temporaries, and LU matrices;
- suitable network, ODE, Jacobian-assembly, and dense-LU loops use `omp simd`;
- no nested OpenMP team is created for an individual 8--22 order matrix,
  because scheduling would cost more than the matrix operation.

For a three-step 64 x 16 `aprox13` burn at `3e9 K` with NSE disabled, every
output from steps 0--3 contains the same 20 HDF5 datasets for 1 and 16 threads,
and all values are bitwise identical. Initially absent Ne20 is produced, so the
test exercises the network/ODE/LU path rather than pure hydrodynamics or NSE.
The high-temperature NSE path has a separate bitwise 1/16-thread comparison
with `aprox19`.

## 8. Microphysics and transport-coefficient interface

The transport mathematics lives in
`src/physics/diffusionCoe/diffusion_math.hpp`. Inputs are density, temperature,
electron pressure, and electron degeneracy from `eos_state_t`, plus
`Aion^-1` and `Zion` from `SpeciesManager`. The call order is:

```text
HelmEos::evaluate
  -> extract EOS state and network species properties
  -> ConductivityMath::compute_stellar_conductivity
```

The [diffusion-coefficient alignment record](DiffusionCoefficientAlignment.md)
reports hexadecimal floating-point comparison for more than 10,000 randomized
`aprox19` states. Maximum absolute and relative errors are both `0.0` over the
tested range. Section 5 separately covers network RHS, Jacobian, and LHS paths.

## 9. Historical CPU solver comparisons

An earlier ODE-dispatch regression used the real Helmholtz table, all four Timmes
networks, and one 8 x 2 Cellular step. At `dt=1e-14 s`, BD completed all matrix sizes for
`iso7`, `aprox13`, `aprox19`, and `aprox21`; every output is finite and
`max|sum(X)-1| <= 2.2204e-16`. In the `aprox13` BD-to-BE_NR comparison,
relative energy difference is about `3.51e-15`, relative pressure difference
is about `7.34e-15`, and the principal C12/He4 species differ by no more than
`1.23e-15`. Trace species near zero are reported with both absolute and
relative errors.

BD guards its highest-order early-exit lookup with `k + 1 < MAX_K` before
accessing `n_seq[k+1]`. `BE_NR`, `BD`, and `ROS4` are selectable through
`ode_solver`. ROS4 uses a matched four-stage L-stable coefficient set and
constructs and factors `I - gamma*dt*J` once per internal step. In the
Helmholtz/aprox13 one-zone comparison, ROS4 relative to an internally converged
BE_NR run had species Linf `3.281e-11` and relative total-energy error
`1.517e-11`, passing that record's `1e-8` criterion. Inputs, table identity, and
metrics are recorded in the [burn validation](../../validation/burn/README.md).
Section 12 describes current independent time-integration acceptance. New
networks and production states require step-size and tolerance convergence
with species and energy trajectory comparison.

## 10. Usage

Select a network in the parameter file:

```text
use_burn = true
network_name = iso7       # or aprox13 / aprox19 / aprox21 / custom:<id>
use_nse = true               # enabled by default; set to false to disable online constrained NSE
nseTempThreshold = 4.5e9
nseDensThreshold = 1.0e6
ode_solver = BE_NR
linear_solver = Auto
eos_type = helmholtz
eos_table_path = /absolute/path/to/helm_table.dat
```

Generated pynucastro packages are documented in the
[custom-network local contract](../../src/physics/network/custom/README.md)
and the [Reference](../Reference.md). Linear-solver requests are
case-insensitive. `Auto` selects DenseLU through 31 total ODE equations,
including temperature and any auxiliary energy state. Larger systems use
SuiteSparse KLU on CPU or cuDSS on CUDA. SparseKLU is
CPU-only and cuDSS is CUDA-only, with incompatible explicit pairs rejected
before backend construction rather than substituted. The optional cuDSS 0.8
provider is discovered through CMake/`CUDSS_ROOT` and enabled only when linked
with the registered network/EOS routes. Missing providers fail closed.

A generated package registers for device execution when it provides
device-callable math and passes the
[package metadata checks](../../src/physics/network/custom/README.md).
CPU and CUDA consume the same math header, constants, and declared Jacobian
structure. Recognized embedded weak tables have backend-owned immutable storage
and explicit borrowed views. Accepted packages without a device-callable math
contract execute on CPU only. CUDA sparse execution reuses
the shared BE_NR/ROS4/BD continuations with a backend-specific CSR/cuDSS
executor, not a second set of network or ODE physics. Generated packages
advertise NSE only after the documented data and equilibrium-model checks.
Recognized weak networks remain ordinary-ODE cases and integrate their signed energy
source with the same ODE stages, error control and rollback as composition and
temperature. Nuclear energy and that source integral enter the common
accepted-energy accounting.
The [network validation](../../validation/network/README.md) records distinguish
generated math, solver compatibility, independent scientific trajectories and
whole-application coverage. A model's scientific reliability depends on its
isotope set, reaction data and range of applicability.

OpenMP is enabled by default and can be disabled with
`-DARCH_ENABLE_OPENMP=OFF`. For an OpenMP build, control the runtime thread count
with an environment variable, for example:

```bash
OMP_NUM_THREADS=16 <build-dir>/bin/ARCH CellularDet case.par
```

Startup output reports the resolved `Species Count` and
`OpenMP: ON (max threads=...)` for configuration auditing.

## 11. Physical and maintenance boundaries

1. **Transport order:** `HelmEos` fills `eos_state_t`, `SpeciesManager` supplies
   `Aion^-1` and `Zion`, and `diffusion_math` evaluates transport.
2. **Weak rates and electron chemical potential:** weak-rate entry points in
   `aprox19` and `aprox21` that depend on Helmholtz `eta_e` currently return
   zero. Enabling them requires validated rates and an explicit interface map.
3. **Network-constrained NSE:** online Saha NSE uses the selected compact
   network. `iso7` and `aprox13` support only the `Ye=0.5` constrained solution;
   full NSE requires an independent species set and conservation mapping.
4. **Required revalidation:** rate/species/energy-data changes require the
   corresponding original-Fortran network comparisons. EOS or integration
   changes require independent thermodynamic, time-convergence and energy-
   closure checks, followed by CPU/CUDA application validation. Retain the
   historical thermal/LHS comparison separately from the current first-law model.

## 12. Integration accuracy and verification

The shared first-law equation in Section 4 is tested with analytic reaction
and EOS models, including composition-dependent energy and heat capacity.
The controls check energy rate, the complete Jacobian and fourth-order ROS4
convergence on CPU and CUDA. The matched
[KPP ROS4 formulation](https://kpp.readthedocs.io/en/stable/num_methods/rosenbrock-methods.html)
uses that RHS Jacobian. Independent DOP853/Radau trajectories provide a separate
time-integration reference for the built-ins and the tabulated Urca network.

The current [independent built-in review](../../validation/burn/results/independent-time-final-20260907/release-888/evidence.json)
passes all four networks with both DOP853 and Radau, each at two maximum
time-step sizes: sixteen independent trajectories in total. It queries the
shared reaction/EOS RHS without using ARCH's ODE algorithm or Jacobian.
Separate 60- and 80-digit Helmholtz evaluations check endpoint energy, while
independent quadrature checks the first-law balance. The review validates
time integration and thermodynamic consistency; reaction-data validation
remains tied to the cited nuclear data and original network comparisons.

Historical method-dependent endpoint snapshots remain unchanged as regression
records. Current integration accuracy is judged against the independently
integrated references, not by treating those older solver outputs as exact
solutions.

BE_NR estimates local time error separately from Newton convergence. An
exact linear/Newton solve alone cannot establish integration accuracy. The
shared controller reduces error when the tolerance is tightened, as checked
by the analytic tolerance-refinement control.

These ODE changes do not replace reaction rates, EOS tables, NSE, the accepted
composition-energy definition or physical closure budgets. Generated-network
energy now removes a common conserved baryon mass before summation, using only
the emitted mass data and validated reaction stoichiometry. This changes the
energy reference without changing the nuclear-mass convention. The
[burn](../../validation/burn/README.md) and
[network](../../validation/network/README.md) validation records distinguish
these scientific checks from application, sustained and sanitizer acceptance.

Project-owned analytic EOS, transport and NSE constants use one SI/CODATA 2022
set shared by both backends. Timmes reaction data and generated pynucastro
assets keep their own declared data conventions. Reference calculations must
use the corresponding constants and data; older hexadecimal snapshots retain
their historical context. See the
[constants authority](../../src/physics/constant/README.md).

Last updated: 2026-09-07.
