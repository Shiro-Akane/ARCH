# Historical generated custom-network and KLU compatibility

Historical generator-v3 CPU evidence, not a certificate for the current CUDA
release candidate. See the [current module summary](../../README.md).

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

> CPU status: 31-, 150-, and 200-isotope pynucastro packages pass generation,
> coexistence, dispatch, generated RHS/Jacobian, SparseKLU factor/refactor, and
> one-step driver smoke tests. Long-time physical qualification is not claimed.

This record exercises user-defined network names rather than reserving a fixed
`sn160` slot. Three packages are generated into sibling folders and registered
in one CMake configuration. A `.par` file selects one runtime name, so the
packages do not replace one another or any `aprox*`/`iso*` built-in network.

## Environment and packages

The run used GCC 13.3.0, Python 3.11.15 from the `p311` environment,
pynucastro 2.12.0, and SuiteSparse 7.13.0. The worktree was based on
`affde827fcbf317382ed45372912b562652a71c5`. The combined custom-network build
used the CPU backend, Debug configuration, and `ARCH_ENABLE_OPENMP=OFF` on an
Intel Core i7-10700 under x86_64 WSL2. All packages were regenerated with
`GenerateNetwork.py` version 3 after the inert-species safeguard was added.

| Runtime ID | Isotopes | ReacLib rates | Recipe SHA256 |
| --- | ---: | ---: | --- |
| `custom:audit31` | 31 | 229 | `153e21acf3e265a9ffcacd60e6ebd8f9263f5409cb16eda4df64854236884607` |
| `custom:audit150` | 150 | 1416 | `a6b2fbb68c92aac0c44734bb01cd66091076576cbdb88cb58b0bb4994933ba4e` |
| `custom:audit200` | 200 | 1965 | `882d94567940e144f4d37a1128af8c0472ce89a8537ac90b385677822de9681c` |

The recipes are stress definitions around a broad valley-of-stability band;
they are not published `sn160`-class physical networks. Generated C++ packages
are build artifacts and are not committed.

An additional one-nucleus, zero-rate `--check` probe retained the requested
nucleus as one inert species. This verifies that an unusual disconnected
`NUCLEI` recipe is not silently reduced to an empty network.

## Solver result

At `rho = 1e7 g cm^-3`, `T = 3e9 K`, and equal C12/O16 mass fractions, the
generated RHS and Jacobian were passed through `SparseMatrixData` and KLU. Each
matrix was factored, solved against a manufactured solution, evaluated again at
`1.01 T`, and refactored with the same symbolic pattern.

| Isotopes | mass-RHS relative residual | first KLU error | refactor error |
| ---: | ---: | ---: | ---: |
| 31 | `2.80e-17` | `0.00` | `4.44e-16` |
| 150 | `1.27e-16` | `0.00` | `6.66e-16` |
| 200 | `1.46e-16` | `8.88e-16` | `2.22e-16` |

Three Helmholtz `BurnOneZone` runs used BE_NR and
`linear_solver = Auto`, advancing one step to `1e-16 s`. They exited normally
and selected SparseKLU with `N = 32`, `151`, and `201`. Explicit DenseLU rejects
the 31-isotope case as designed, preserving its dedicated at-most-30-isotope
contract. A separate KLU-disabled build rejected `Auto` for the same large
network during dispatch, before entering the integrator.

pynucastro 2.12 emits `jac.set(..., 0.0)` for every structurally absent species
pair. Generator version 3 removes only those compile-time literal-zero calls;
it never filters entries whose evaluated value happens to be zero at one state.
The retained species-block patterns contain 423, 2363, and 3223 entries instead
of 31-squared, 150-squared, and 200-squared slots. After the temperature row and
column are added, the complete ODE Jacobians contain 486, 2664, and 3624
entries. This keeps KLU symbolic reuse and restores the intended sparse storage
behavior.

## Regenerate and compile

~~~bash
CUSTOM_ROOT=/tmp/arch-custom-network-validation

conda run -n p311 python tools/network/GenerateNetwork.py \
  validation/network/inputs/audit31.py \
  --check --custom-root "$CUSTOM_ROOT"
conda run -n p311 python tools/network/GenerateNetwork.py \
  validation/network/inputs/audit31.py \
  --custom-root "$CUSTOM_ROOT"

# Repeat for audit150.py and audit200.py, then configure all three together.
cmake -S . -B build-network-validation \
  -DARCH_ENABLE_KLU=ON \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY=/tmp/arch-network-validation-bin \
  -DARCH_CUSTOM_NETWORK_ROOT="$CUSTOM_ROOT" \
  -DARCH_CUSTOM_NETWORKS="audit31;audit150;audit200"
cmake --build build-network-validation --parallel 1
~~~

The commands select the tested `p311` conda environment; another Python 3.11
environment may be used when it contains the recorded pynucastro release.
`--custom-root` is an audit option; normal user generation writes under
`src/physics/network/custom/`.

The one-off RHS/KLU/refactor harness and generated packages are intentionally
not committed. The table above and [metrics.csv](metrics.csv) retain that audit
evidence; the commands here reproduce package generation, registry discovery,
and compilation without adding a permanent test target.

## Scope label

- **Verified:** safe package naming, disconnected-nucleus retention, no built-in overwrite, three-package
  coexistence, CMake selection, `.par` dispatch, generated RHS/Jacobian,
  KLU-disabled fail-fast, mass-RHS residual, KLU factor/refactor, and short
  driver/I/O execution.
- **Not a physics qualification:** screening choice, rate completeness,
  tolerances, thermal trajectory, weak-energy derivatives, and long-time
  abundance evolution remain the responsibility of each user network.
- **Scale boundary:** CSC values are sparse, but the current entry-to-slot
  lookup allocates `N*N` integers. The audit reaches 200 isotopes and does not
  claim unbounded network size.
- **Pending:** CUDA sparse backend parity; these CPU KLU results do not imply
  cuDSS compatibility.

Machine-readable results are retained in [metrics.csv](metrics.csv).
