# Tabular EOS interpolation and Shen source-table assessment

Historical evidence; not the current release status. See the [curated summary](../README.md).

Chinese translation: [Chinese archive](tabular-assessment-archive.zh-CN.md). The English file is
the authoritative source text.

> CPU status: normalized rank-3/rank-4 smooth free-energy tables pass the
> spacing sweep. Two real Shen assets were acquired and audited but are not
> accepted as directly loadable ARCH tables. CUDA: pending.

## Normalized HDF5 sweep

Current refactor note (2026-09-06): project-owned analytic constants are now
unified under SI/CODATA 2022 for both backends; see the
[single constants authority](../../../src/physics/constant/README.md). External
table contents are unchanged. The historical results below are not a current
release qualification. Affected Helm and ideal-fallback raw-bit snapshots must
be reconciled against independent current-constant expectations without
self-refreshing implementation output or relaxing budgets.

An isolated audit derived from the tabular EOS regression wrote analytic
ideal-gas Helmholtz free energy into both normalized rank-3 and rank-4 HDF5
layouts at five resolutions. Rank selection was automatic and both policies
were sampled at 100 interior thermodynamic points and the four `rho`/`T`
corners at each policy's fixed test composition. The scalar results are retained
here; the multi-resolution audit source is not a committed test target.

The C++ sweep used a working tree based on
`affde827fcbf317382ed45372912b562652a71c5` plus the changes recorded here,
GCC 13.3.0, the CPU backend,
Release flags `-O3 -march=native -ffast-math -DNDEBUG`, and the same
Intel Core i7-10700 x86_64 WSL2 host used for the external-table analyses.
The regression itself is serial; `OMP_NUM_THREADS=2` was set for the surrounding
ARCH build.

| nodes per thermodynamic axis | spacing (dex) | full maximum relative error | interior maximum |
| ---: | ---: | ---: | ---: |
| 17 | 0.125 | 0.516063 | 0.118984 |
| 33 | 0.0625 | 0.0494655 | 0.0132374 |
| 65 | 0.03125 | 0.00542707 | 0.00153701 |
| 129 | 0.015625 | 0.000696707 | 0.000194342 |
| 161 | 0.0125 | 0.000361253 | 0.0000942008 |

The 129- and 161-node tables meet the `1e-3` full-domain criterion. A smooth
new table should therefore start near 0.015 dex or finer when its outer nodes
are queried. Guard nodes reduce boundary-stencil error, but every production
table still requires axis-halving, especially near rapid curvature or phase
boundaries. The analytic free energy is composition independent, so this test
verifies 3D/4D rank detection and layout but not nonlinear composition-axis
accuracy.

An additional nonideal direct-table probe omitted both derivative datasets and
used `e = cv T + alpha rho`, `P = R rho T + K rho^2`, for which constant-energy
and constant-temperature density derivatives differ. At `rho = 10` and
`T = 1e7`, both rank-3 and rank-4 policies returned
`2.5001704121322535e15`, bitwise equal to an independent constant-energy finite
difference. The analytic value is `2.5e15` (relative interpolation error
`6.82e-5`); the constant-temperature result differs by `16.67%`. The same
one-off probe confirmed rejection of an unknown/missing modern schema version
and a clean dispatcher cache after failed construction.

## Official Shen EOS4

The official EOS4 archive was obtained from
[Zenodo record 3612487](https://zenodo.org/records/3612487) under CC BY 4.0.

| Asset | Bytes | SHA256 |
| --- | ---: | --- |
| `eos4.tab.zip` | 29,344,007 | `1c47911219a72862a27d4eb3eec765cd38857564f91249d886cb1b8295a8d720` |
| `eos4.tab` | 143,167,115 | `5ee37819f873387af9c38207bcada72a48abe695b9491df9ad5c6a5e69487c34` |

The table has 650,650 states: 110 density points at 0.1 dex, 91 temperature
points at 0.04 dex, and 65 proton-fraction points at 0.01 spacing. It provides a
baryonic free energy in MeV per baryon relative to 938 MeV. Matching its
internal-energy zero requires adding 6.506 MeV per baryon before converting to
ARCH specific free energy. Treating its proton fraction as `Ye` additionally
assumes charge neutrality. The source is a baryonic table; constructing the
total EOS expected by ARCH also requires a documented electron/positron and
photon component model.

Applying ARCH's five-point derivative reconstruction directly to that baryonic
potential gives median/90th/99th pressure relative errors of
`3.89e-5 / 1.20e-3 / 0.1215`; internal-energy errors are
`3.53e-5 / 0.05097 / 0.4295`. The source contains 78,335 negative-pressure
states. Reconstructed `P`, `cv`, or `cs2` is non-positive or non-finite in
98,867 states. These are not silently clipped: a baryon-only table is
incompatible with the current total-EOS positivity contract until a documented
component model and valid/phase domain are supplied.

## StellarCollapse HShen HDF5

The EOSDriver-format HShen table was obtained from the
[StellarCollapse EOS collection](https://stellarcollapse.org/equationofstate.html).
Its CC BY-NC-SA terms are incompatible with ordinary bundling under the project
MIT license, so it remains an external validation asset.

| Asset | Bytes | SHA256 |
| --- | ---: | --- |
| compressed H5 | 282,075,158 | `4ae597f50149afa6dc53eb2cf58dd8118f5a40cbbbe011daff405b08219ec3a0` |
| decompressed H5 | 391,264,384 | `3b7c598bf56ec12d734e13a97daf1eeb1f58f59849c5f65c4f9f72dd292b177c` |

Its shape is `(Ye,T,rho) = (65,180,220)`. It stores `logpress`, shifted
`logenergy`, entropy, `dedt`, `cs2`, and pressure derivatives, but no Helmholtz
free energy. The audit reversed its `2.49119e18 erg/g` energy shift and
converted the MeV temperature and entropy in `k_B` per baryon consistently to
erg/g before reconstructing `a = e - T s`. Applying the ARCH finite differences
then gives the following relative-error quantiles against the native fields:

| field | q50 | q90 | q99 | reconstructed non-positive count |
| --- | ---: | ---: | ---: | ---: |
| pressure | `6.90e-5` | `1.388e-2` | `0.21696` | 3,863 |
| cv | `1.043e-2` | `0.6377` | `9.136` | 41,874 |
| cs2 | `1.337e-3` | `0.1509` | `7.177` | 32,514 |
| energy | `1.312e-4` | `1.622e-2` | `0.6151` | — |

Mapping this file to the legacy direct layout would also change its semantics:
EOSDriver interpolates `logP` and shifted `logE`, whereas ARCH direct tables
currently interpolate the physical fields linearly. The source also contains
7,998 non-positive `dedt` and 1,443 non-positive `cs2` states. It is therefore
recorded as assessed/not accepted, not as a failed download or a supported EOS.

## Converter boundary

A production converter for these families needs explicit coordinate arrays and
units, energy-zero and shift metadata, baryon/lepton/photon scope, `Ye`/`Yp`
semantics, native free-energy derivatives where available, valid/phase masks,
per-field `linear`/`log10`/`shifted_log10` transforms, and source/version/license
provenance. Current schema v1 remains valid for smooth uniformly spaced
Helmholtz tables; this assessment defines the extension work required before a
real nuclear-matter table can be claimed.

## Maintained smoke

~~~bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R tabular_eos_ideal_gas --output-on-failure
~~~

This command checks the 161-node normalized 3D/4D tables and the legacy direct
path. It does not rerun the complete five-resolution audit above.

The large external assets and exploratory conversion data are deliberately not
committed. Their checksums are listed above; selected scalar results are
retained in [metrics.csv](../metrics.csv).
