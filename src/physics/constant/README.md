# Constants and units

`PhysicalConstants.h` is the single CPU/CUDA authority for project-owned physical
and named mathematical constants. It contains scalar compile-time values, not
backend storage or runtime selection. Formula-local aliases may refer to this
authority; they must not supply another numerical value.

The 2026-09-06 owner-approved unification replaces the earlier relocation-only
profiles. There is one current set, not a set per EOS or a historical-version
maintenance layer. Use SI defining constants and the measured central values
from [CODATA 2022 / NIST](https://physics.nist.gov/cuu/Constants/Table/allascii.txt),
the latest published adjustment checked on 2026-09-06. More printed digits in an
old measurement do not make it more accurate. Pi comes from C++20 `<numbers>`.

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

Deferred by the project owner: `physics/network/**`, generated pynucastro math,
Timmes reaction constants, nuclear masses and reaction/screening fit data. These
are data/model contracts, not a second backend implementation. Their constants
must be changed together with their generated energy metadata and reference
data, not by search-and-replace. External EOS tables also keep their original
contents; adopting new analytic constants does not regenerate an electron table.
No claim is made that external tables or deferred networks were restandardized.

## Validation impact

Helm analytic terms, tabular ideal fallbacks, NSE and conductivity now consume
the shared current values. This is an authorized numerical update, not just a
prefix change. Old raw-bit snapshots using different values are historical
evidence, not current acceptance oracles. Requalification must use independently
derived expectations and unchanged physical acceptance budgets; do not refresh
snapshots from the implementation merely to obtain a pass.

Keep this small: the existing constants test checks definitions/conversions and
independent high-precision derived values; the existing CUDA compile probe checks
device constexpr consumption. Affected EOS/NSE/burn checks and their tested
source identities are recorded in the shared [Validation](../../../validation/README.md).
Future constant changes reuse those checks and independent references.

This is not a bucket for every number in the program. Model fits (Helm Coulomb,
stellar conductivity), RK/ODE coefficients, convergence thresholds, table bounds,
runtime defaults and launch sizes remain at their sole owning implementation.
They are inventoried in `docs/development/ImplementationOwnership.md`. Moving a scientific
fit or changing a units convention requires its own numerical review.
