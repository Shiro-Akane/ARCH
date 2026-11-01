# ARCH: Adaptive Reactive CUDA Hydrodynamics

[简体中文](README.zh-CN.md) · [Documentation](docs/README.md)

[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](docs/guides/Build.md)
[![Build](https://img.shields.io/badge/build-CMake-orange.svg)](docs/guides/Build.md)
[![CPU CI](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Shiro-Akane/ARCH/actions/workflows/ci.yml)
[![CUDA](https://img.shields.io/badge/CUDA-supported-success.svg)](docs/CudaBackendStatus.md)
[![ARCH code: MIT](https://img.shields.io/badge/ARCH_code-MIT-yellow.svg)](LICENSE)

ARCH models compressible fluid motion, heat transfer and nuclear reactions. It uses finite-volume methods and adaptive mesh refinement (AMR) to resolve evolving flow structures. CPU and CUDA share the physics and mathematical implementation.

**Linux is recommended**, including Linux under WSL2. Native Windows 11 x64 also supports the CPU/CUDA command-line program and the complete Studio workflow, but may still have platform-specific instability or bugs. See [platform support](docs/releases/PlatformSupport.md) for tested configurations and limitations. **Studio is optional** for scientific calculations.

## Features and documentation

- One- to three-dimensional fluid dynamics, dynamic AMR and Cartesian, spherical and cylindrical coordinates.
- Ideal-gas and tabulated equations of state, thermal/viscous/species diffusion, nuclear reactions, and external/self gravity.
- CPU/OpenMP and CUDA execution, HDF5 output and compatible checkpoint restart.
- A desktop workbench for parameters, initial previews, builds, runs and output inspection.

The [feature list](docs/Features.md) describes supported combinations. Use the [reference](docs/Reference.md) for parameters and interfaces, and [validation](validation/README.md) for reference problems, numerical errors and convergence.

ARCH uses **CGS units**: length in cm, time in s, density in g/cm³, pressure and energy density in erg/cm³, specific internal energy in erg/g, temperature in K, and angles in rad. Inputs are not converted automatically. The [case guide](docs/guides/SimulationCase.md#units) explains units and output interpretation.

## Build

Install the [platform tools](docs/guides/Build.md#tools-and-dependencies), then run these commands from the repository root. Preparation reuses or builds matching HDF5; configuration supplies HighFive and KLU through the shared CMake dependency setup.

| Requirement | Linux / WSL2 | Native Windows 11 x64 |
| --- | --- | --- |
| CPU | C++20 GCC, CMake 3.22+, Ninja and Git | GCC/UCRT64 and UCRT64 CMake/Ninja on the PowerShell PATH, native Git |
| CUDA | CMake 3.25.2+, CUDA Toolkit 12.0+, compatible host compiler and GPU driver | NVCC, MSVC 14.44 x64 Developer terminal, Kitware Windows CMake, Windows SDK and compatible GPU driver |
| Optional Studio | Native x64 Node 24+, npm and graphical session/WSLg | Native x64 Node 24+, npm and GNU objdump |

### Get the source

```bash
git clone --branch main --single-branch https://github.com/Shiro-Akane/ARCH.git
cd ARCH
```

An existing checkout can use `git pull --ff-only` after local changes are saved.

### Option A: CPU

Linux / WSL2:

**Prepare dependencies**

```bash
cmake -DARCH_PROFILE=linux-core -P cmake/dependencies/Prepare.cmake
```

**Configure**

```bash
cmake --preset linux-core
```

**Build**

```bash
cmake --build --preset linux-core
```

Windows PowerShell, with the [UCRT64 tool directory on PATH](docs/guides/Build.md#windows-cpu-installation):

**Prepare dependencies**

```powershell
cmake -DARCH_PROFILE=windows-core -P cmake/dependencies/Prepare.cmake
```

**Configure**

```powershell
cmake --preset windows-core
```

**Build**

```powershell
cmake --build --preset windows-core
```

Repeat only the build command after ordinary source edits. Executables are `build/linux-core/bin/ARCH` and `build/windows-core/bin/ARCH.exe`; the first run is shown below.

### Option B: CPU and CUDA

Linux / WSL2, on the GPU machine:

**Prepare dependencies**

```bash
cmake -DARCH_PROFILE=linux-core-with-cuda -P cmake/dependencies/Prepare.cmake
```

**Configure**

```bash
cmake --preset linux-core-with-cuda
```

**Build**

```bash
cmake --build --preset linux-core-with-cuda
```

Windows, in the [MSVC 14.44 x64 Developer Command Prompt](docs/guides/Build.md#windows-cuda-installation):

**Prepare dependencies**

```bat
cmake -DARCH_PROFILE=windows-core-with-cuda -P cmake/dependencies/Prepare.cmake
```

**Configure**

```bat
cmake --preset windows-core-with-cuda
```

**Build**

```bat
cmake --build --preset windows-core-with-cuda
```

These builds provide **CPU and CUDA in the same executable**, under `build/<profile>/bin/`. Set `compute_backend = cpu`, `cuda` or `auto` in the parameter file to select execution at runtime. cuDSS is optional for sparse CUDA burning; ordinary GPU calculations and compact DenseLU burning work without it. The [build guide](docs/guides/Build.md) covers custom tool paths, dependencies and build memory.

The first Sod case needs no EOS table. Helmholtz cases need table data: use `git lfs pull` in a checkout, or follow the [source-archive instructions](docs/guides/Build.md#source-archives-and-eos-tables).

## First run

Sod is a small one-dimensional shock tube. Two initially separated gas states produce a shock, contact surface and expanding wave, making it a useful first calculation.

Linux/WSL2:

```bash
export OMP_NUM_THREADS=4
./build/linux-core/bin/ARCH Sod simulation/Sod/Sod_beginner.par
```

Windows Command Prompt; use the initialized Developer terminal for a CUDA build:

```bat
call build\windows-core\bin\arch-env.cmd
set OMP_NUM_THREADS=4
build\windows-core\bin\ARCH.exe Sod simulation/Sod/Sod_beginner.par
```

`arch-env.cmd` supplies the runtime DLL paths selected during configuration. For CUDA, replace `windows-core` with `windows-core-with-cuda` in both paths.

`Sod` selects the compiled problem definition; the `.par` file supplies its parameters. `OMP_NUM_THREADS` controls CPU worker threads. To use a CUDA-enabled build, select its executable and change the existing `compute_backend` assignment in a copy of the input.

The program prints time-step diagnostics and writes a log, Plotfiles (`*_plt_*.h5`) and restart checkpoints (`*_chk_*.h5`) under `output/first_sod/`. The beginner input uses 64 cells; `simulation/Sod/Sod.par` provides the 128-cell example.

**Paths inside `.par` files resolve from the calculation's working directory, not the parameter file's directory.** Relative paths such as `output/first_sod` work on both systems. Adapt absolute Linux/WSL paths before using the same file on Windows; see [paths in Studio and parameter files](docs/guides/Studio.md#paths-on-linux-and-windows).

Continue with the [Simulation Case Guide](docs/guides/SimulationCase.md) to inspect output, adjust parameters and write `Setup`/`Init` for a new problem.

## ARCH Studio

Studio brings parameter editing, Configure/Build, initial-field and initial-AMR Preview, Run/Restart and read-only Plotfile viewing into one desktop workbench.

![ARCH Studio displaying a two-dimensional Sedov density field with adaptive mesh structure](docs/guides/images/studio-overview.png)

*Inspect an evolved density field, display the adaptive mesh and query stored cell values. The [illustrated Studio guide](docs/guides/Studio.md) walks through configuration, preview and output inspection.*

Add Studio to the Core build you already configured. Install its [desktop prerequisites](docs/guides/Build.md#studio-desktop), then enable the option, build and launch:

Linux / WSL2:

**Enable Studio in this Core tree**

```bash
cmake -S . -B build/linux-core -DARCH_BUILD_STUDIO=ON
```

**Build the desktop**

```bash
cmake --build build/linux-core --target arch-studio --parallel 1
```

**Open the empty start page**

```bash
build/linux-core/bin/arch-studio
```

Windows PowerShell, in the same compiler environment used for Core:

**Enable Studio in this Core tree**

```powershell
cmake -S . -B build/windows-core -DARCH_BUILD_STUDIO=ON
```

**Build the desktop**

```powershell
cmake --build build/windows-core --target arch-studio --parallel 1
```

**Open the empty start page**

```powershell
& .\build\windows-core\bin\arch-studio.cmd
```

For an existing CUDA build, use `linux-core-with-cuda` or `windows-core-with-cuda` in these paths. Studio reuses that executable and its CPU/CUDA capabilities. The Windows launcher supplies its tool configuration automatically.

The empty start page lets you choose the project, build, executable, model and `.par`. To open a model directly, use the [one-command Sedov example](docs/guides/Studio.md#launch-with-a-model-and-parameter-file). `--config` loads a parameter file into **Config**; **Configure** runs CMake, and **Build** compiles the program. The [build guide](docs/guides/Build.md#build-and-launch-the-desktop) also covers adding Studio to a manually configured build.

## Extended models

Tabular EOS uses the [table data contract](src/physics/eos/TabularEOS.md). The [generated-network guide](src/physics/network/custom/README.md) explains how to create and register a reaction network. [User boundaries](docs/guides/UserBoundaries.md) covers fluid and potential boundary conditions.

## Repository map

| Directory | Contents |
| --- | --- |
| `include/`, `simulation/` | Public case headers, runnable problems and example inputs |
| `src/` | Shared physics/numerics, mesh, driver, CPU/CUDA execution and I/O |
| `studio/` | Desktop window, local Host service and workbench |
| `EOS_toolkit/` | EOS tables and preparation tools |
| `docs/` | User guides, reference and developer documentation |
| `validation/`, `tests/` | Scientific references and engineering regressions |
| `tools/`, `cmake/` | Build, validation and configuration utilities |

See the [source guide](src/README.md) and [contributor guide](docs/development/README.md) for implementation ownership. Use the [reporting guide](docs/guides/Reporting.md) for a build or numerical problem.

## Citation

For v1.4.1, cite Haonan Ye, Enjie Jiang and Yu Ding, *ARCH: Adaptive Reactive CUDA Hydrodynamics*, version 1.4.1, 2026-11-02, [GitHub release](https://github.com/Shiro-Akane/ARCH/releases/tag/v1.4.1).

[CITATION.cff](CITATION.cff) provides machine-readable metadata. The series DOI is [10.5281/zenodo.23503924](https://doi.org/10.5281/zenodo.23503924); the version DOI for v1.3.0 is [10.5281/zenodo.23503925](https://doi.org/10.5281/zenodo.23503925). A v1.4.1 Zenodo version has not been published. Cite the version used for your results; details are in the [citation guide](docs/zenodo/README.md).

## License

ARCH-owned code uses the [MIT License](LICENSE). Third-party scientific code and data retain their original terms; see the [third-party notices](THIRD_PARTY_NOTICES.md) and [license directory](LICENSES).
