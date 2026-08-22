# Timmes C++ Nuclear Networks: Implementation, Validation, and Use

Chinese translation: [TimmesNetworks.zh-CN.md](TimmesNetworks.zh-CN.md).
The English file is the authoritative source text.

> Status: the CPU/OpenMP implementation has passed comparison against the
> original Fortran programs. This note records the source, numerical
> conventions, validation error, parallel model, and maintenance boundaries.

## 1. Source and implementation boundary

The `iso7`, `aprox13`, `aprox19`, and `aprox21` directories contain C++
translations of the classic compact Timmes nuclear-network Fortran packages
provided with the project. Reaction-rate formulas, forward/reverse relations,
nuclear data, screening corrections, approximate-equilibrium branches, RHS
assembly, and energy-release conventions follow the corresponding
`public_*.f90` source.

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
- physical acceptance compares C++ and original Fortran output at identical
  states with the same EOS table.

The numerical comparisons in Section 5 define the current physical baseline.

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

The self-heating temperature equation follows the Timmes implementation:

```text
dT/dt = enuc / cv
```

The Helmholtz EOS supplies `cv`. The complete Jacobian uses:

```text
J(i,j) = d(dX_i/dt) / dX_j
J(i,T) = d(dX_i/dt) / dT
J(T,j) = (d enuc / dX_j) / cv
J(T,T) = (d enuc / dT) / cv
```

The temperature column uses analytic rate derivatives. The temperature row
omits the quotient-rule terms `d(cv)/dX` and `d(cv)/dT`, matching the original
Timmes Jacobian.

Backward Euler with Newton-Raphson solves:

```text
LHS = I - dt * J
LHS * delta_U = U_old - U_k + dt * RHS(U_k)
```

The screened composition Jacobian uses the original frozen-screening
convention. Any change to screening-factor differentiation must update the
Jacobian, LHS, and validation baseline together.

## 5. Original-Fortran numerical comparison

Validation uses the real Helmholtz table at six states:

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

NSE projection solves:

```text
e_EOS(rho, T_new, X_NSE(T_new)) - e_old - enuc(X_old -> X_NSE) = 0
```

The fast path uses EOS `cv` followed by a secant slope; bounded bisection is the
fallback. A state is accepted only when composition conservation and relative
energy closure both reach `1e-12`. Failure preserves the original state and
returns to the ordinary ODE path.

Numerical validation records:

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

## 9. CPU validation status of BD and ROS4

ODE-dispatch regression uses the real Helmholtz table, all four Timmes networks,
and one 8 x 2 Cellular step. At `dt=1e-14 s`, BD completes all matrix sizes for
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
Helmholtz/aprox13 one-zone regression, ROS4 relative to the strict BE_NR
reference has species Linf `3.281e-11` and relative total-energy error
`1.517e-11`, passing the current `1e-8` criterion. Inputs, table identity, and
metrics are recorded in the [burn validation](../../validation/burn/README.md).
New networks and production states still require step-size and tolerance
convergence with species and energy trajectory comparison.

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

Generated pynucastro packages are documented in the [custom-network local contract](../../src/physics/network/custom/README.md) and the [Reference](../Reference.md). `Auto` resolves to the dedicated DenseLU backend through 30 isotopes and to SuiteSparse KLU above 30. Generated packages set `SUPPORTS_NSE=false`. Their production qualification covers solver tolerances and composition/energy trajectories.

CMake requires OpenMP through `find_package(OpenMP REQUIRED)`. Control the
runtime thread count with an environment variable, for example:

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
4. **Required revalidation:** changes to rates, species order, energy weights,
   EOS `cv`, or ODE projection logic require the four-network Fortran
   RHS/Jacobian/LHS comparison and hexadecimal microphysics baseline, with a
   maximum relative-error threshold below `1e-12`.

Last updated: 2026-08-20.
