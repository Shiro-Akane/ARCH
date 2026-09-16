# Tabular EOS source and HDF5 interface

Chinese translation: [TabularEOS.zh-CN.md](TabularEOS.zh-CN.md). The exact
runtime parameter and policy surface is indexed from
[`docs/Reference.md`](../../../docs/Reference.md). This file is the local,
file-format contract for table producers.

An equation of state (EOS) relates a material's density, temperature and
composition to quantities such as pressure and internal energy. A tabular EOS
stores sampled values instead of evaluating every term from an analytic model.
HDF5 is the file container; the dataset names, axes and units below define what
ARCH expects inside it. Table rank counts thermodynamic axes, not the number
of spatial dimensions in the simulation.

## Scope

`eos_type = tabular` is one policy with automatic 3D/4D dispatch. It reads
ARCH's normalized HDF5, native EOSDriver/StellarCollapse total-EOS HDF5, and
the original positive-temperature 16-column baryon ASCII main tables used by
Shen EOS2/EOS4. Set `eos_table_path` to the source; no user conversion or
EOS-name-specific runtime policy is required. EOSDispatcher inspects content,
loads the supported representation, completes declared missing components when
needed, and binds one immutable owner to the existing CPU/CUDA views.

Unknown component scope is never inferred from numbers or a filename. Arbitrary
CompOSE products, unrelated ASCII layouts, zero-temperature `.t00` and
zero-charge `.yp0` auxiliary products are not supported by these readers.
A supported file interface does not establish scientific accuracy throughout
a particular source table's entire domain.

`HelmEos` is a separate policy for the native fixed-layout Timmes
`helm_table.dat`.

## Upstream table families

The [official Shen releases](https://user.numazu-ct.ac.jp/~sumi/eos/) are main
tables over density, temperature, and proton/electron fraction. The
[CompOSE software](https://compose.obspm.fr/software/) reads its own general
purpose `(T, nB, Yq)` products and can export its own HDF5 layout.
[StellarCollapse/EOSDriver](https://stellarcollapse.org/equationofstate.html)
distributes total Shen, LS and HS-family `.h5` products in the supported
EOSDriver schema. Their baryon, electron/positron and photon contributions
are already included: ARCH does not add a second Helmholtz EOS to them.
The original EOS2/EOS4 main tables instead contain baryons and require the
component completion described below. The original files distributed with ARCH
retain their [CC BY 4.0 provenance](../../../THIRD_PARTY_NOTICES.md#external-shen-eos-tables-and-eosdriver-compatible-formats);
processed HShen tables are not bundled.

## Component declarations and automatic completion

A normalized HDF5 file may declare scalar string `eos_components`:

| Declaration | Added during host loading |
| --- | --- |
| `baryons` | electrons/positrons and photons |
| `baryons,electrons_positrons` | photons only |
| `baryons,photons` | electrons/positrons only |
| `baryons,electrons_positrons,photons` or `total` | nothing |

Entries must be recognized, distinct, and include baryons. Without this dataset,
an existing normalized table retains its legacy complete-EOS interpretation;
absence does not request completion. EOSDriver's native contract already means
total EOS. The recognized baryon ASCII contract instead supplies its explicit
baryon-only declaration. A format adapter supplies data interpretation, not
another nuclear-matter model.

Completion requires `thermodynamic_model=free_energy`; independently interpolated
pressure/energy fields are not enough to define the missing potential. The
loader adds only the missing terms from the existing Helm electron/positron
component and shared analytic photon component. It never adds Helm ions or
Coulomb corrections on top of the source baryons. `eos_helm_table_path` selects
the auxiliary electron file and defaults to
`EOS_toolkit/tables/helmholtz/helm_table.dat`. A complete table or a table missing
only photons does not read that file. Source files remain unchanged, and users
do not switch between several EOS policies or maintain a converted companion
table for each run.

Electron completion needs a physical `Ye`: rank 3 uses `composition_axis=Ye`
(the default), not a single `species:<name>` fraction; rank 4 uses `Ye=Zbar/Abar`.
Species metadata are required. Optional scalar `baryon_mass_g` must be finite
and positive and records `rho=m_B*n_B`. With `q=1/(m_B*N_A)`, the electron provider
is queried at `rho_H=q*rho`; its specific free energy and energy are multiplied
by `q`, while its physical pressure is unchanged. Photons use the actual source
density. This constant unit conversion is not a density-dependent energy shift
or a correction fitted to Shen. If the mass declaration is absent, the existing
provider mass convention is retained.

Optional scalar integer `nuclear_equilibrium` is exactly `0` or `1`. A value of
`1`, or the native nuclear-equilibrium table contract, requires `use_burn=false`:
independent kinetic burning or NSE would double-count nuclear binding and assume
composition degrees of freedom that the equilibrium table does not have.
Setting this flag to zero is a physical declaration, not a way to make an
equilibrium table compatible with an arbitrary reaction network.

Declared/strict table routes also reject Steger-Warming flux splitting and
automatic stellar conductivity, including nonequilibrium tables. Use a
general-EOS flux and an explicitly chosen constant thermal diffusivity, or
disable thermal diffusion. Permission to use a kinetic energy source is not
proof of compatibility with every weak reaction: tabular views do not yet
provide the electron chemical-potential diagnostic `eta`. In particular,
`aprox19`/`aprox21` electron-capture terms that require a physical `eta` are not
qualified with an arbitrary tabular EOS. The existing Helmholtz routes retain
their electron diagnostics; this change does not add a second diagnostic table
or silently substitute a model for missing weak-process inputs.

Declared-component or nuclear-equilibrium free-energy tables use a strict
finite-domain policy, whether completed automatically or supplied already total.
Source/component invalidity is propagated through all derivative stencils;
queries cannot interpolate across a masked vertex or extrapolate past the source
or electron-table domain. In particular, unsupported `rho_H*Ye` or temperature
does not invoke an extrapolated Helm contribution. One constant energy reference
is chosen at loading if needed for positive conserved energy. It is fixed for
the whole owner and does not alter pressure, entropy or heat capacity. It does
not guarantee positive or monotone interpolation everywhere: each query still
checks admissibility. Temperature recovery checks every monotone interval of
the actual Hermite polynomial, rejects absent or multiple valid roots, and
never bridges masked cells or substitutes an ideal gas.

The owner fingerprint binds source bytes, their interpretation, and the exact
auxiliary electron-table bytes when used. Changing only the supplement therefore
invalidates a cached/restarted EOS identity; moving identical files does not.

## Original baryon ASCII main tables

The supported format has positive-temperature blocks with 16 numeric columns,
common density and `Ye` coordinates, and the published EOS2/EOS4 unit/reference
conventions. Log-density and log-temperature grids must satisfy the common
uniform-grid derivative contract; their physical nodes are retained, as are
the possibly nonuniform `Ye` nodes. The owner needs at least five density and
temperature nodes. This is a format-specific parser feeding the same generic
completion layer, not an EOS-name-specific physical adjustment.

The source convention is `rho=m_B*n_B` with fixed `m_B=1.66054e-24 g`; pressure is
in MeV fm^-3, E/F in MeV per baryon, and entropy in Boltzmann constants per
baryon. Conversion uses this fixed source mass, not the fluctuating ratio of
independently printed density columns. Source E is relative to 931.494 MeV and
F to 938 MeV, so the documented constant `938-931.494=6.506 MeV` aligns F to E's
reference before conversion. This is not a fitted offset. Printed density/nB or
`Ye` pairs outside their source-precision consistency checks are masked rather
than moving the thermodynamic coordinates.

The interpolated potential is constrained by source F, `F_ln(rho)=P/rho` and
`F_ln(T)=-T*S`, with matching electron/photon terms added to each constraint.
These source pressure and entropy constraints retain cold thermal information
that differentiating rounded F alone, or subtracting rounded E/F, can lose.
Runtime energy is derived from this same potential; the independently printed
E column remains a source-consistency diagnostic, not a second energy function.

Source F/E/S are not perfectly consistent at their printed precision. The
checked original data include roughly one percent of points outside the
half-printed-unit F/E/S residual budget. Therefore this representation must not
be described as satisfying every printed column to that budget. Neither a
state-dependent zero shift nor a fitted correction is used to hide that
discrepancy. Valid-cell/source comparisons and inversion conditioning remain
part of the [EOS validation](../../../validation/eos/README.md); sampled success
does not qualify the entire nuclear-matter domain.

## Native EOSDriver total tables

The reader requires scalar or one-element `pointsrho`, `pointstemp`, `pointsye`
and `energy_shift`, one-dimensional `logrho`, `logtemp`, `ye`, and fields
`logpress`, `logenergy`, `dedt`, `cs2`, `dpdrhoe`, `dpderho` with C-order shape
`[pointsye, pointstemp, pointsrho]`. Each axis needs at least two finite,
strictly increasing nodes. The reader retains nonuniform nodes and transposes
field storage without resampling. Density is in g cm^-3, source temperature is
in MeV, and `Ye` is charge per baryon under charge neutrality. Temperature and
heat-capacity units use the shared physical constants. Species metadata must
be present so ARCH can calculate `Ye` from the evolved mass fractions.

`logpress` is `log10(P)` and `logenergy` is `log10(e_source + energy_shift)`.
ARCH interpolates these encoded fields before exponentiation. Its conserved
specific internal energy is `e_ARCH = e_source + energy_shift`; the finite,
nonnegative source shift is a fixed energy reference throughout the run.
Negative physical source energy therefore does not require manual shifting.
The loader never chooses a different shift at individual thermodynamic states.

Thermal and acoustic quantities are analytic derivatives of the same
interpolated `P(rho,T,Ye)` and `e_ARCH(rho,T,Ye)`:

~~~text
cv        = (de/dT)_rho
kappa     = (dP/dT)_rho / cv
chi       = (dP/drho)_T - kappa * (de/drho)_T
cs^2      = chi + kappa * P/rho^2
~~~

This closes the thermal chain rule and Newtonian Euler sound speed. It does
not enforce the Maxwell relations of a single free-energy potential. The
source `dedt`, `cs2` and pressure derivatives remain reference/quality fields;
they are not substituted for derivatives of a different interpolant. In
particular, reconstructing `a=e-Ts` and differentiating it is not part of this
reader. Composition derivatives include the logarithmic decoding chain rule.

Nodes with non-positive source heat capacity/sound speed, invalid mechanical
derivatives or nonincreasing adjacent thermal energy are excluded. A query
requires a cell whose eight vertices are valid, plus finite positive returned
pressure, shifted energy, heat capacity and acoustic speed squared. Native
queries outside this domain fail on both backends; they do not use the
normalized-schema ideal-gas fallback. Temperature recovery inverts the actual
piecewise log-linear energy or pressure field, checks the residual and rejects
absent or multiple thermal roots. Pressure may have a negative thermal slope;
a matching constant-pressure interval does not define a unique inverse.
The current recovery scans thermal intervals,
so native table performance should be measured for the intended workload.
Even a unique inverse can be poorly conditioned in cold, highly degenerate
matter: the relative temperature sensitivity is `e_ARCH/(T*cv)`. Finite
precision in the source `logenergy` field can then preclude a requested
temperature tolerance despite a small energy residual. The real-source
regression reports these under-resolved states separately; loading a native
table does not establish a uniform temperature-accuracy guarantee.

These nuclear-equilibrium tables already include the nuclear binding-energy
contribution and eliminate the detailed nonequilibrium composition. ARCH
rejects their use with a separate kinetic burn/NSE source. Using the same
`Ye` coordinate does not make an arbitrary reaction network thermodynamically
compatible. Electron-specific `eta`, `pele` and `xne` are not supplied by this
reader; support for an electron-dependent transport closure is separate.
Startup therefore also rejects Steger-Warming flux splitting, which requires
a composition-only ideal-gas gamma, and automatic stellar thermal conductivity.
A general-EOS flux and explicit positive constant `alpha_therm` remain available;
thermal diffusion may instead be disabled.

The table fingerprint includes both source bytes and the native interpretation
contract, including the energy convention. Restart checks use that same
identity. Legacy normalized files without new component declarations retain
their original file fingerprints.
The native reader uses the existing immutable host/device owners and dispatcher
cache; it does not write a converted table beside the user's source file.

## Normalized schema: rank detection

A table producer must write scalar integer `table_rank` equal to 3 or 4. Dispatch
uses this metadata first and verifies that it agrees with the composition axes.

The reader also accepts files without `table_rank`, inferring the rank as follows:

- `n_X` and no `n_A`/`n_Z` means rank 3;
- both `n_A` and `n_Z` and no `n_X` means rank 4;
- incomplete or mixed axes are rejected as ambiguous.

Rank detection uses schema content rather than filenames or a table-name list.

## Common scalar datasets

| Dataset | Type | Meaning |
| --- | --- | --- |
| `arch_eos_version` | integer | must equal 1 when present; may be omitted only when both `table_rank` and `thermodynamic_model` are absent; other versions are rejected |
| `table_rank` | integer | table producers must write 3 or 4; reader inference is described above |
| `thermodynamic_model` | UTF-8 string | `free_energy` or `direct`; the reader uses `direct` when absent |
| `eos_components` | UTF-8 string, optional | explicit component declaration; accepted values and legacy behavior are above |
| `nuclear_equilibrium` | integer, optional | exactly 0 or 1; 1 excludes independent kinetic burn/NSE |
| `baryon_mass_g` | positive finite scalar, optional | fixed source mass per baryon for electron unit conversion |
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

Set `thermodynamic_model = free_energy` and store one float64 potential dataset:

| Rank | Dataset shape and C-order index |
| --- | --- |
| 3 | `free_energy[n_rho, n_T, n_X]` |
| 4 | `free_energy[n_rho, n_T, n_A, n_Z]` |

Values are the specific Helmholtz free energy `a` in erg g^-1. All values must
be finite. Both thermodynamic axes require at least five nodes. Optional
`free_energy_dlnrho` and `free_energy_dlnT` have the same shape and units as
`free_energy` and supply derivatives of the declared source components with
respect to natural-log coordinates. They are potential constraints, not
independent runtime pressure or energy fields. Completion adds matching
derivative terms only when these datasets are supplied.

Let `x = ln(rho)` and `y = ln(T)`. ARCH builds fourth-order finite-difference
fields through second order in each coordinate, including the mixed fields,
starting from the supplied first-derivative constraints when present,
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

These identities describe the interpolant, not an independent accuracy
certificate for its source samples. A fixed owner energy reference, when used,
adds that same constant to `e` without changing its derivatives. Monotonicity
across discontinuities or poorly resolved phase boundaries is not guaranteed.
Queries reject non-positive pressure, conserved energy, `cv` or `cs^2`, and
non-finite derivatives. Legacy complete normalized inputs must already use a
documented positive-energy reference; explicitly declared free-energy sources
use the load-time constant-reference and strict-domain handling described above.

## Direct-field model

Set `thermodynamic_model = direct`; this is also the reader's default when the
dataset is absent. Store float64 `pressure`, `energy`, `sound_speed`, and `cv` with the exact
rank-dependent shape above. Optional `dp_drho` and `dp_dT` use the same shape.
Their meanings are `(dP/drho)_e` and `(dP/dT)_rho`, respectively.

The loader requires finite data, positive pressure/energy/sound speed/cv, and
energy strictly increasing with temperature at fixed density and composition. Values
are vertex-interpolated (trilinear or quadrilinear). A missing `dp_drho` is
estimated by perturbing density and reinverting temperature at fixed energy;
it is not a constant-temperature derivative. The direct path is the
input representation for products without a single
Helmholtz-potential representation. A direct table cannot declare missing
components and request independent additions to its fields. Tables with a
free-energy potential use the preferred model above.

Queries outside any declared density, temperature, or composition bound do not
extrapolate the table. The normalized direct-field path retains the existing
monatomic ideal-gas fallback (`gamma = 5/3`), as does the undeclared legacy
free-energy path. Explicitly declared free-energy tables instead use the strict
policy above. A fallback is a model discontinuity, not evidence that an
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
3D direct/vertex smoke result is `3.30779e-3`. These values apply only to
the smooth analytic EOS. A phase-transition or nuclear-matter table has no
universal accepted spacing and must retain its own axis-halving report.

The repository regression command below checks the finest 161-node normalized
rank-3/rank-4 case and the direct-field smoke. The complete multi-resolution
sweep was an isolated validation audit; its scalar evidence is retained under
`validation/eos` rather than as another test target.

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

Other external formats need documented provenance, units, energy convention,
lepton/photon scope, composition definitions, valid masks, field transforms and
refinement/error measurements before support can be claimed. The historical
Shen assessment under [`validation/eos`](../../../validation/eos/README.md)
retains its original source and reconstruction results; native interface
verification is separate from physical qualification of a real nuclear table.
