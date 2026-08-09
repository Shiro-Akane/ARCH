#  ARCH: Adaptive Reactive CUDA Hydrodynamics

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg)]()
[![Build System](https://img.shields.io/badge/Build-CMake-orange.svg)]()

**ARCH** is a lightweight, modular C++ framework for Compressible Fluid Dynamics (CFD). It is designed with a focus on runtime flexibility and extensibility, allowing researchers to switch solvers, equations of state, and physical problems without recompiling the source code.

##  Key Features

*   **Conservative block AMR:** Same-level ghost exchange, coarse-fine prolongation/restriction, flux-register reflux, and per-thread flux reduction preserve finite-volume evolution across arbitrary refinement levels.
*   **Dimension-aware memory layout:** nblockx3 = 0 creates true 2D tiles; nblockx2 = nblockx3 = 0 creates true 1D tiles while retaining a contiguous block pool.
*   **Shock-capturing hydrodynamics:** PCM, MUSCL, and PPM reconstruction combine with Euler, SSPRK2, or SSPRK3 integration and SW, VL, Roe, HLL, or HLLC fluxes.
*   **AMR-aware diffusion:** RKL2 super-time-stepping advances the composite hierarchy with stage-wise halo synchronization and reflux; RKL1 is available for controlled first-order studies.
*   **Composable physics:** External gravity, nuclear burning, ideal/tabular/Helmholtz EOS policies, and Helmholtz diffusionCoe transport participate in AMR evolution.
*   **Canonical diagnostics:** AMR and HDF5 output share DENS, PRES, TEMP, velocity, VORT, DIVV, ENTR, ENUC, and registered-species fields.
*   **Restartable HDF5 IO:** Plot and checkpoint files preserve AMR hierarchy state for restart; fresh output directories are created before logging.
*   **Case-facing interface:** A problem case includes UserInterface.h and GlobalDefs.h; public setup helpers cover built-in networks and EOS pressure initialization.


---

## Project Structure

```text
ARCH/
├── bin/                        # Compiled executables (generated)
├── build/                      # CMake build directory (generated)
├── runs/                       # [Workspace] Production run directories (Git ignored)
├── output/                     # Generated simulation outputs (Git ignored)
├── Validation_file/            # Reproducible AMR validation archive
├── simulation/                 # [User Space] Problem definitions & template .par files
│   ├── Cellular/               # Cellular detonation problem
│   ├── RTinstability/          # Rayleigh-Taylor instability problem
│   └── Sod/                    # Sod shock tube problem
├── src/                        # [Developer Space] Core source code
├── amr/                    # Tree, block pool, ghost exchange, and reflux
│   ├── core/                   # Runtime parameters & configuration (SimConfig)
│   ├── data/                   # Data structures (FluidState, UserTypes)
│   ├── driver/                 # Time integration and main simulation driver
│   ├── grid/                   # Active-dimension grids and geometric metrics
│   ├── interface/              # Abstract interfaces for physics & numerical methods
│   ├── io/                     # Logging, HDF5 plot, and checkpoint restart IO
│   ├── numerics/               # Core numerical methods
│   │   ├── burnsolver/         # ODE solvers for nuclear burning (BE_NR, etc.)
│   │   ├── diffusion/          # Diffusion time integrators (RKL1, RKL2)
│   │   ├── flux/               # Riemann solvers and flux calculation (SW, LF, HLLC)
│   │   ├── integrator/         # Time integration schemes (Euler, RK2, RK3)
│   │   ├── linalg/             # Linear algebra solvers (DenseLU, etc.)
│   │   └── reconstruction/     # Spatial reconstruction & limiters (PCM, PLM)
│   ├── physics/                # Physical models
│   ├── diagnostics/        # Metric-aware derived fluid diagnostics
│   │   ├── diffusionCoe/       # Diffusion coefficients calculation
│   │   ├── eos/                # Equations of State (IdealGas, Helmholtz, Tabular)
│   │   ├── gravity/            # Gravity policies (none and external)
│   │   ├── network/            # Nuclear reaction networks (aprox19, etc.)
│   │   ├── nse/                # Nuclear Statistical Equilibrium (NSE) solver
│   │   └── species/            # Fluid species and reaction management
│   └── main.cpp                # Simulation entry point
└── CMakeLists.txt              # CMake build configuration
```

---

## Dependencies & Prerequisites

To build and run ARCH, you need the following environment:

- **C++ Compiler**: GCC 10+ / Clang 10+ (Must support C++20)
- **CMake**: Version 3.18 or higher
- **OpenMP**: For multi-threading parallelization
- **HDF5**: C++ High-Level (HL) libraries for data output

> [!TIP]
> We recommend using Conda to manage dependencies. A provided `environment.yml` (if available) can be used to set up the toolchain.

---

## Build Instructions

ARCH uses CMake for compilation. Ensure you have a C++20 compatible compiler.

1. **Create build directory**
```bash
mkdir build && cd build
```

2. **Configure CMake**
```bash
# Release mode is highly recommended for performance.
# OpenMP is enabled by default. To explicitly set it, use -DARCH_ENABLE_OPENMP=ON|OFF
cmake -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON ..
```

3. **Compile**
```bash
# Compile using all available CPU cores
cmake --build . -j$(nproc)
```

The executable `ARCH` will be generated in the `bin/` directory.

---

## Simulation Setup & Usage

ARCH adopts a "Workspace" workflow. **Do not run simulations inside the source directories.**

For detailed instructions on how to set up a new problem case, run a simulation (such as the Sod Shock Tube), and configure the `.par` parameter file, please refer to:
👉 **[Simulation Case & Configuration Guide](simulation/CaseGuide.md)**

AMR validation figures, stored-plot inputs, and the reproducible renderer are kept
together in [AMR visual validation archive](Validation_file/AMR_Visual_Archive/README.md).


---

## Extending ARCH (Developer Guide)

ARCH is designed with a plugin-style architecture using zero-overhead static dispatch.
To add new physical modules or solvers, follow these guidelines:

### 1. Hydro Solver (Riemann Solver / Flux)
- **Location**: `src/numerics/flux/`
- **Interface**: Implement a flux function or Riemann solver class (e.g., `FluxSW.h`, `FluxHLLC.h`).
- **Registration**: Register the implementation in `src/driver/SolverDispatch.cpp`.

### 2. Equation of State (EOS)
- **Location**: `src/physics/eos/`
- **Interface**: Create a class (e.g., `MyNewEOS`) that implements the state evaluation methods.
- **Tabular EOS**: If adding a new tabular format, you must implement a Host Manager and a device-compatible View (refer to `Tabular3DEOS.h`).
- **Registration**: Register the new EOS parser logic in `dispatch_eos()` within `src/physics/eos/eosdispatch.h`.

### 3. Nuclear Reaction Network
- **Location**: `src/physics/network/`
- **Interface**: Provide a Struct/Class that satisfies the `NetType` interface, defining isotope lists and rate computations (e.g., `NetAprox19`).
- **Registration**: Register the network inside `dispatch()` in `src/numerics/burnsolver/BurnDispatch.h`.

### 4. ODE Solver (for Nuclear Burning)
- **Location**: `src/numerics/burnsolver/`
- **Interface**: Create a solver wrapper template `Solver_NEW<NetType, MatrixType, LinearSolver>` that implements the `integrate(...)` method.
- **Registration**: Add the new solver string matching to `dispatch_ode()` in `src/numerics/burnsolver/BurnDispatch.h`.

### 5. Linear Algebra Solver (Linalg)
- **Location**: `src/numerics/linalg/`
- **Interface**: Provide a matrix and solver wrapper (e.g., `DenseLUSolver`) that implements the required linear solver interfaces for stiff ODE integration.
- **Registration**: Register it in `dispatch_linsolver()` within `src/numerics/burnsolver/BurnDispatch.h`.

### 6. Diffusion Solver
- **Location**: `src/numerics/diffusion/`
- **Interface**: Provide a class with a static `integrate` method matching:
  `void integrate(FluidState&, const auto&, const Grid&, const SimConfig&, double, double, const auto&)`
- **Registration**: Add your solver to the conditional dispatch in `dispatch_diffusion()` within `src/numerics/diffusion/DiffDispatch.h`.

### 7. Gravity Solver
- **Location**: `src/physics/gravity/`
- **Interface**: Create a class implementing the gravity calculation logic (e.g., `ExternalGravity.h`).
- **Registration**: Register it in `dispatch_gravity()` within `src/physics/gravity/GravityDispatch.h`.

### 8. Add a New Problem Case
- **Location**: `simulation/MyNewCase/`
- **Interface**: Inherit from `ProblemGenerator`. Implement `Setup(SimConfig&, SpeciesManager&)` and `Init(const PointCoords&, PrimitiveData&) const`.
- **Registration**: Use the `REGISTER_PROBLEM_CLASS("MyNewCase", MyNewCaseClass)` macro at the bottom of your file. See `simulation/CaseGuide.md` for a full template.

---

## License
This project is licensed under the MIT License