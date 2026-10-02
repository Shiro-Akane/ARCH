# ARCH: Adaptive Reactive CUDA Hydrodynamics

Chinese translation: [README.zh-CN.md](README.zh-CN.md) · [Documentation overview](docs/README.md)

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)]()
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)]()
[![CPU CI](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml)
[![CPU backend](https://img.shields.io/badge/backend-CPU-success.svg)]()
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH models compressible fluid motion, heat transfer and nuclear reactions. It advances fluid state with a finite-volume method and uses adaptive mesh refinement (AMR) to add spatial resolution where needed. CPU and CUDA share the physics and mathematical implementation.

**ARCH uses CGS for inputs, outputs and physical constants**: length in `cm`, time in `s`, density in `g/cm³`, pressure and energy density in `erg/cm³`, specific internal energy in `erg/g`, and temperature in `K`. Angles use `rad`. Inputs are not converted automatically; see the [case guide](docs/guides/SimulationCase.md#units) for more units.

Begin with [Build](#build) and [First run](#first-run). After the first case, follow the [ARCH Simulation Case Guide](docs/guides/SimulationCase.md) in order. Use the [feature list](docs/Features.md) or [reference](docs/Reference.md) when you need module scope or a specific setting.

## Features and documentation

ARCH provides one- to three-dimensional fluid dynamics, dynamic AMR, equations of state, diffusion, nuclear reactions and external/self gravity on CPU and CUDA. The [separate feature list](docs/Features.md) shows the applicable modules, geometries and boundaries. The [validation index](validation/README.md) identifies tested combinations; [release notes](docs/releases/README.md) describe changes in each source version.

Physical and potential user boundaries use separate sibling sources and the
existing two public headers. See [user boundaries](docs/guides/UserBoundaries.md)
for inflow, transport flux and potential conditions.

Choose `compute_backend = cpu`, `cuda` or `auto` in the parameter file. Automatic selection occurs at startup; see the [CUDA guide](docs/CudaBackendStatus.md) for details.

## Build

Build in a Linux or WSL2 terminal. Prepare the dependencies for your backend:

- **Core tools:** a C++20 compiler, CMake 3.22+, Ninja and Git.
- **Libraries used by both builds:** HDF5 C++/HL and OpenMP.
- **CUDA builds also need:** CMake 3.25.2+, CUDA Toolkit 12.0+ and a compatible GPU driver.

The [build guide](docs/guides/Build.md) covers installation, compilation memory limits and EOS tables in source archives.

### Get the source

```bash
git clone --branch main --single-branch https://github.com/Shiro-Akane/ARCH.git
cd ARCH
```

An existing checkout can use `git pull --ff-only` after local changes are saved.

### Option A: CPU

```bash
cmake --preset cpu-release
cmake --build build-cpu --target ARCH --parallel 1
```

The executable is `build-cpu/bin/ARCH`; continue with [First run](#first-run).

### Option B: CPU and CUDA

Run on the GPU machine that will execute the calculation:

```bash
cmake --preset cuda-release
cmake --build build-cuda --target ARCH --parallel 1
```

The executable is `build-cuda/bin/ARCH` and can also run CPU cases. CUDA compilation takes more host memory and time; the [build guide](docs/guides/Build.md) covers resource guards and parallel limits. Sparse CUDA burning uses the optional cuDSS library, configured as described there.

The first Sod case needs no EOS table. Helmholtz cases need the actual table data; a Git checkout can use `git lfs pull`, while source archives follow the [table instructions](docs/guides/Build.md#source-archives-and-eos-tables).

## First run

The Sod shock tube starts with two regions of gas at different densities and
pressures. Removing the imaginary partition between them produces a shock, a
contact surface and an expanding wave. This small one-dimensional example is
a useful first check that the program runs and produces readable output.

Run it from the repository root after Option A:

```bash
export OMP_NUM_THREADS=4
./build-cpu/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Here `Sod` selects the problem definition, and the `.par` file supplies its
parameters. `OMP_NUM_THREADS` controls the number of CPU worker threads; it is
not a numerical accuracy setting. For Option B, the
executable is `./build-cuda/bin/ARCH`. To request GPU execution, add
`compute_backend = cuda` to a copy of the example parameter file and run that
copy.

A successful run prints the selected methods and a table of time steps, then
writes files under `output/first_sod/`:

```text
SodBeginner_log.dat
SodBeginner_HLLC_plt_0000.h5
SodBeginner_chk_0000.h5
...
```

The log is readable text. Files containing `_plt_` hold fluid fields for
inspection, while `_chk_` files are checkpoints for restarting a run. HDF5 is
the data format used for these binary files. This example uses 64 cells;
`simulation/Sod/Sod.par` provides the 128-cell standard shock tube, and
`simulation/Sedov/` contains a blast-wave example.

**New to ARCH? Continue with the [ARCH Simulation Case Guide](docs/guides/SimulationCase.md).** It starts from the Sod case you just ran, then explains the mesh and settings, output checks, controlled experiments and writing your own case.

## Extended models

Tabular EOS loads data that meet its [data contract](src/physics/eos/TabularEOS.md). The [generated-network guide](src/physics/network/custom/README.md) explains how to create and register a reaction network. The [reference](docs/Reference.md) describes their configuration and combination rules.

## Documentation overview

The [documentation index](docs/README.md) organizes learning, configuration, validation and contributor material. The [case guide](docs/guides/SimulationCase.md) covers output inspection and writing `Setup`/`Init`; the [reference](docs/Reference.md) describes parameters and interfaces; the [validation index](validation/README.md) explains tested scope. See the [reporting guide](docs/guides/Reporting.md) for help with a build or calculation.

## Repository map

The [source guide](src/README.md) explains module ownership and entry points. This map shows where to look first; the [contributor guide](docs/development/README.md) covers interfaces and review work.

```text
ARCH/
├── include/                 # Two public headers for user cases
├── simulation/              # Runnable cases and example inputs
├── src/
│   ├── api/                 # Configuration inspection and CPU preview for GUI clients
│   ├── core/                # Parameter definitions, parsing and case registration
│   ├── interface/           # Case setup and initial-state adapters
│   ├── data/                # Field, state and configuration records
│   ├── grid/                # Coordinates and finite-volume geometry
│   ├── amr/                 # Mesh hierarchy, transfers, exchange and reflux
│   ├── driver/              # Runtime selection, stage schedule and state lifetime
│   ├── cuda/                # Device storage, kernels and backend adapters
│   ├── numerics/            # Shared numerical algorithms
│   │   ├── flux/            # Riemann and flux-splitting policies
│   │   ├── reconstruction/  # Face states and slope limiting
│   │   ├── integrator/      # Fluid time integration
│   │   ├── diffusion/       # Diffusion operators and RKL stepping
│   │   ├── burnsolver/      # Reaction-network ODE integration
│   │   ├── linalg/          # Linear-system views and solvers
│   │   ├── elliptic/        # Poisson operators and boundary discretization
│   │   ├── multigrid/       # Multigrid levels, transfers and cycles
│   │   └── state/           # State admissibility checks
│   ├── physics/             # Shared physical models and material data
│   │   ├── eos/             # Thermodynamic closures and table readers
│   │   ├── gravity/         # External and self-gravity physics
│   │   ├── network/         # Built-in and generated reaction networks
│   │   ├── nse/             # Nuclear statistical equilibrium
│   │   ├── species/         # Composition and mixture properties
│   │   ├── diffusionCoe/    # Transport coefficients
│   │   ├── constant/        # Physical constants and units
│   │   └── diagnostics/     # Derived physical diagnostics
│   ├── io/                  # Logs, HDF5 plots and checkpoints
│   └── main.cpp             # Application entry point
├── EOS_toolkit/             # Runtime EOS tables
├── docs/                    # Guides, feature list and reference
├── validation/              # Scientific checks and recorded results
├── tests/                   # Regression tests
├── tools/                   # Build and validation utilities
└── cmake/                   # Build configuration
```

## Numerical scope

Coupled calculations inherit the temporal and physical limits of their fluid, burning, diffusion and gravity components. The [reference](docs/Reference.md) explains combination rules, state repairs and conservation diagnostics; the [validation index](validation/README.md) identifies tested inputs and errors.

## Development roadmap

These proposed directions extend the [current feature set](docs/Features.md). A question mark marks an open design; arrows show planning order rather than a software dependency.

```text
Physics:  Gravity model extensions? → MHD? → { BSSN? | Z4c? }
Software: MPI → GNN? → { FP32/FP64 selection? | RT-core acceleration? }
```

Self-gravity already covers the validated domains listed in the [feature list](docs/Features.md#self-gravity-domains); external mass sources remain a possible extension. MHD would add magnetic fields to the fluid model. BSSN and Z4c are candidate formulations for evolving spacetime.

MPI would distribute calculations across processes and machines. Later exploration may consider graph neural networks, selectable floating-point precision, and GPU ray-tracing hardware where it fits an algorithm. Numerical accuracy, performance, memory use, build efficiency and documentation remain ongoing work alongside these proposals.

## License

ARCH-owned code uses the [MIT License](LICENSE). Third-party scientific code and data retain their original provenance and terms; see the [third-party notices](THIRD_PARTY_NOTICES.md) and [license directory](LICENSES/).
