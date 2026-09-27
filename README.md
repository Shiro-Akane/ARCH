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

Begin with [Build](#build) and [First run](#first-run). The [feature list](docs/Features.md) and [reference](docs/Reference.md) describe available models and their combination rules.

## Features and documentation

ARCH provides one- to three-dimensional fluid dynamics, dynamic AMR, equations of state, diffusion, nuclear reactions and external/self gravity on CPU and CUDA. The [separate feature list](docs/Features.md) shows the applicable modules, geometries and boundaries. The [validation index](validation/README.md) identifies tested combinations; [release notes](docs/releases/README.md) describe changes in each source version.

Choose `compute_backend = cpu`, `cuda` or `auto` in the parameter file. Automatic selection occurs at startup; see the [CUDA guide](docs/CudaBackendStatus.md) for details.

## Build

Build in Linux or a WSL2 terminal. CPU builds need a C++20 compiler, CMake 3.22+, Ninja, HDF5 C++/HL, OpenMP and Git. CUDA adds CMake 3.25.2+, CUDA Toolkit 12.0+ and a compatible driver. See the [build guide](docs/guides/Build.md) for dependency setup, memory limits and EOS tables in source archives.

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
the data format used for these binary files. The
[case guide](docs/guides/SimulationCase.md) explains how to inspect the fields
and compare runs. This example uses 64 cells; `simulation/Sod/Sod.par` provides
the 128-cell standard shock tube, and `simulation/Sedov/` contains a blast-wave
example.

## Extended models

Tabular EOS loads data that meet its [data contract](src/physics/eos/TabularEOS.md). The [generated-network guide](src/physics/network/custom/README.md) explains how to create and register a reaction network. The [reference](docs/Reference.md) describes their configuration and combination rules.

## Documentation overview

The [documentation index](docs/README.md) organizes learning, configuration, validation and contributor material. The [case guide](docs/guides/SimulationCase.md) covers output inspection and writing `Setup`/`Init`; the [reference](docs/Reference.md) describes parameters and interfaces; the [validation index](validation/README.md) explains tested scope. See the [reporting guide](docs/guides/Reporting.md) for help with a build or calculation.

## Repository map

```text
ARCH/
├── simulation/  # runnable cases and example inputs
├── src/         # fluid, physics, numerics and backends
├── EOS_toolkit/ # runtime table data
├── docs/        # guides, feature list and reference
├── validation/  # scientific checks and results
├── tests/       # regression tests
├── tools/       # build and validation utilities
└── cmake/       # build configuration
```

Browse the [source map](src/README.md) for module entry points.

## Numerical scope

Coupled calculations inherit the temporal and physical limits of their fluid, burning, diffusion and gravity components. The [reference](docs/Reference.md) explains combination rules, state repairs and conservation diagnostics; the [validation index](validation/README.md) identifies tested inputs and errors.

## License

ARCH-owned code uses the [MIT License](LICENSE). Third-party scientific code and data retain their original provenance and terms; see the [third-party notices](THIRD_PARTY_NOTICES.md) and [license directory](LICENSES/).
