# Tabular EOS HDF5 interface

Chinese translation: [TabularEOS.zh-CN.md](TabularEOS.zh-CN.md). The exact
runtime parameter and policy surface is indexed from
[`docs/Reference.md`](../../../docs/Reference.md). This file is the local,
versioned contract for table producers.

## Scope

`eos_type = tabular` is one policy with automatic 3D/4D dispatch. Its input
contract is ARCH's normalized, uniformly spaced HDF5 schema. Existing Shen, LS,
SFHo/HS, CompOSE, or EOSDriver tables enter this policy through converters that
map their variables, units, zero points, and composition coordinates. The C++
policy remains common to all converted table families.

`HelmEos` is a separate policy for the native fixed-layout Timmes
`helm_table.dat`.

## Upstream table families

The [official Shen releases](https://user.numazu-ct.ac.jp/~sumi/eos/) are main
tables over density, temperature, and proton/electron fraction. The
[CompOSE software](https://compose.obspm.fr/software/) reads its own general
purpose `(T, nB, Yq)` products and can export its own HDF5 layout.
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
likewise distributes Shen, LS, and HS-family `.h5` files in the EOSDriver
schema. ARCH represents these products as table families under the common
tabular policy. Format compatibility is determined by the normalized datasets
below, produced by a provenance-preserving converter.

## Rank detection

A new file must contain scalar integer `table_rank` equal to 3 or 4. Dispatch
uses this metadata first and verifies that it agrees with the composition axes.

For legacy ARCH files only, absent `table_rank` is inferred as follows:

- `n_X` and no `n_A`/`n_Z` means rank 3;
- both `n_A` and `n_Z` and no `n_X` means rank 4;
- incomplete or mixed axes are rejected as ambiguous.

Rank detection uses schema content rather than filenames or a table-name list.

## Common scalar datasets

| Dataset | Type | Meaning |
| --- | --- | --- |
| `arch_eos_version` | integer | recommended schema version; current writer uses 1 |
| `table_rank` | integer | required for new files: 3 or 4 |
| `thermodynamic_model` | UTF-8 string | `free_energy` or `direct`; absent means legacy `direct` |
| `n_rho`, `n_T` | integer | number of uniformly spaced thermodynamic nodes |
| `log_rho_min`, `log_rho_max` | float64 | base-10 density bounds, rho in g cm^-3 |
| `log_T_min`, `log_T_max` | float64 | base-10 temperature bounds, T in K |

Bounds must be finite and strictly increasing. The table includes both
endpoints, so `dlog_rho = (max-min)/(n_rho-1)` and similarly for temperature.

A rank-3 file additionally contains `n_X`, `X_min`, and `X_max`. Its optional
`composition_axis` is either `Ye` (also the default) or
`species:<registered-name>`. ARCH derives `Ye` from the active
`SpeciesManager`, or uses the named species mass fraction.

A rank-4 file additionally contains `n_A`, `A_min`, `A_max`, `n_Z`,
`Z_min`, and `Z_max`. ARCH derives Abar and Zbar from the active composition.

## Preferred free-energy model

Set `thermodynamic_model = free_energy` and store one float64 dataset:

| Rank | Dataset shape and C-order index |
| --- | --- |
| 3 | `free_energy[n_rho, n_T, n_X]` |
| 4 | `free_energy[n_rho, n_T, n_A, n_Z]` |

Values are the specific Helmholtz free energy `a` in erg g^-1. All values must
be finite. Both thermodynamic axes require at least five nodes.

Let `x = ln(rho)` and `y = ln(T)`. ARCH builds fourth-order finite-difference
fields through second order in each coordinate, including the mixed fields,
then applies a tensor-product quintic Hermite interpolant in `(x,y)`. It is C2
inside the covered thermodynamic domain. Composition interpolation remains
linear: one blend for rank 3, and successive Zbar/Abar blends for rank 4.

All returned quantities come from the same interpolated potential:

~~~text
P               = rho * a_x
e               = a - a_y
cv              = (a_y - a_yy) / T
(dP/dT)_rho     = rho * a_xy / T
(dP/drho)_T     = a_x + a_xx
(de/drho)_T     = (a_x - a_xy) / rho
(dP/drho)_e     = (dP/drho)_T - (dP/dT)_rho * (de/drho)_T / cv
(dP/de)_rho     = (dP/dT)_rho / cv
cs^2            = (dP/drho)_e + (dP/de)_rho * P/rho^2
Gamma1          = rho * cs^2 / P
~~~

Deriving all fields from one potential maintains the implemented thermodynamic
relations. Monotonicity across discontinuities and poorly resolved phase
boundaries remains outside the current interpolant's guarantees. A table is
rejected at query time if it produces non-positive `cv` or `cs^2`.

## Legacy direct model

Set `thermodynamic_model = direct`, or omit it only for an existing legacy
file. Store float64 `pressure`, `energy`, `sound_speed`, and `cv` with the exact
rank-dependent shape above. Optional `dp_drho` and `dp_dT` use the same shape.

The loader requires finite data, positive pressure/sound speed/cv, and energy
strictly increasing with temperature at fixed density and composition. Values
are vertex-interpolated (trilinear or quadrilinear); missing derivatives are
estimated locally. The direct path is the compatibility contract for upstream products without a
single Helmholtz-potential representation. New free-energy EOS tables use the
preferred model above.

## Resolution and acceptance

Table resolution is accepted per EOS through measured convergence. Curvature,
phase transitions, and source-table accuracy determine the required mesh. The
following rules define the initial spacing and acceptance process:

- spacing above 0.1 dex in `log10(rho)` or `log10(T)` emits a warning;
- 0.025 dex is the recommended starting point for a smooth physical domain
  padded by at least two table nodes on every thermodynamic side; this keeps
  production queries on the centered five-point derivative stencil;
- if the outermost table nodes are themselves part of the physical domain,
  0.0125 dex is the validated smooth-EOS starting point for sub-permille error;
- composition axes (`X`/`Ye`, Abar, and Zbar) use linear interpolation;
  determine their accepted spacing by axis halving;
- halve every axis spacing and compare `P`, `e`, `cv`, `cs`,
  `(dP/drho)_e`, `(dP/de)_rho`, temperature inversion, and the intended
  hydro/burn trajectory;
- sample cell interiors, vertices, and all outer boundaries;
- split or refine intervals near rapid curvature or phase boundaries; the
  current interpolant has no monotonicity limiter.

The maintained ideal-gas test spans two decades with 161 nodes on each
thermodynamic axis (0.0125 dex), samples all four thermodynamic corners, and
measures maximum relative error 3.61253e-4 for both rank-3 and rank-4
free-energy tables. At 0.025 dex the interior result was 7.98367e-4, while
including the one-sided outer boundaries raised it to 2.74774e-3. At 0.05 dex
the interior free-energy result was 6.58068e-3. The retained legacy 3D
direct/vertex path has a separate endpoint-inclusive 0.05-dex smoke result of
3.30779e-3. These regression values apply to the smooth analytic EOS used by
the test; nuclear-matter tables retain their own convergence requirements.

Run the checks with:

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

A converter for an external EOS must also document provenance, original units,
energy zero convention, lepton/photon contributions, composition definition,
valid domain, and its refinement/error report.
