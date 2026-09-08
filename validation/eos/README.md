# EOS validation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). English is authoritative.

An equation of state (EOS) relates density, temperature and composition to
pressure, energy and their derivatives. The tests check both these local
thermodynamic relations and their use in evolving fluid and burning problems.
Each reference covers its stated thermodynamic range and table representation.

The results on this page belong to the scientific acceptance snapshot identified
in the [central Validation index](../README.md). Source organization and build
verification have a separate
[maintenance record](../backend/results/maintenance-freeze-20260908/).

EOS verification combines independent thermodynamic references with actual
hydrodynamic and burning applications. Ideal gas, Helmholtz and normalized
Tabular3D/Tabular4D share their mathematics between CPU and CUDA; backend owners
provide table storage and lifetime management.

## Coupled application results

The [Release application record](results/application-native-20260907/release-891/evidence.json)
passes twelve cases and 96 CPU/CUDA executions. The
[independent endpoint record](results/application-native-20260907/endpoints-892/evidence.json)
checks all 24 physical endpoints. Both records identify the same source,
executable, comparator and dependencies and verify that they stayed unchanged.

Analytic ideal-gas data populate direct-field and free-energy tables of both
ranks. Four cases evolve an entropy wave on a mixed-level adaptive mesh to
`t=1e-9`; density is compared with exact finite-volume wave averages. Eight
cases evolve aprox13 burning at fixed density to `t=1e-10`, using BE_NR, BD and
ROS4 with free-energy tables and BD with direct-field tables. These burning
cases disable NSE. Intermediate step checks supplement the prescribed-time
physical comparisons.

| Check | Largest recorded error | Acceptance budget |
| --- | ---: | --- |
| Analytic pressure, free-energy tables | relative `2.975e-10` | `1e-3` |
| Analytic pressure, direct-field tables | relative `3.236e-4` | `1e-3` |
| Entropy-wave density | L1 / mean density `9.190e-5` | `1e-3` |
| Hydro mass, momentum and energy conservation | relative `2.065e-15` | `rtol=2e-12`, `atol=2e-11` |
| Burn source-aware energy closure | relative `3.713e-15` | `1e-12` |

CPU/CUDA fields pass `rtol=2e-8`, `atol=1e-12`; burning mass fractions satisfy
the `1e-12` sum budget. Hydro conservation uses physical cell volumes. The
independent burn check computes nuclear binding-energy release from native
mass fractions and compares it with conserved-energy change; expected energy
does not come from a production EOS or time integrator.

The tables and trajectories define the scope of these results. Validity of a
user-supplied EOS table follows its documented thermodynamic domain and data
contract. The [validation index](../README.md) records overall acceptance,
combining these results with full regression, restart, sanitizer, sustained
execution and capacity evidence.

## Independent mathematical checks

- The EOS tests compare Host/Device values, table ownership and error paths.
  Manufactured polynomials check interpolation, composition gradients,
  Hessian actions and heat-capacity derivatives.
- Helmholtz state, temperature inversion and a short isentropic path use
  independent references. Weak/strong Coulomb and radiation-dominated states
  additionally test pressure, energy, heat capacity and electron quantities.
- [helm_reference.py](helm_reference.py) reconstructs the table interpolant
  from independent endpoint constraints at two high precisions. It uses the
  original table data and documented constants, without calling production
  EOS code. Sound speed and pressure derivatives follow independent
  thermodynamic identities.

## Reproduce

Run the [application recipe](results/application-native-20260907/replay.py)
with `--build-dir` and a new `--output-dir`. It creates the manufactured tables
and runs both backends through the common application validator. Then run the
[endpoint checker](results/application-native-20260907/check_terminal.py)
with the same `--build-dir`, `--report` pointing to the application `evidence.json`,
and another new `--output-dir`. These scripts require NumPy and h5py and retain
inputs, scientific budgets and artifact identities in their reports.

Use a CUDA-enabled build configured as described in the
[build guide](../../README.md). Select build concurrency according to available
memory. The mathematical tests can also be run separately:

```bash
cmake --build build --target arch_cuda_eos_host_device_parity
ctest --test-dir build -R '^eos_host_device_parity$' --output-on-failure
python validation/eos/helm_reference.py
```

The independent script requires NumPy, SciPy and mpmath. Its output is a
reference calculation, not an automatic fixture update. The normalized CPU
table regression can also be run with:

```bash
cmake --build build --target tabular_eos_regression
ctest --test-dir build -R '^tabular_eos_ideal_gas$' --output-on-failure
```

## Table formats and follow-up work

The current reader accepts normalized 3D/4D HDF5 and native EOSDriver total-EOS
HDF5 under the [data contract](../../src/physics/eos/TabularEOS.md). The latter
preserves native axes and log-field encodings, adopts the source's fixed energy
shift and rejects invalid cells or ambiguous temperature inverses. It is a
new interface, not part of the frozen application results above. Its focused
regression target is `arch_native_tabular`; an optional second argument after
the output directory loads an actual EOSDriver file. The current source also
reads the shared finite-temperature baryon ASCII format used by EOS2/EOS4,
and completes explicitly missing electron/positron or photon terms for both
normalized free-energy ranks. Generic CompOSE remains unsupported. The
[historical source-table assessment](results/tabular-assessment-archive.md)
retains the original spacing study and source-data analysis.

### Native targeted check

The local working tree based on `e799640b` plus uncommitted changes was checked
against the original EOSDriver `HShenEOS.h5` with
[NativeTabularRegression.cpp](../../tests/host/NativeTabularRegression.cpp).
Of 400 deterministic interior samples, 395 had unique usable inverses:
391 were thermally resolved at the fixed `2e-8` relative-temperature budget,
and 4 were separately classified as source-precision-limited. Four samples
touched invalid cells and one had multiple valid thermal roots; those states
were rejected rather than assigned an arbitrary inverse.

The largest resolved temperature error was `2.1496398940564856e-9`; the largest
error including the under-resolved group was `5.5932036852564723e-7`. The
largest energy back-substitution residual was `1.6412292014574082e-14`, below
the unchanged `2e-12` budget. Under-resolved classification uses source log-energy
ULPs and `e/(T*cv)` before testing the inverse; it does not certify those cold
states at the `2e-8` temperature budget. The
[retained diagnostic log](results/tabular-extension-20260908/native-real-final.log)
records this earlier-source observation. This targeted
interface/conditioning check is not a complete physical qualification of Shen
matter and does not modify the frozen application records above.

### Component and raw-baryon extension checks

The [extension record](results/tabular-extension-20260908/README.md) keeps source,
asset and build identities, regression reports, actual CUDA owner execution and
bounded application checks separate from the historical release above.
Manufactured 3D/4D tables check every supported missing-component subset,
already-total inputs, an independent non-default baryon-mass convention,
source/dependency fingerprints and startup rejection. Independent component
controls check Timmes electron/positron and analytic photon formulas. Exact
potentials check strict inverses, same-cell multiple roots, invalid intervals
and derivative seeds.

Each unmodified EOS2/EOS4 source has 650650 nodes. Source coordinate checks
mark 6382 and 6369 nodes respectively; another 29211 source-valid nodes per
table lie outside the electron provider. Propagating all derivative dependencies
excludes 75126 and 74918 nodal stencils. These are not valid-volume fractions;
queries require the full participating patch to be valid.

Each real-table owner sample fixes 200 source nodes and 50 interior points.
Both tables accept 174 nodes and 46 interiors; 7 nodes exceed component support,
19 nodes and 4 interiors touch excluded derivative stencils. No accepted sample
is relabeled from a failed, nonphysical or ambiguous inverse. Maximum nodal
pressure/energy error relative to independently assembled source F/P/S and
component terms is `2.427e-14` (fixed budget `2e-8`). Maximum resolved inverse
temperature error is `2.902e-14`; all 46 accepted interiors per table meet the
fixed `2e-8` budget. Energy back-substitution meets `2e-12`.

These node identities and interior self-consistency do not measure interpolation
accuracy against an independent continuous nuclear-matter model. The source's
printed F/E/S retain small discrepancies; roughly 1% of source-valid nodes
exceed a strict half-last-digit rounding budget. ARCH preserves F/P/S constraints
and does not fit reference constants or overwrite source files to remove this
disagreement. Full-domain and nonuniform nuclear-EOS applications remain
separate qualification tasks.
