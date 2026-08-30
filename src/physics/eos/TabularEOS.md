# Tabular EOS HDF5 interface

Chinese translation: [TabularEOS.zh-CN.md](TabularEOS.zh-CN.md). The exact
runtime parameter and policy surface is indexed from
[`docs/Reference.md`](../../../docs/Reference.md). This file is the local,
versioned contract for table producers.

## Scope

`eos_type = tabular` is one policy with automatic 3D/4D dispatch. Its input
contract is ARCH's normalized, uniformly spaced HDF5 schema. A source-specific
converter may map Shen, LS, SFHo/HS, CompOSE, or EOSDriver variables, units,
zero points, and composition coordinates into this contract. ARCH does not
currently ship such a converter, and an upstream filename or HDF5 container is
not evidence of compatibility. The C++ policy remains common to tables that
have been converted and independently accepted.

`HelmEos` is a separate policy for the native fixed-layout Timmes
`helm_table.dat`.

## Upstream table families

The [official Shen releases](https://user.numazu-ct.ac.jp/~sumi/eos/) are main
tables over density, temperature, and proton/electron fraction. The
[CompOSE software](https://compose.obspm.fr/software/) reads its own general
purpose `(T, nB, Yq)` products and can export its own HDF5 layout.
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
likewise distributes Shen, LS, and HS-family `.h5` files in the EOSDriver
schema. These are candidate source families, not files that the current loader
can open directly. Format compatibility is determined only by the normalized
datasets below and a provenance-preserving, family-specific conversion report.

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
| `arch_eos_version` | integer | optional only for legacy files; when present it must equal 1, and unknown versions are rejected |
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
rejected at query time if it produces non-positive pressure, specific internal
energy, `cv`, or `cs^2`, or non-finite derivatives. Schema v1 deliberately
requires a documented energy-zero shift that keeps every reachable specific
internal energy positive because the hydro inversion uses positive `e` as its
admissible state domain.

## Legacy direct model

Set `thermodynamic_model = direct`, or omit it only for an existing legacy
file. Store float64 `pressure`, `energy`, `sound_speed`, and `cv` with the exact
rank-dependent shape above. Optional `dp_drho` and `dp_dT` use the same shape.
Their meanings are `(dP/drho)_e` and `(dP/dT)_rho`, respectively.

The loader requires finite data, positive pressure/energy/sound speed/cv, and
energy strictly increasing with temperature at fixed density and composition. Values
are vertex-interpolated (trilinear or quadrilinear). A missing `dp_drho` is
estimated by perturbing density and reinverting temperature at fixed energy;
it is not a constant-temperature derivative. The direct path is the
compatibility contract for upstream products without a single
Helmholtz-potential representation. New free-energy EOS tables use the
preferred model above.

Queries outside any declared density, temperature, or composition bound do not
extrapolate the table. Schema v1 switches to the existing monatomic ideal-gas
fallback (`gamma = 5/3`). This is a model discontinuity, not evidence that an
external nuclear EOS covers that state. A production table must place guard
nodes around the complete reachable domain and report any fallback encounter.

## Resolution and acceptance

Table resolution is accepted per EOS through measured convergence. Curvature,
phase transitions, and source-table accuracy determine the required mesh. The
following rules define the initial spacing and acceptance process:

- spacing above 0.02 dex in `log10(rho)` or `log10(T)` emits a conservative
  warning;
- for a smooth EOS, start at about 0.015 dex or finer when outer table nodes are
  queried; the audited 0.015625-dex and 0.0125-dex cases are both below a
  `1e-3` endpoint-inclusive error;
- retaining at least two guard nodes outside the physical query domain avoids
  the one-sided five-point boundary stencil, but does not replace axis-halving;
- composition axes (`X`/`Ye`, Abar, and Zbar) use linear interpolation;
  determine their accepted spacing by axis halving;
- halve every axis spacing and compare `P`, `e`, `cv`, `cs`,
  `(dP/drho)_e`, `(dP/de)_rho`, temperature inversion, and the intended
  hydro/burn trajectory;
- sample cell interiors, vertices, and all outer boundaries;
- split or refine intervals near rapid curvature or phase boundaries; the
  current interpolant has no monotonicity limiter.

The retained ideal-gas audit sweep spans two decades on both thermodynamic
axes:

| Nodes per axis | spacing (dex) | full-domain maximum | interior maximum |
| ---: | ---: | ---: | ---: |
| 17 | 0.125 | 0.516063 | 0.118984 |
| 33 | 0.0625 | 0.0494655 | 0.0132374 |
| 65 | 0.03125 | 0.00542707 | 0.00153701 |
| 129 | 0.015625 | 0.000696707 | 0.000194342 |
| 161 | 0.0125 | 0.000361253 | 0.0000942008 |

Rank-3 and rank-4 results are numerically identical because this analytic free
energy is composition independent. The sweep therefore verifies automatic rank
detection, layout, finite-difference/interpolation convergence, and boundary
behavior; it does not qualify nonlinear composition interpolation. The retained
legacy 3D direct/vertex smoke result is `3.30779e-3`. These values apply only to
the smooth analytic EOS. A phase-transition or nuclear-matter table has no
universal accepted spacing and must retain its own axis-halving report.

The repository regression command below checks the finest 161-node normalized
rank-3/rank-4 case and the legacy direct smoke. The complete multi-resolution
sweep was an isolated validation audit; its scalar evidence is retained under
`validation/eos` rather than as another test target.

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

A converter for an external EOS must also document provenance, original units,
energy zero convention, lepton/photon contributions, composition definition,
valid/phase masks, native field transforms or derivatives, and its
refinement/error report. The current Shen source-table assessment is recorded
under [`validation/eos`](../../../validation/eos/README.md); neither assessed
asset is accepted as a directly loadable ARCH table.
