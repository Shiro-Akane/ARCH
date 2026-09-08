# Constants and units

`PhysicalConstants.h` is the single CPU/CUDA authority for project-owned physical
and named mathematical constants. It contains scalar compile-time values, not
backend storage or runtime selection. Formula-local aliases may refer to this
authority; they must not supply another numerical value.

All models use the same definitions, grouped by physical discipline rather than
by caller. The values come from SI defining constants and the measured central
values in [CODATA 2022 / NIST](https://physics.nist.gov/cuu/Constants/Table/allascii.txt),
checked on 2026-09-06. Measured constants retain the precision supported by that
reference. Pi comes from C++20 `<numbers>`.

| Namespace | Quantities and units |
|---|---|
| `math` | Dimensionless pi and two-pi; radians |
| `units` | erg/eV and erg/MeV conversions |
| `statistical` / `statistical::cgs` | Avogadro (1/mol), Boltzmann (erg/K) |
| `quantum::cgs` | Planck and reduced Planck (erg s) |
| `atomic::cgs` | Atomic mass unit and proton mass (g) |
| `relativity::cgs` | Light speed (cm/s) |
| `gravity::cgs` | Gravitational constant (cm^3/(g s^2)) |
| `electromagnetic` / `electromagnetic::cgs` | Fine structure; Gaussian e^2 (erg cm) |
| `radiation::cgs` | Stefan-Boltzmann (erg/(cm^2 s K^4)), energy density (erg/(cm^3 K^4)) |

The exact definitions are k_B=1.380649e-16 erg/K, h=6.62607015e-27 erg s,
N_A=6.02214076e23 /mol and c=2.99792458e10 cm/s. Measured values retain their
published significant figures: m_u=1.66053906892e-24 g (standard uncertainty
5.2e-34 g), m_p=1.67262192595e-24 g (5.2e-34 g),
G=6.67430e-8 cm^3/(g s^2) (1.5e-12), alpha=7.2973525643e-3 (1.1e-12).
Stored doubles still have binary rounding; physical exactness is not a promise
of an exactly representable binary fraction.

Derived quantities use the common definitions: hbar=h/(2 pi),
sigma=2 pi^5 k_B^4/(15 h^3 c^2), a=4 sigma/c and Gaussian e^2=alpha hbar c.
This avoids another rounded sigma or charge authority. SI coulombs are not
Gaussian statC; do not insert an SI charge into the Coulomb formula. N_A*m_u
is not defined to be exactly one gram per mole in the present SI.

## Units and data boundaries

Microphysics uses rho in g/cm^3, T in K, specific energy in erg/g, pressure in
erg/cm^3, conductivity in erg/(cm K s), and mass fractions without units.
Nuclear binding energies use the network's declared MeV conversion convention.
Ideal-gas problems may use consistent arbitrary code units and supplied species
Cv values. The framework does not rescale those inputs implicitly. In
particular, the air-like 718 J/(kg K) fallback is a model default owned by
`IdealGasView`, not a universal constant or an implicit cgs conversion. Future
geometrized-unit calculations must explicitly convert to G=c=1, not overwrite
the dimensional constants above.

`physics/network/**`, generated pynucastro math, Timmes reaction constants,
nuclear masses and reaction/screening fit data retain their model-specific
definitions. These constitute strict data/model contracts, not secondary backend implementations. Any changes to their constants must be made in tandem with updates to their generated energy metadata and reference data; they must not be updated via simple search-and-replace. Likewise, external EOS tables retain their original contents and conventions. Note that updating the analytic constants here will not regenerate an electron table or alter any pre-existing reaction data.

## Validation impact

Helm analytic terms, tabular ideal fallbacks, NSE and conductivity consume these
shared values. Archived raw-bit snapshots retain the constants and source
identities under which they were recorded. Acceptance checks use independently
derived expectations and the stated physical budgets; changing a constant
requires a numerical review, not snapshots copied from the changed implementation.

The constants test checks definitions/conversions and
independent high-precision derived values; the existing CUDA compile probe checks
device constexpr consumption. Affected EOS/NSE/burn checks and their tested
source identities are recorded in the shared [Validation](../../../validation/README.md).
Future constant changes reuse those checks and independent references.

This is not a bucket for every number in the program. Model fits (Helm Coulomb,
stellar conductivity), RK/ODE coefficients, convergence thresholds, table bounds,
runtime defaults and launch sizes remain at their sole owning implementation.
They are inventoried in `docs/development/ImplementationOwnership.md`. Moving a scientific
fit or changing a units convention requires its own numerical review.
