# ARCH: Adaptive Reactive CUDA Hydrodynamics

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text; update it first when behavior or interfaces
change.

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![Main backend](https://img.shields.io/badge/main-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-V2%20in%20development-yellow.svg)]()
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH is a block-adaptive finite-volume framework for compressible and reactive
hydrodynamics. The `main` branch provides the CPU implementation. CUDA development
continues on GitHub branches.

The project is intended for two groups:

- students learning how a CFD case is configured, initialized, advanced, and inspected;
- researchers extending Riemann solvers, equations of state, reaction networks,
  diffusion, gravity, AMR, or diagnostics directly in the source tree.

## Project status

| Capability | `main` branch | V2 CUDA branch |
| --- | --- | --- |
| CPU/OpenMP hydrodynamics | supported | supported target |
| CUDA execution | reserved | in active development |
| 1D/2D/3D block AMR | supported | parity work in progress |
| Ideal, tabular, and Helmholtz EOS | supported on CPU | module parity must be reported by V2 |
| External gravity | supported | module parity must be reported by V2 |
| Self gravity | unavailable | unavailable |
| Nuclear networks and NSE | supported on CPU | module parity must be reported by V2 |
| Quantitative validation suite | first CPU baselines recorded | CPU/CUDA parity pending |

`compute_backend` and `cuda_device` are reserved CUDA keys. The `main` driver
executes the CPU backend.

## Implemented capabilities

- conservative block AMR with ghost exchange, prolongation/restriction, flux
  registers, and reflux;
- dimension-aware 1D, 2D, and 3D storage on Cartesian, cylindrical, and spherical grids;
- SW, VL, Roe, HLL, and HLLC flux policies;
- PCM, MUSCL/PLM, and PPM reconstruction with Euler, SSPRK2, or SSPRK3 time stepping;
- ideal, automatically ranked 3D/4D tabular (including normalized free-energy tables), and Timmes Helmholtz equations of state;
- external gravity, built-in or generated pynucastro nuclear burning, DenseLU/KLU linear solves, NSE projection, and RKL1/RKL2 super-time-stepping diffusion;
- HDF5 plot and checkpoint files, including restart of the AMR leaf hierarchy.

## Build

ARCH targets a Linux/WSL-style C++ environment. Required tools and
libraries are:

- a C++20 compiler;
- CMake 3.22 or newer;
- OpenMP unless configured off;
- HDF5 C++ and HL libraries;
- Git and network access during configuration, because CMake fetches HighFive and, when no installed KLU package is found, pinned SuiteSparse;
- Git LFS when cloning EOS `.dat` or `.h5` assets tracked through LFS.

Configure and build from the repository root:

```bash
git lfs pull
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
```

The dispatch translation units instantiate a large template matrix. Very high
parallel build counts can exhaust memory even though the dispatch target is
compiled at a reduced optimization level. Increase `--parallel` only after
checking available RAM.

The executable is written to `bin/ARCH`. KLU is enabled by default; CMake
uses an installed package or fetches pinned SuiteSparse v7.13.0.

## Tabular EOS and custom networks

Tabular EOS configuration supplies the HDF5 path; rank selection is file-driven:

~~~text
eos_type = tabular
eos_table_path = /path/to/model.h5
~~~

`table_rank` inside the file selects the 3D or 4D policy. New EOS tables should
store specific Helmholtz free energy and follow the
[local HDF5 contract](src/physics/eos/TabularEOS.md); upstream Shen/LS/HS or
CompOSE files require a family-specific converter to that contract. No external
EOS converter is currently bundled; the real Shen source-table assessment is
recorded in [validation/eos](validation/eos/README.md).

To generate a custom reaction network, copy and edit the example recipe—choose
a unique folder/`NETWORK_ID` and the required nuclei—then run:

~~~bash
cp examples/network/CustomNetworkRecipe.py MyNetwork.py
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py --check
conda run -n p311 python tools/network/GenerateNetwork.py MyNetwork.py
cmake -S . -B build
cmake --build build --parallel 4
~~~

Each generated package occupies `src/physics/network/custom/<id>/`. CMake
registers the generated IDs present in that directory, supports multiple IDs,
and reserves the `aprox*`/`iso*` namespaces for built-in networks. Existing-ID
replacement follows the guarded generator workflow. Select one package per run:

~~~text
network_name = custom:<id>
linear_solver = Auto
~~~

`Auto` keeps the dedicated DenseLU path through 30 isotopes and selects
SparseKLU above 30. The complete contract and generator limitations are in the
[Research and API Reference](docs/Reference.md); multi-size compatibility
evidence is centralized in [validation/network](validation/network/README.md).

## First run

Run the small one-dimensional Sod teaching case:

```bash
export OMP_NUM_THREADS=4
./bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

A successful run prints the selected EOS, solver, reconstruction, time
integrator, AMR resolution, and a step table. It writes files under
`output/first_sod/`:

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

`Sod_beginner.par` uses 64 cells; `Sod.par` provides the 128-cell standard
shock tube. The blast benchmark is in `simulation/Sedov/`.

## Documentation paths

The [Simulation Case Guide](docs/guides/SimulationCase.md) provides a continuous
student path through the first run, core CFD parameters, and a new
`Setup`/`Init` case.

For exact parameter names, accepted values, API signatures, output schemas,
extension contracts, and known compromises, use the single searchable
[Research and API Reference](docs/Reference.md).

The [documentation index](docs/README.md) groups learning guides, physics
notes, API reference material, and legal-document pointers by audience.

Quantitative status, CPU results, known failures, and CUDA placeholders are
indexed in [validation/README.md](validation/README.md). The
[AMR status page](validation/amr/README.md) integrates the historical figures
with quantitative conservation baselines and the remaining local
refinement-retention limitation.

## Repository map

```text
ARCH/
├── README.md                  # Entry point and first run
├── LICENSE                    # MIT license for ARCH-authored material
├── THIRD_PARTY_NOTICES.md     # Scientific-source provenance and terms
├── LICENSES/                  # Retained third-party license texts
├── CMakeLists.txt             # CPU build and template-dispatch targets
├── simulation/               # Case implementations and reusable example inputs
├── docs/                     # Guides, reference, physics notes, legal index
├── validation/               # Single V&V tree: inputs, records, metrics, figures
├── EOS_toolkit/              # Runtime EOS tables grouped by model
├── src/
│   ├── core/                 # Parameter loading, case registry, public facade
│   ├── interface/            # ProblemGenerator adapters
│   ├── data/                 # Conservative and case-facing state types
│   ├── grid/                 # Coordinates and finite-volume metrics
│   ├── amr/                  # Hierarchy, pool, exchange, flux registers
│   ├── driver/               # Runtime dispatch and operator sequence
│   ├── numerics/             # Flux, reconstruction, integration, burn, diffusion
│   ├── physics/              # EOS, gravity, species, networks, NSE, diagnostics
│   ├── io/                   # Parameters, logging, HDF5 plot/checkpoint IO
│   └── main.cpp
├── build/                    # Generated, ignored
├── bin/                      # Generated, ignored
└── output/                   # Generated, ignored
```

## Current numerical boundaries

- Burning, diffusion, and hydro use the symmetric composition
  `B(dt/2)-D(dt/2)-H(dt)-D(dt/2)-B(dt/2)`; the coupled method is therefore at
  most second order, even when SSPRK3 is selected for hydro;
- Coarse-fine AMR faces use MUSCL-MinMod in place of PPM's wide stencil;
- Density, velocity, internal-energy, and species safeguards can modify the
  conservative update in invalid or near-vacuum states;
- CUDA parity, stable local AMR refinement retention, and self gravity remain
  outside the validated `main`-branch feature set; current KLU and generated-
  network evidence is indexed under `validation/network`, while production
  convergence remains network-specific;
- Release builds use `-march=native` and `-ffast-math`, which favor performance
  over cross-machine bitwise reproducibility;
- ARCH currently exposes source-extension interfaces rather than an installed
  public library ABI.

Convergence and production studies should follow the Reference. Official
validation claims require reproducible inputs, reference solutions, norms, and
tolerances.

## License

ARCH-authored material is released under the [MIT License](LICENSE).
Third-party-derived scientific code and data retain their upstream provenance
and terms; see [Third-Party Provenance and Notices](THIRD_PARTY_NOTICES.md).
In particular, the MIT license does not relicense the Timmes-derived networks,
NSE implementation, Helmholtz EOS, or table data. The optional KLU backend retains its SuiteSparse LGPL/BSD terms in [LICENSES](LICENSES/) and [third-party notices](THIRD_PARTY_NOTICES.md).
