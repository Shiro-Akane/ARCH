# ARCH Simulation Case Development Guide

Welcome to the `simulation` directory! This is where you define your custom physics problems.
ARCH uses a **plugin-style, object-oriented architecture**, meaning you can add new simulation cases simply by creating a new `.cpp` file containing your problem class. CMake will automatically discover and compile it.

---

## 🚀 How to Add a New Problem

1. **Create your Case File**: Create a new `.cpp` file in a sub-folder (e.g., `simulation/MySupernova/MySupernova.cpp`).
2. **Implement the Class**: Create a class and fill in the `Setup` and `Init` methods.
3. **Register**: Use the `REGISTER_PROBLEM_CLASS` macro at the bottom of your file.
4. **Compile**: Run `make` inside your `build/` directory.
5. **Run**: `./bin/ARCH MyProblemName path/to/my_config.par`.

---

## 📚 Interface Reference

To create a problem, you define a class that implements two core methods: `Setup` and `Init`. By encapsulating your problem into a class, you avoid polluting the global namespace and can cleanly store parameters as class member variables.

### 1. Headers

You **only** need to include two headers to interact with the core engine. You DO NOT need to include complex underlying physical headers (like EOS or Reaction Networks).

```cpp
#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
```

### 2. Setup Function (`Setup`)
**Signature:** `void Setup(SimConfig &config, SpeciesManager &specs)`

This function runs **once** during system initialization. Use it to:
* Read custom parameters from the `.par` file.
* Save these parameters to your class member variables.
* Register chemical/nuclear species.

### 3. Initialization Function (`Init`)
**Signature:** `void Init(const PointCoords &p, PrimitiveData &out) const`

This function runs **for every cell** during grid initialization. The system passes you the cell's spatial coordinates `p.x, p.y, p.z`. Your task is to populate the `out` struct with the initial fluid state.

---

## 🛠 User-Callable API & Functions

Below are all the standard functions and data structures you will interact with when writing a case.

### ⚙️ SimConfig (Reading Parameters)
Passed into `Setup`. Used to read custom variables defined in your `.par` file.

* `config.Get<T>(const std::string &key, T default_val)`
  * **Description**: Reads a variable from the `.par` file. If the variable doesn't exist, it returns the `default_val`.
  * **Example**: `double radius = config.Get<double>("bubble_radius", 1.5);`
  * **Example**: `int mode = config.Get<int>("perturbation_mode", 2);`

### 🧪 SpeciesManager (Managing Isotopes/Materials)
Passed into `Setup`. Used to define or look up the fluids/isotopes in your simulation.

* `int specs.add_species(std::string name, double A, double Z, double gamma, double Cv)`
  * **Description**: Manually registers a new fluid or isotope into the system and returns its unique ID.
  * **Parameters**:
    * `name`: The string identifier (e.g., `"Hydrogen"`, `"DriverGas"`). This name will appear in the output `.h5` files.
    * `A`: **Mass Number** (Atomic Weight). For a macroscopic ideal gas, you can usually set this to 1.0. For real isotopes (e.g., C12), it is 12.0.
    * `Z`: **Atomic Number** (Proton Number). Defines the charge. For ideal gases, typically 1.0. For C12, it is 6.0.
    * `gamma`: **Specific Heat Ratio** ($\gamma = C_p / C_v$). For example, 1.4 for air, 5.0/3.0 for monatomic gas.
    * `Cv`: **Heat Capacity at Constant Volume** ($C_v$). Units are J/(kg·K) or erg/(g·K) depending on your unit system.
  * **Example**: `int air_id = specs.add_species("Air", 1.0, 1.0, 1.4, 717.5);`

* `int specs.GetSpeciesID(const std::string &target_name)`
  * **Description**: Looks up the ID of a species that has already been loaded. This is **highly useful** when you have activated a nuclear network (like `aprox19`) in your `.par` file, as the system will automatically pre-load all the network isotopes for you.
  * **Example**: `int c12_id = specs.GetSpeciesID("c12");` (Case-insensitive)

### 📐 PointCoords (Coordinate System Independence)
Passed into `Init`. An extremely powerful feature of ARCH is its **Geometry Decoupling Design** (see `src/grid/Grid.h`). When setting initial conditions in your `case.cpp`, you can completely **ignore** the `geometry` parameter set in the `.par` file (which only dictates how the solver computes fluxes and volumes). 

The `Grid` module automatically projects every cell into **all three coordinate systems simultaneously**, assuming the physical origin $(0,0,0)$ is always aligned at $x=0, y=0, z=0$. You can initialize a symmetric shape using the easiest coordinate system, while running the actual simulation on a completely different mesh!

* `p.x`, `p.y`, `p.z` *(double)*: Cartesian coordinates.
* `p.r`, `p.theta`, `p.phi` *(double)*: Spherical coordinates. (e.g., $r=\sqrt{x^2+y^2+z^2}$)
* `p.r_cy`, `p.phi_cy`, `p.z_cy` *(double)*: Cylindrical coordinates. (e.g., $r_{cy}=\sqrt{x^2+y^2}$)

*(Note: In 1D or 2D setups, the missing dimensions are safely handled. For instance, in 2D spherical/cylindrical geometry, it perfectly degenerates to polar coordinates on the plane.)*

**Equivalent Representation Examples:**
Imagine you want to place a high-density spherical bubble of radius `0.5` at the origin. 
* **Hard way (using computational Cartesian coordinates):**
  ```cpp
  if (std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z) < 0.5) { ... }
  ```
* **Smart way (using the unified spherical representation):**
  ```cpp
  if (p.r < 0.5) { ... }
  ```
This works flawlessly **even if your `.par` file sets `geometry = cartesian`**. ARCH handles the underlying mapping, allowing your initial condition code to remain clean and mathematically intuitive.

### 🌊 PrimitiveData (Setting Fluid States)
Passed into `Init`. Used to assign the physical values to a specific grid cell.

* `out.rho` *(double)*: The fluid mass density ($\rho$).
* `out.p` *(double)*: The fluid thermal pressure ($P$).
* `out.u`, `out.v`, `out.w` *(double)*: The fluid velocity in the X, Y, and Z directions respectively.
* `out.SetMassFraction(int id, double value)`
  * **Description**: Sets the mass fraction ($X_i$) of a specific species in this cell. The `id` must be the integer returned by `add_species` or `GetSpeciesID`. The sum of all mass fractions in a cell should ideally equal 1.0.
  * **Example**: `out.SetMassFraction(air_id, 1.0);`

### 🧰 ProblemHelper (EOS & Network Utilities)
Provides high-level wrapper functions to assist with setting up initial conditions, particularly when you need to calculate thermodynamic quantities. You can use these by calling them directly in your `Setup` or `Init` functions (requires `#include "../../src/core/ProblemHelper.h"`).

* `ProblemHelper::GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X)`
  * **Description**: Computes the thermal pressure $P$ given density ($\rho$), temperature ($T$), and an array of species mass fractions ($X$). This is incredibly useful when you want to define your initial conditions based on Temperature rather than Pressure, as it automatically queries the underlying EOS (Ideal, Helmholtz, or Tabular) for you.
  * **Example**: 
    ```cpp
    // Assuming you have previously populated out.mass_fractions (e.g. from default_X)
    out.p = ProblemHelper::GetPressureFromRhoT(config, specs, out.rho, 1e9, out.mass_fractions.data());
    ```
* `ProblemHelper::SetupNetworkAndFractions(SimConfig &config, SpeciesManager &specs, std::vector<double> &default_X)`
  * **Description**: Automatically registers all isotopes and computes the default ambient mass fractions based on the chosen nuclear network in your `.par` file (e.g., `aprox19`). Call this in `Setup` to populate your `default_X` array safely.

---

## 💻 Template Example

Here is a minimal, complete example of a new simulation case:

```cpp
/**
 * @file MyCase.cpp
 * @brief Template for creating a new simulation problem.
 */

#include "../../src/core/UserInterface.h"
#include "../../src/data/GlobalDefs.h"
#include <cmath>
#include <iostream>

class MyCustomProblem 
{
    // ----------------------------------------------------
    // Member variables to hold parameters read from .par
    // ----------------------------------------------------
    double m_density;
    double m_pressure;
    int m_spec_id;

public:
    // ========================================================================
    // 1. Setup Phase
    // Read config and define Species.
    // ========================================================================
    void Setup(SimConfig &config, SpeciesManager &specs)
    {
        // Read custom parameters from the parameter file
        m_density  = config.Get<double>("my_custom_density", 1.0);
        m_pressure = config.Get<double>("my_custom_pressure", 1.0);

        // Register a species: Name="Air", A=1, Z=1, Gamma=1.4, Cv=717.5
        // NOTE: You ONLY need to manually add species here if you are NOT using a nuclear network.
        // If a network (e.g. aprox19) is enabled in the .par file, its isotopes are auto-loaded!
        m_spec_id = specs.add_species("Air", 1.0, 1.0, 1.4, 717.5);

        std::cout << "[MyCase] Setup Complete. Target Density: " << m_density << "\n";
    }

    // ========================================================================
    // 2. Initialization Phase
    // Set initial primitive variables (rho, u, p, species) at coordinate (x,y,z).
    // ========================================================================
    void Init(const PointCoords &p, PrimitiveData &out) const
    {
        // Add a simple density perturbation using math functions
        double perturbation = 0.1 * std::sin(2.0 * M_PI * p.x);

        out.rho = m_density + perturbation;
        out.p   = m_pressure;
        out.u   = 0.0;
        out.v   = 0.0;
        out.w   = 0.0;

        // Set species composition (100% of the species we registered)
        out.SetMassFraction(m_spec_id, 1.0);
    }
};

// ============================================================================
// Registration
// Name your problem here (e.g., "MyCase"). 
// This name is used in the command line: ./bin/ARCH MyCase ...
// ============================================================================
REGISTER_PROBLEM_CLASS("MyCase", MyCustomProblem);
```

---

## 💡 Tips for Complex Cases

1. **No EOS Includes Required**: Notice that you don't need to manually invoke the EOS inside `Init`. You only define the raw physical properties (`rho` and `p`), and the underlying engine automatically calculates internal energy and populates the conservative vectors (`eng`, `mom_u`, etc.) based on the active `eos_type`.
2. **Standard C++ Math**: You can use `<cmath>` functions like `std::sin`, `std::exp`, `std::sqrt` directly in the `Init` function to create shapes and perturbations.

---

## 📄 Parameter File (`.par`) Template

Here is a comprehensive template for configuring your `.par` parameter file. Parameters are grouped by function.

```ini
# ==============================================================================
# ARCH Simulation Configuration File Template
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. Mandatory Parameters (Must exist in every run)
# ------------------------------------------------------------------------------
# Grid setup
# Dimensionality is degenerated by setting n_i = 1 (dictated by discrete mathematics and memory structure).
geometry = cartesian # Geometry of the computational domain (Options: cartesian, spherical, cylindrical)

n1       = 100       # Number of cells in the 1st dimension
n2       = 1         # Number of cells in the 2nd dimension (Set to 1 to degenerate this dimension)
n3       = 1         # Number of cells in the 3rd dimension (Set to 1 to degenerate this dimension)

# Domain physical boundaries
# IMPORTANT: x, y, z here simply mean the 1st, 2nd, and 3rd axes of your chosen geometry!
# For example, if geometry = spherical, x is r, y is theta, z is phi.
# You can use mathematical expressions like "2.0 * pi" directly instead of 6.28318!
x1_min    = 0.0       
x1_max    = 1.0
x2_min    = 0.0
x2_max    = 2.0 * pi

# Boundary conditions (Options: outflow, reflect, periodic)
x1l_boundary_type = outflow
x1r_boundary_type = outflow
x2l_boundary_type = outflow
x2r_boundary_type = outflow
x3l_boundary_type = outflow
x3r_boundary_type = outflow

# Execution & Hardware
compute_backend = cpu        # Options: cpu, cuda
cuda_device     = 0          # CUDA device ID (if using cuda backend)

# Numerics
solver          = HLLC       # Riemann Solver (Options: SW, VL, HLLC, Roe)
reconstruct     = ppm        # Reconstruction (Options: pcm, plm, ppm)
limiter         = minmod     # Limiter for reconstruction (Options: minmod, superbee, mc)
timeintegrator  = RK3        # Time Integrator (Options: RK2, RK3)
cfl             = 0.4        # Courant-Friedrichs-Lewy stability condition

# Physics Core
eos_type       = ideal       # Options: ideal, tabular, helmholtz
eos_table_path = ""          # Path to HDF5 table (Required for tabular/helmholtz)
gamma          = 1.4         # Default adiabatic index (used if eos=ideal)

# ------------------------------------------------------------------------------
# 2. Optional Parameters (Can be omitted; defaults apply)
# ------------------------------------------------------------------------------
# Time & I/O
tmax         = 1.0           # Physical end time (Default: 0.1)
max_steps    = -1            # Stop after N steps (-1 to disable)
out_dir      = output        # Folder for output data
base_name    = MyCase        # Prefix for generated HDF5 files
restart      = false         # Set to true/yes to resume from a checkpoint
restart_file = ""            # Path to the .h5 checkpoint file

# IO Frequency
plt_dt       = 0.1           # Output plot files every 0.1 physical seconds
plt_dstep    = -1            # Output plot files every N steps (-1 to disable)
chk_dt       = 0.5           # Checkpoint files for restarts
chk_dstep    = -1            # Checkpoint frequency by steps (-1 to disable)
plt_variables = all          # Output vars: all, conserved, or comma-separated list (rho,u,p,eng,species)

# Gravity
gravity_type = external      # Options: none, external, self
gravity_g_x  = 0.0
gravity_g_y  = -9.81         # Constant external gravity in Y direction
gravity_g_z  = 0.0
# gravity_G  = 6.6743e-8     # For self gravity

# Nuclear Burning
use_burn         = 0             # 1 = Enable burning, 0 = Disable
network_name     = aprox19       # Options: aprox13, aprox19, aprox21, iso7.
use_nse          = 1             # 1 = Enable online Timmes NSE solver
ode_solver       = BE_NR         # Underlying ODE solver (BE_NR, ROS4, BD)
linear_solver    = DenseLU       # Linear Algebra solver (DenseLU, SparseKLU)

# Diffusion
use_diffusion    = 0             # 1 = Enable diffusion, 0 = Disable
diff_integrator  = RKL2          # Diffusion Time Integrator (Options: RKL1, RKL2)
diff_cfl         = 0.8           # CFL condition for explicit diffusion integrator
diff_max_stages  = 256           # Maximum number of stages (s) allowed for RKL integrators
use_thermal_diff = 0             # 1 = Enable thermal diffusion
use_viscous_diff = 0             # 1 = Enable viscous diffusion
use_species_diff = 0             # 1 = Enable species diffusion
# nu_visc        = 0.0           # Constant kinematic viscosity (Do not set if using HelmEOS)
# alpha_therm    = 0.0           # Constant thermal diffusivity (Do not set if using HelmEOS)
# D_spec         = 0.0           # Constant species diffusivity (Do not set if using HelmEOS)

# ------------------------------------------------------------------------------
# 3. Advanced Tuning & Internal Controls (Hidden by Default)
# ------------------------------------------------------------------------------
# Only override these mathematical and internal solver parameters if necessary!

# --- Numerics & Grid Controls ---
# EntropyFix      = On       # Apply entropy fix (On/Off)
# EntropyFixCoefficient = 0.1
# sml_rho         = 1e-12    # Floor density
# max_eint        = 1e21     # Maximum specific internal energy allowed

# --- Advanced ODE & Burning Parameters ---
# nseTempThreshold = 4.5e9       # Temperature threshold for NSE (K)
# nseDensThreshold = 1.0e6       # Density threshold for NSE (g/cm^3)
# enforce_mass_conservation = 1  # Re-normalize mass fractions after burning step
# burn_verbose_level = 0         # Print debug information during burning
# 
# nuclearTempMin   = 1e9         # Minimum temperature to ignite burning (K)
# nuclearDensMin   = 1e-10       # Minimum density to ignite burning (g/cm^3)
# smallt           = 1e5         # Temperature floor for burning module (K)
# smallx           = 1e-20       # Mass fraction floor for burning module
# dt_init          = 1e-16       # Forced initial physical timestep for extreme stiff problems
# dt_min           = 1e-20       # Minimum allowed physical timestep
# tstep_change_factor = 1.2      # Maximum growth factor for macro-fluid timestep
# enucDtFactor     = 1e30        # Limits step size based on nuclear energy release rate (set to 0.1 to enable)
# 
# ode_rtol         = 1e-4        # Relative tolerance for Newton-Raphson
# ode_atol         = 1e-8        # Absolute tolerance
# ode_max_newton_iter = 50       # Max NR iterations per sub-step before reducing dt
# ode_max_substeps = 10000       # Max ODE sub-steps per macro-step
# ode_dt_fac_max   = 2.0         # Max step growth factor for the internal PI controller
# ode_dt_fac_min   = 0.1         # Max step shrink factor for the internal PI controller
# ode_dt_safe_fac  = 0.9         # Safety factor for step size selection
# ode_initial_dt_frac = 1e-3     # Initial sub-step fraction of macro-step
# ode_use_numerical_jac = 0      # 1 = Force numerical Jacobian even if analytical exists
# ode_freeze_jacobian = 0        # 1 = Freeze Jacobian for multiple Newton iterations

# ------------------------------------------------------------------------------
# 4. User Custom Parameters (Read by config.Get<T> in your Setup function)
# ------------------------------------------------------------------------------
# You can define anything here without touching the core framework!
my_custom_density  = 2.0
my_custom_pressure = 5.0
perturbation_mode  = 3
bubble_radius      = 0.15
```

---

## 🏃 Step-by-Step Example (Sod Shock Tube)

This example demonstrates how to run a built-in simulation case using the Workspace workflow.
ARCH adopts a "Workspace" workflow. **Do not run simulations inside the source directories.**

1. **Create a run directory:**
```bash
mkdir -p runs/test_sod
```

2. **Copy the template configuration:**
```bash
cp simulation/Sod/default.par runs/test_sod/arch.par
```

3. **Run the simulation:**
```bash
# Set OpenMP threads and run from the root directory or the run directory
export OMP_NUM_THREADS=4
./bin/ARCH Sod runs/test_sod/arch.par
```