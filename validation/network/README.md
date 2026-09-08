# Reaction-network validation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). English is authoritative.

A reaction network defines the isotopes, reaction rates and coupled evolution
equations used by burning. These tests follow generated networks from package
registration to actual time evolution, and compare weak-reaction composition and
energy changes with independent references. The sparse cases also check the
large-system solver path, not just whether a package compiles.

The results on this page belong to the scientific acceptance snapshot identified
in the [central Validation index](../README.md). Source organization and build
verification have a separate
[maintenance record](../backend/results/maintenance-freeze-20260908/).

CPU and CUDA pass the generated-network application matrix, the real
31-isotope sparse trajectory matrix, and independent weak-reaction trajectories
with constant heat capacity and the Helmholtz EOS. These records exercise shared
network physics and ODE solvers through their CPU and CUDA backends.

## Current evidence

The four records below share the same scientific acceptance source. Each records the
actual build, generated packages, provider libraries and execution controls.
Scientific accuracy and application integration have distinct acceptance criteria;
the table gives rounded maxima with the unchanged budgets.

| Record | Coverage | Largest measured error and original budget |
| --- | --- | --- |
| [Weak Urca, constant heat capacity](results/weak-cv-native-20260907/release-898/evidence.json) | BE_NR, BD and ROS4 on CPU/CUDA against independent trajectories | Tight-control normalized state error `3.3544e-8`, budget `1e-7` |
| [Weak Urca, Helmholtz EOS](results/weak-helm-native-20260907/release-899/evidence.json) | The same six trajectory routes with composition-dependent thermal closure | Tight-control normalized state error `6.5112e-13`, budget `1e-7` |
| [Real audit31 sparse evolution](results/sparse-native-20260907/release-900/evidence.json) | Three ODEs, four external steps, two- and three-cell storage; CPU KLU / CUDA cuDSS | Field error `1.8812e-14` against `2e-10`; limiter error `1.8305e-14` against `2e-8` |
| [Generated-network ARCH applications](results/runtime-native-20260907/release-875/backend-validation-evidence.json) | Six cases, 48 CPU/CUDA executions and 24 comparisons | Normalized field difference `8.7290e-15`; original field tolerances `rtol=2e-8`, `atol=1e-12` |

### Independent weak trajectories

The controlled Na23–Ne23 Urca pair starts with equal mass fractions at
`rho=4e9 g/cm³` and `T=5e8 K`. The constant-cv trajectory uses
`cv=1e8 erg/(g K)` for 10 seconds; the Helmholtz trajectory runs for 0.01 seconds.
The reference reads the original Suzuki tables, interpolates them independently,
and integrates with DOP853 and Radau. Temperature follows the fixed-density first
law, including composition-dependent internal energy and the signed weak source.

Scientific error measures mass fractions, `T/T_initial`, and the integrated weak
source divided by the initial specific internal-energy scale. That scale is
`cv*T_initial` for constant cv and the independently evaluated initial energy
for Helmholtz. Both retain the original `1e-7` scientific budget. DOP853/Radau
agree within `3.3307e-16` and `1.4823e-21`, respectively; their agreement and
independent energy-identity budget is `2e-11`. The maximum independent energy
residuals are `4.5750e-16` and `8.5639e-19`.

All three production ODEs pass on both backends at local tolerance `1e-11`.
Refining the local tolerance from `1e-7` improves the BE_NR error by about
100.0 times for constant cv and 88.7 times for Helmholtz. The coarse constant-cv
error, `3.3556e-6`, remains recorded as a failed control under the same `1e-7`
scientific budget, making the tolerance-convergence result reproducible.
The tight production energy-closure errors are `2.5455e-14` and `2.4728e-10`
against the original `2e-8` limit. This latter check is relative to heating,
rather than the initial-energy normalization of the independent state comparison.

The trajectory routes use DenseLU on both backends. Accompanying typed-factory
checks also exercise CUDA DenseLU/cuDSS owner reuse with two- and three-cell
storage. Their separate CPU/CUDA field comparison retains the original
`2e-10 * max(1, |reference|)` budget. Those factory controls use constant cv in
both runs; the Helmholtz scientific trajectory uses the real EOS. These tests establish the tabulated
Urca integration path; other generated networks are assessed using their own
isotope sets, reaction data and applicable thermodynamic range.

### Sparse and whole-application integration

The audit31 system has 31 isotopes plus temperature: 32 ODE equations, so it
exercises the production sparse route. The focused record uses CPU KLU and CUDA
cuDSS with each of BE_NR, BD and ROS4, changes storage from two to three cells,
and records real composition evolution over four external steps. It checks
shared-physics provider agreement and storage reuse; independent reaction-data
references serve a different purpose. Recorded step times are diagnostics,
not a controlled speedup benchmark.

The [application manifest](runtime_cases.json) runs audit31 and weak_urca through
the real ARCH program with the Helmholtz EOS and all three ODEs.
`linear_solver=Auto` selects KLU on CPU and cuDSS on CUDA for audit31, and DenseLU
on both backends for the compact weak network. Both backends reach the prescribed
physical times: `1e-10` seconds for audit31 and `0.01` seconds for weak Urca.
The largest fixed-time normalized field difference is `7.6473e-15`.
Across all samples, the maximum mass-fraction sum error is `2.2204e-16` against
the original `1e-8` budget; density and energy remain positive.

Intermediate weak-network step/controller differences are recorded as diagnostics;
physical comparison uses equal-time endpoints. Strict checkpoint restoration is
covered by separate restart suites. DenseLU's production boundary is 31
**total ODE equations**, including temperature and any signed-source quadrature;
larger systems use CPU KLU / CUDA cuDSS.

### Device-safety coverage

The final focused [memcheck](../backend/results/final-first-law-20260907/memcheck-929/evidence.json)
and [racecheck](../backend/results/final-first-law-20260907/racecheck-903/evidence.json)
campaigns each pass all 23 routes, including generated-network mathematics,
controlled weak trajectories, owner reuse and real audit31 sparse evolution.
Memcheck has 23 complete reports with zero errors or leaks; racecheck has
23 complete reports with zero hazards, errors or warnings.

| Sparse instrumentation | Observation interval | Largest field / limiter errors | Original budgets |
| --- | --- | --- | --- |
| Memcheck | Full `1e-10 s` | `1.8812e-14` / `1.8305e-14` | `2e-10` / `2e-8` |
| Racecheck | Explicit `1e-12 s` | `1.5222e-14` / `1.5148e-14` | `2e-10` / `2e-8` |

Both sparse checks retain three ODEs, two- and three-cell storage and four
subdivisions, with all 24 CPU and 24 GPU step records. Density, temperature,
heat capacity, local tolerance and initial composition are unchanged. Every
method records real evolution and positive kernel activity, with a stable pool
capacity of two. Only the racecheck sparse observation interval is shortened;
the other 22 commands and all numerical budgets are unchanged. The ordinary
audit31 sparse trajectory, audit31 applications and full-interval memcheck
retain their `1e-10 s` endpoint.

The [instrumentation guide](../backend/results/final-first-law-20260907/README.md)
contains both guarded reproduction commands, the complete test-build requirement
and independent record reviews. Instrumented timings are diagnostics, not speedup
benchmarks. Overall acceptance is tracked in the [validation overview](../README.md).

## Reproduce the records

Run the following in a Linux/WSL shell from the repository root after installing
the [build prerequisites](../../README.md#build), including the EOS assets and
cuDSS 0.8. The archived generated packages and independent references use
Python 3.11 with **pynucastro 2.12.0**. Use the same Python environment for generation and the
reference tools so they read the same Suzuki tables and nuclear data.

```bash
python3.11 -m venv build/network-python
source build/network-python/bin/activate
python -m pip install "pynucastro==2.12.0" numpy scipy mpmath

python tools/network/GenerateNetwork.py validation/network/inputs/audit31.py
python tools/network/GenerateNetwork.py validation/network/inputs/weak_urca.py

validation_build="$PWD/build/network-validation"
cmake -S . -B "$validation_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DARCH_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=native \
  -DARCH_ENABLE_KLU=ON -DARCH_FETCH_SUITESPARSE=ON \
  -DARCH_ENABLE_CUDSS=ON -DCUDSS_ROOT=/path/to/cudss \
  -DARCH_CUSTOM_NETWORKS="audit31;weak_urca" \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$validation_build/bin"

python tools/run_memory_guarded.py --min-available-mib 1536 \
  --max-swap-growth-mib 256 --pressure-guard -- \
  cmake --build "$validation_build" --parallel 1 --target \
    ARCH arch_cuda_single_level_validation \
    arch_cuda_generated_weak_trajectory_weak_urca \
    arch_cuda_generated_weak_factory_weak_urca \
    arch_cuda_generated_sparse_burn_audit31
```

Replace `/path/to/cudss` with the installed provider prefix, or omit `CUDSS_ROOT`
if CMake already finds it. An installed KLU package can be located through
`CMAKE_PREFIX_PATH`; otherwise CMake fetches its pinned SuiteSparse dependency.
Check that configuration reports both `cuDSS found` and `SuiteSparse KLU enabled`.
These records require both providers. The five build targets above include the
three focused executables, ARCH and its checkpoint comparator. The explicit
output directory places ARCH at the path used below; its default is the
repository's `bin/`, not the build directory's `bin/`.

The generator writes [audit31](inputs/audit31.py) and [weak_urca](inputs/weak_urca.py)
under `src/physics/network/custom/`; both backends use those same registered
packages. Matching packages are left unchanged. To replace a package after
changing its recipe or generation settings, add `--replace`; the generator keeps a
backup. Rerun CMake after generating or replacing a package.

Keep this environment active and use new, empty result directories for each
replay. The commands below use the recorded four-thread setting. Adjust build
concurrency and [memory-guard](../../tools/run_memory_guarded.py) limits to
available resources; the serial build above is a starting configuration.

```bash
export OMP_NUM_THREADS=4

python3 validation/network/run_weak_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-weak-cv \
  --network-id weak_urca --eos constant_cv \
  --rho 4e9 --temperature 5e8 --interval 10 --cv 1e8

python3 validation/network/run_weak_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-weak-helm \
  --network-id weak_urca --eos helmholtz \
  --rho 4e9 --temperature 5e8 --interval 0.01 --cv 1e8

python3 validation/network/run_sparse_validation.py \
  --build-dir "$validation_build" --output-dir validation/network/results/local-audit31 \
  --network-id audit31 --rho 1e7 --temperature 3e9 --interval 1e-10 \
  --cv 1e8 --rtol 1e-7 --steps 4 --composition c12=0.5 o16=0.5

python3 tools/validate_backend_results.py \
  --source-root . --build-dir "$validation_build" \
  --arch "$validation_build/bin/ARCH" \
  --checkpoint-validator "$validation_build/arch_cuda_single_level_validation" \
  --manifest validation/network/runtime_cases.json \
  --output-root validation/network/results/local-generated-runtime
```

For the Helmholtz command, `--cv` supplies the accompanying factory control;
the trajectory itself uses Helmholtz derivatives. The weak runner retains coarse
and tight controls and requires the tight result to pass. The sparse runner
requires all three ODEs and both storage sizes; a selected-method diagnostic or
a skipped GPU run does not count as complete coverage.

## Scope and follow-up

Complete Release/Debug regressions and the
[five-phase core-build check](../backend/results/cold-core-first-law-20260907/release-909/README.md)
and the [bounded capacity campaign](../backend/results/device-memory-first-law-20260907/README.md)
have passed, as have both focused device-safety campaigns above. Final delivery
review and the combined acceptance record are tracked in the
[validation overview](../README.md). The four scientific/application records
and the instrumentation records retain their distinct acceptance scopes.

Complete 150/200-isotope trajectories and scaling remain the approved follow-up
work for a larger validation system, outside this local release gate. For very
large networks, scientific reliability depends on the isotope set, reaction data
and model's range of applicability; solver/provider validation is recorded
separately. Custom-network NSE is not currently supported; the
[NSE reference](nse_reference.py) covers the supported built-in networks.

See the internal [release standard](../../docs/development/CudaReleaseStandard.md)
and the user-facing [backend guide](../../docs/CudaBackendStatus.md).
