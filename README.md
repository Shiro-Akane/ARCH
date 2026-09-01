# ARCH: Adaptive Reactive CUDA Hydrodynamics

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text; update it first when behavior or interfaces
change.

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-experimental-yellow.svg)]()
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH is a block-adaptive finite-volume framework for compressible and reactive
hydrodynamics. The CPU backend is the scientific authority. An
`ARCH_ENABLE_CUDA=ON` build also provides an experimental uniform-Cartesian
backend that reuses the same registered policies and allocation-free physics
mathematics.

The project is intended for two groups:

- students learning how a CFD case is configured, initialized, advanced, and inspected;
- researchers extending Riemann solvers, equations of state, reaction networks,
  diffusion, gravity, AMR, or diagnostics directly in the source tree.

## Project status

| Capability | CPU backend | CUDA backend |
| --- | --- | --- |
| 1D/2D/3D hydrodynamics | supported | experimental; uniform Cartesian topology only |
| Dynamic block AMR | supported | rejected; device storage transactions are infrastructure, not AMR execution |
| Ideal, tabular, and Helmholtz EOS | supported | implemented; device qualification remains pending for several table paths |
| Diffusion with RKL1/RKL2 | supported | implemented for Cartesian grids |
| Gravity | none and external | none only; external/self are rejected |
| Nuclear networks and NSE | built-in and generated networks; NSE only for the four built-ins; DenseLU/optional KLU | four built-in networks with NSE and DenseLU (at most 30 species); no generated networks or sparse solve |
| Plot/checkpoint write | shared host writer | same writer; device-authoritative state is explicitly materialized first |
| Checkpoint restart | supported | rejected; `auto` may fall back to CPU before construction |
| Quantitative validation suite | CPU baselines recorded | CPU/CUDA parity remains pending |

`compute_backend = cpu`, `cuda`, or `auto` is resolved once before backend
construction. Explicit CUDA never silently falls back. `auto` may select CPU
only at that pre-construction boundary when the requested run is outside the
CUDA capability matrix or no usable device is available, and only when the CPU
capability gate accepts the same run.

The backend split is deliberately narrow: policy registration, AMR decisions,
host-only Morton/topology mathematics, EOS/network/solver mathematics,
scheduling, and HDF5 schemas are shared. CUDA-specific code owns kernels, device storage,
transfers, streams, and retirement fences.

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

For the experimental CUDA backend, use a CUDA toolkit and a device with compute
capability 8.6 or newer:

```bash
cmake -S . -B build-cuda -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_KLU=OFF
cmake --build build-cuda --parallel 2
```

KLU remains a CPU-only optional backend. cuDSS is not integrated.

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
use_burn = true
use_nse = false
linear_solver = Auto
~~~

Generated networks are currently CPU-only and do not implement the Timmes NSE
projection. `Auto` keeps the dedicated DenseLU path through 30 isotopes and
selects SparseKLU above 30 only when the CPU build has KLU enabled. The complete
contract and generator limitations are in the
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
- CUDA parity, dynamic CUDA AMR, CUDA restart input, non-Cartesian CUDA
  geometry, CUDA gravity, generated CUDA networks, and CUDA sparse solves
  remain outside the validated feature set; current KLU and generated-network
  evidence is CPU-only and indexed under `validation/network`;
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
