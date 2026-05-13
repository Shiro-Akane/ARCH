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

##  Project Structure

```text
ARCH/
├── bin/                    # Compiled executables (generated)
├── build/                  # CMake build directory (generated)
├── runs/                   # [Workspace] Production run directories (Git ignored)
├── simulation/             # [User Space] Problem definitions & template .par files
│   ├── Sod/                # e.g., Sod Shock Tube problem
│   └── CCSNe/              # e.g., Core-Collapse Supernova problem
├── src/                    # [Developer Space] Core source code
│   ├── core/               # SimConfig
│   ├── data/               # FluidState, UserTypes
│   ├── driver/             # Time integration driver
│   ├── grid/               # Grid management
|   ├── interface/          # Physical problem shecme
│   ├── io/                 # Input/Output (HDF5 integration)
|   ├── numerics/
|   |   ├── flux/           # Numerical schemes (SW, LF, HLLC...)
|   |   ├── integrator/     # Numerical integrator (Euler, RK2, RK3...)
|   |   ├── reconstruction/ # Numerical reconstruction method and limiter
│   ├── physics/          
│   │   ├── species/        # Species management & reactions
│   │   ├── eos/            # Equation of State (IdealGas, etc.)
│   │   └── solvers/        # Numerical schemes (SW, LF, HLLC...)
│   └── user_case/          # Template of simulation cases
└── CMakeLists.txt          # Build configuration

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
cmake ..

# 3. Compile using all available CPU cores
cmake --build .

The executable ARCH will be generated in the bin/ directory.

------------------------------------ Usage -------------------------------------
ARCH adopts a "Workspace" workflow. Do not run simulations inside the source directories.

./bin/ARCH <ProblemType> <PathToParFile>

Step-by-Step Example (Sod Shock Tube)
1. Create a run directory:

mkdir -p runs/test_sod

2. Copy the template configuration:

cp simulation/Sod/default.par runs/test_sod/arch.par

3. Run the simulation:

# Run from the root directory or the run directory
./bin/ARCH Sod runs/test_sod/arch.par

------------------------------------ Configuration -------------------------------------------

Configuration (.par File)
The behavior of the simulation is controlled by the .par file.
# ==========================================
# Simulation Identity 
# ==========================================

# ==========================================
# Grid & Time
# ==========================================
nx   = 400             # Number of cells
xmin = 0.0
xmax = 1.0
tmax = 0.25            # Max simulation time
cfl  = 0.8             # Courant factor

# ==========================================
# Numerical Scheme
# ==========================================
# Options: SW (Steger-Warming), LF (Lax-Friedrichs), LW (Lax-Wendroff)
solver  = SW

# Options: minmod, superbee, mc (only for 2nd order schemes)
limiter = minmod      

# ==========================================
# Physics (Problem Specific)
# ==========================================
gamma_left  = 1.4
rho_left    = 1.0
p_left      = 1.0
...

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