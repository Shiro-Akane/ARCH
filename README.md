#  ARCH: Adaptive Reactive CUDA Hydrodynamics

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++17](https://img.shields.io/badge/Standard-C%2B%2B17-blue.svg)]()
[![Build System](https://img.shields.io/badge/Build-CMake-orange.svg)]()

**ARCH** is a lightweight, modular C++ framework for Compressible Fluid Dynamics (CFD). It is designed with a focus on runtime flexibility and extensibility, allowing researchers to switch solvers, equations of state, and physical problems without recompiling the source code.

##  Key Features

*   **Runtime Polymorphism:** Switch numerical schemes (e.g., SW, LF, Roe, HLLC) and parameters via configuration files (`.par`).
*   **Modular Architecture:** Strict separation between Core (Driver, Grid), Physics (Solvers, EoS), and User Problems (Sod, Sedov, CCSNe).
*   **Factory Pattern:** Automatic registration and dispatching of Solvers and Problem types.
*   **Safety Mechanisms:** Built-in "Problem ID" checks to prevent using the wrong parameter file for a simulation case.
*   **High Performance:** Header-only template-based solver implementations with OpenMP parallelization support.

---

## Project Structure

```text
ARCH/
├── bin/                        # Compiled executables (generated)
├── build/                      # CMake build directory (generated)
├── runs/                       # [Workspace] Production run directories (Git ignored)
├── output/                     # Simulation outputs (HDF5/plot files)
├── simulation/                 # [User Space] Problem definitions & template .par files
│   ├── Cellular/               # Cellular detonation problem
│   ├── RTinstability/          # Rayleigh-Taylor instability problem
│   └── Sod/                    # Sod shock tube problem
├── src/                        # [Developer Space] Core source code
│   ├── core/                   # Runtime parameters & configuration (SimConfig)
│   ├── data/                   # Data structures (FluidState, UserTypes)
│   ├── driver/                 # Time integration and main simulation driver
│   ├── grid/                   # Grid management and parallelization logic
│   ├── interface/              # Abstract interfaces for physics & numerical methods
│   ├── io/                     # I/O handling (HDF5 integration & logging)
│   ├── numerics/               # Core numerical methods
│   │   ├── burnsolver/         # ODE solvers for nuclear burning (BE_NR, etc.)
│   │   ├── flux/               # Riemann solvers and flux calculation (SW, LF, HLLC)
│   │   ├── integrator/         # Time integration schemes (Euler, RK2, RK3)
│   │   ├── linalg/             # Linear algebra solvers (DenseLU, etc.)
│   │   └── reconstruction/     # Spatial reconstruction & limiters (PCM, PLM)
│   └── physics/                # Physical models
│       ├── eos/                # Equations of State (IdealGas, Helmholtz)
│       ├── gravity/            # Gravity module (Self-gravity, External)
│       ├── network/            # Nuclear reaction networks (aprox19)
│       └── species/            # Fluid species and reaction management
├── EOS_toolkit/                # Equation of State generation & analysis tools
├── Validation_file/            # Reference data & validation scripts
└── CMakeLists.txt              # CMake build configuration
```

---

##   Dependencies & Prerequisites
*    **To build and run ARCH, you need the following environment:

C++ Compiler: GCC 9+ / Clang 10+ (Must support C++17)

CMake: Version 3.15 or higher

OpenMP: For multi-threading parallelization

HDF5: C++ High-Level (HL) libraries for data output

Note: We recommend using Conda to manage dependencies. A provided environment.yml (if available) can be used to set up the toolchain.

------------------------------ Build Instructions -------------------------------

ARCH uses CMake for compilation. Ensure you have a C++17 compatible compiler (GCC, Clang, or MSVC).

# 1. Create build directory
mkdir build && cd build

# 2. Configure (Release mode recommended for performance)
# OpenMP is enabled by default. To explicitly set it, use -DARCH_ENABLE_OPENMP=ON|OFF
cmake -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON ..

# 3. Compile using all available CPU cores
cmake --build . -j$(nproc)

The executable ARCH will be generated in the bin/ directory.

------------------------------------ Usage -------------------------------------
ARCH adopts a "Workspace" workflow. Do not run simulations inside the source directories.

# OpenMP Configuration
# Set the number of threads for OpenMP parallelization before running
export OMP_NUM_THREADS=4 

./bin/ARCH <ProblemType> <PathToParFile>

Step-by-Step Example (Sod Shock Tube)
1. Create a run directory:

mkdir -p runs/test_sod

2. Copy the template configuration:

cp simulation/Sod/default.par runs/test_sod/arch.par

3. Run the simulation:

# Set OpenMP threads and run from the root directory or the run directory
export OMP_NUM_THREADS=4
./bin/ARCH Sod runs/test_sod/arch.par

------------------------------------ Configuration -------------------------------------------

Configuration (.par File)
The behavior of the simulation is controlled by the .par file. 
Below is a comprehensive list of available parameters based on `RuntimeParams.h`:

# ==========================================
# Grid
# ==========================================
geometry = cartesian   # cartesian, cylindrical, spherical
nx = 400               # Number of cells in X direction
ny = 1                 # Number of cells in Y direction
nz = 1                 # Number of cells in Z direction
x_min = 0.0            # Min X (supports expressions like "pi", "2.0*pi")
x_max = 1.0            # Max X
xl_boundary_type = outflow # outflow, periodic, reflecting, etc.
xr_boundary_type = outflow

# ==========================================
# Numerical Scheme
# ==========================================
solver = SW            # SW, LF, Roe, HLLC, etc.
cfl = 0.8              # Courant factor
limiter = minmod       # minmod, superbee, mc
reconstruct = pcm      # pcm, plm, ppm
timeintegrator = RK2   # Euler, RK2, RK3
EntropyFix = On        # Entropy fix (On/Off)
EntropyFixCoefficient = 0.1 

# ==========================================
# Physics (EOS & Gravity)
# ==========================================
eos_type = ideal       # ideal, helmholtz, tabular, etc.
gamma = 1.4            # Ratio of specific heats for ideal gas
gravity_type = none    # none, external, self
# gravity_g_x = -9.81  # For external gravity
# gravity_G = 6.67e-8  # For self gravity

# ==========================================
# Nuclear Burn & ODE Solver
# ==========================================
use_burn = 0           # 1 to enable nuclear burning
network_name = aprox19 # Nuclear network name
ode_solver = BE_NR     # ODE solver: BE_NR, etc.
ode_rtol = 1e-4        # Relative tolerance for ODE
ode_atol = 1e-8        # Absolute tolerance for ODE

# ==========================================
# I/O & Output
# ==========================================
tmax = 0.25            # Max simulation time
max_steps = -1         # Max steps (-1 for unlimited)
out_dir = data         # Output directory
base_name = arch       # Output base name
plt_dt = 0.01          # Time interval for plot files
chk_dt = 0.1           # Time interval for checkpoint files
restart = false        # Set to true to resume from restart_file
plt_variables = all    # all, conserved, or comma-separated (rho,u,p,eng)

--------------------------------------------------------------------------------------

How to add a new Solver?
Create src/physics/solvers/SolverNew.h.

Implement the solver class (must satisfy the template interface).

Register it in src/physics/solvers/SolverFactory.h inside the DispatchSolver function.

--------------------------------------------------------------------------------------
How to add a new Problem Case?
Create a folder simulation/MyNewCase/.

Create MyNewCase.h and inherit from ProblemGenerator.

Implement InitializeData(), GetConfig(), and GetProblemID().

Register it in src/simulation/ProblemFactory.h.

License
This project is licensed under the MIT License