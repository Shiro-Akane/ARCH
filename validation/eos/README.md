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

Production tabular EOS inputs follow the normalized 3D/4D HDF5
[data contract](../../src/physics/eos/TabularEOS.md). Converting native Shen
EOS4/EOSDriver data is a separate future extension that must document units,
energy zero, thermodynamic components and valid domains. The
[historical source-table assessment](results/tabular-assessment-archive.md)
retains the original spacing study and source-data analysis.
