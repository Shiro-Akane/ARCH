Adaptive Reactive CUDA Hydrodynamics (ARCH): Extensible Astrophysical Hydrodynamics Solver

--------------------------------------------------------------------------------
ARCH is a lightweight, modular C++ framework for 1D Compressible Fluid Dynamics (CFD). It is designed with a focus on runtime flexibility and extensibility, allowing researchers to switch solvers, equations of state, and physical problems without recompiling the source code.

--------------------------------------------------------------------------------
Key Features:

    1.  Runtime Polymorphism: Switch numerical schemes (e.g., SW, LF, Roe) and parameters via configuration files (.par).

    2.  Modular Architecture: Strict separation between Core (Driver, Grid), Physics (Solvers, EoS), and User Problems (Sod, Sedov, CCSNe).

    3.  Factory Pattern: Automatic registration and dispatching of Solvers and Problem types.

    4.  Safety Mechanisms: Built-in "Problem ID" checks to prevent using the wrong parameter file for a simulation case.

    5.  Header-only Solvers: High-performance template-based solver implementation.

--------------------------------------------------------------------------------------

ARCH/
├── bin/                  # Compiled executables
├── build/                # CMake build directory
├── runs/                 # [Workplace] Production run directories (Git ignored)
├── simulation/           # [User Space] Problem definitions & template .par files
│   ├── Sod/              # e.g., Sod Shock Tube problem
│   └── CCSNe/            # e.g., Core-Collapse Supernova problem
├── src/                  # [Developer Space] Core source code
│   ├── core/             # SimConfig
|   ├── data/             # FluidState, UserTypes
│   ├── driver/           # Time integration driver
|   ├── grid/             # Grid
|   ├── io/               # io
│   └── physics/
|   |   ├── species/      # species managment
│   |   ├── eos/          # Equation of State (IdealGas, etc.)
│   |   └── solvers/      # Numerical schemes (SW, LF, HLLC...)
|   └──user_case          # template of simulation case
└── CMakeLists.txt        # Build configuration

------------------------------ Build Instructions -------------------------------

ARCH uses CMake for compilation. Ensure you have a C++17 compatible compiler (GCC, Clang, or MSVC).

# 1. Create build directory
mkdir build && cd build

# 2. Configure (Release mode recommended for performance)
cmake -DCMAKE_BUILD_TYPE=Release ..

# 3. Compile
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