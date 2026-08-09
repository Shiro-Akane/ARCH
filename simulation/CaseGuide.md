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

### Stable case include surface

A maintained case requires exactly these two ARCH headers:

```cpp
#include "../../src/core/UserInterface.h" // Registration macros and ProblemHelper facade.
#include "../../src/data/GlobalDefs.h"    // Explicit SimConfig and PrimitiveData contract.
```

`UserInterface.h` automatically exports the complete public `ProblemHelper` API.
Do **not** include `ProblemHelper.cpp`, an EOS implementation, a network implementation,
or internal AMR/IO headers in a case. `CMakeLists.txt` compiles and links the helper and
all selected EOS/network policies into ARCH automatically. Standard-library headers such
as `<cmath>`, `<string>`, or `<vector>` may of course be included when the case uses them.

### ⚙️ SimConfig (Reading Parameters)
Passed into `Setup`. Used to read custom variables defined in your `.par` file.

* `config.Get<T>(const std::string &key, T default_val)`
  * **Description**: Reads a case-specific value from the `.par` file. Supported `T` values are numeric types and `std::string`; an absent key returns `default_val`.
  * **Example**: `double radius = config.Get<double>("bubble_radius", 1.5);`
  * **Example**: `int mode = config.Get<int>("perturbation_mode", 2);`
  * **Example**: `std::string profile = config.Get<std::string>("profile", "gaussian");`
* `config.GetCustomParam(const std::string &key, double default_val)`
  * **Description**: Numeric shorthand equivalent to `Get<double>`.

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
  * **Example**: `int c12_id = specs.GetSpeciesID("c12");` (Case-insensitive; returns `-1` if absent.)
* `int specs.count() const` and `std::string specs.get_name(int id) const`
  * **Description**: Query the registered species count or the output name for a validated ID. The property accessors `get_A`, `get_Z`, `get_gamma_ref`, and `get_Cv_ref` are also available when a case genuinely needs registered material metadata.

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
* `out.u`, `out.v`, `out.w` *(double)*: The physical velocity components along the active grid directions.
* `out.mass_fractions` *(std::vector<double>)*: The complete composition vector prepared by the framework before every `Init` call.
* `out.SetMassFraction(int id, double value)`
  * **Description**: Sets the mass fraction ($X_i$) of a specific species. The `id` must be returned by `add_species` or `GetSpeciesID`; all species fractions should sum to one.
  * **Example**: `out.SetMassFraction(air_id, 1.0);`
* `out.eng` and conservative momenta are intentionally **not** case inputs. The framework derives them from `rho`, `u/v/w`, `p`, composition, and the active EOS.

### ?? ProblemHelper (EOS & network utilities)

`ProblemHelper` is available automatically through `UserInterface.h`; never include a
helper `.cpp` or EOS/network-specific header in a case. Its complete case-facing API is:

* `ProblemHelper::SetupNetworkAndFractions(SimConfig &config, SpeciesManager &specs, std::vector<double> &default_X)`
  * **Description**: Registers the selected built-in network (`aprox13`, `aprox19`, `aprox21`, or `iso7`) and fills its default composition. Call it once in `Setup` before looking up isotope IDs.
* `ProblemHelper::GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X)`
  * **Description**: Computes pressure from $(\rho,T,X)$ through the configured Ideal, Helmholtz, or tabular EOS.
  * **Use it in `Setup` to precompute a reference pressure**, not in per-cell `Init`: the helper intentionally performs runtime EOS dispatch and is not an inner-loop kernel.
  * **Example**:
    ```cpp
    std::vector<double> default_X;
    ProblemHelper::SetupNetworkAndFractions(config, specs, default_X);
    const double p0 = ProblemHelper::GetPressureFromRhoT(
        config, specs, 1.0e6, 1.0e9, default_X.data());
    ```

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

1. **No implementation includes**: `UserInterface.h` automatically exposes `ProblemHelper`; a case never includes `.cpp` files, EOS implementations, reaction-network implementations, AMR internals, or IO internals.
2. **EOS conversion ownership**: `Init` supplies primitive values only. ARCH computes conservative energy/momenta with the selected `eos_type`. Use `GetPressureFromRhoT` only during `Setup` when a temperature-defined reference state is needed.
3. **Standard C++ Math**: You can use `<cmath>` functions like `std::sin`, `std::exp`, and `std::sqrt` directly in `Init` to create shapes and perturbations.
4. **Registration choices**: `REGISTER_PROBLEM_CLASS("Name", ClassType)` is the maintained class interface. `REGISTER_PROBLEM("Name", SetupFunc, InitFunc)` remains available for stateless free-function cases.

---

### Canonical AMR and PLT variable names

`refine_var` and `plt_variables` use one case-insensitive, canonical vocabulary.
AMR computes the dimensionless Lohner error of every selected scalar and takes the
maximum over variables, cells, and coordinate directions. Therefore both thresholds
are unitless and must satisfy `0 <= derefine_threshold < refine_threshold <= 1`.
The maintained starting values are `0.8` and `0.2`; they are not physical gradients.

| Name | Meaning | AMR | PLT | Availability |
| --- | --- | --- | --- | --- |
| DENS | mass density | yes | yes | all models |
| PRES | thermodynamic pressure from the active EOS | yes | yes | all EOS policies |
| TEMP | temperature from the active EOS | yes | yes | all EOS policies |
| VELX, VELY, VELZ | physical velocity components along active grid directions | yes | yes | active dimensions only |
| ENER | total energy density | yes | yes | all models |
| VORT | metric-aware magnitude of `curl(v)` | yes | yes | Cartesian, cylindrical, and spherical; centered metric stencils |
| DIVV | metric-aware `div(v)` | yes | yes | Cartesian, cylindrical, and spherical; finite-volume face measures |
| ENTR | EOS-local `Gamma1` invariant proxy `p/rho^Gamma1` | yes | yes | `Gamma1 = rho*c_s^2/p` from the active EOS; see note below |
| ENUC | signed nuclear specific-energy source rate | yes | yes | requires `use_burn = 1` |
| JENS | Jeans-resolution criterion | reserved | reserved | disabled until the self-gravity potential solver exists |
| SPECIES | every registered species | yes | yes | when species are registered |
| c12, he4, HeavyFluid, etc. | one registered tracer/species | yes | yes | AMR and PLT matching are case-insensitive |

PLT entries can be joined with either commas or `+`: for example,
`plt_variables = CONSERVED+ENUC+c12`. `CONSERVED` enables DENS, VELX, VELY,
VELZ, and ENER; `ALL` enables every currently available PLT field and every
registered species. A single registered species can instead be requested directly
(e.g. `c12` or `He4`) without writing the full `SPECIES` group. The parser
normalizes requested species names to lowercase, and `SpeciesManager` performs the
same case-insensitive lookup before output; the HDF5 dataset retains the network's
registered spelling. ENUC without burning, unsupported velocity components, JENS,
or an unregistered requested species emits a warning and is safely omitted.

`ENTR` is deliberately **not** labelled thermodynamic entropy for a general EOS.
ARCH evaluates the local adiabatic exponent as `Gamma1 = rho*c_s^2/p`, using the
same EOS sound speed as the Riemann solver, then writes/tests `p/rho^Gamma1`.
For a constant-Gamma ideal gas this is the usual entropy invariant. For Helmholtz
and tabular EOS it is a state-local refinement proxy, not a globally conserved
entropy; ARCH never substitutes `physics.gamma` or an EOS `get_gamma` placeholder.
If an AMR cell has non-positive pressure or no finite positive sound speed, ENTR
fails explicitly rather than silently choosing a constant exponent.

VORT and DIVV use one shared operator in `src/physics/diagnostics/VelocityDiagnostics.h`. DIVV is a
finite-volume face-flux difference using `GridMetrics::CellVolume` and
`GridMetrics::FaceArea`; VORT uses the matching physical cylindrical/polar/spherical
curl identities. This avoids a Cartesian-only diagnostic path and keeps AMR and
PLT numerically identical. The former aliases (`rho`, `p`, `u`, `v`, `w`, `eng`, and
the long forms of VORT/DIVV/ENTR/JENS) are not accepted.

---

## 📄 Parameter File (`.par`) Template

Here is a comprehensive template for configuring your `.par` parameter file. Parameters are grouped by function.

```ini
# ==============================================================================
# ARCH Simulation Configuration File Template
# ==============================================================================

# ------------------------------------------------------------------------------
# 1. Core configuration (recommended explicit settings)
# ------------------------------------------------------------------------------
# Grid and AMR-root setup
# x1/x2/x3 denote logical grid coordinates; their geometry meaning is documented below.
# A block owns a fixed interior tile. Set nblockx3 = 0 for a true 2D
# allocation and nblockx2 = nblockx3 = 0 for a true 1D allocation.
geometry = cartesian # Options: cartesian, spherical, cylindrical

nblockx1 = 4         # Root blocks along x1
nblockx2 = 4         # Root blocks along x2; 0 removes this dimension
nblockx3 = 0         # Root blocks along x3; 0 removes this dimension
max_blocks = 512     # Strict AMR pool cap; size this for lrefinemax without OOM.

# AMR Setup: dimensionless Lohner error estimator
lrefinemin = 0
lrefinemax = 2
regrid_interval = 2
refine_var = DENS, PRES
refine_threshold = 0.8       # refine when max Lohner error exceeds this value
derefine_threshold = 0.2     # derefine only below this value
# Domain physical boundaries
# IMPORTANT: x, y, z here simply mean the 1st, 2nd, and 3rd axes of your chosen geometry!
# For example, if geometry = spherical, x is r, y is theta, z is phi.
# You can use mathematical expressions like "2.0 * pi" directly instead of 6.28318!
x1_min    = 0.0
x1_max    = 1.0
x2_min    = 0.0
x2_max    = 1.0       # For angular coordinate x2, a common choice is 2.0 * pi
x3_min    = 0.0       # Ignored when nblockx3 = 0
x3_max    = 1.0       # Ignored when nblockx3 = 0

# Boundary conditions (Options: outflow, reflect, periodic)
x1l_boundary_type = outflow
x1r_boundary_type = outflow
x2l_boundary_type = outflow
x2r_boundary_type = outflow
x3l_boundary_type = outflow
x3r_boundary_type = outflow

# Numerics
solver          = HLLC       # Options: SW, VL, Roe, HLL, HLLC
reconstruct     = ppm        # Options: pcm, muscl/plm, ppm
limiter         = minmod     # Options: minmod, superbee, vanleer, mc, none
time_integrator = RK3        # Options: Euler/RK1, RK2, RK3
cfl             = 0.4        # Courant-Friedrichs-Lewy stability condition

# Physics core
eos_type       = ideal       # Options: ideal, tabular, helmholtz
eos_table_path = ""          # Required table path for tabular/helmholtz
gamma          = 1.4         # Ideal-gas reference gamma; EOS-derived thermodynamics override it

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
plt_variables = DENS, PRES, TEMP, VELX, VELY, ENER, SPECIES
                              # Safe 2D/non-burning default; ALL and CONSERVED remain available

# Canonical AMR/PLT variables, availability, and aliases are documented above.
# Gravity (optional)
gravity_type = none          # Options: none, external; self potential is not implemented
gravity_g_x  = 0.0           # Used only for gravity_type = external
gravity_g_y  = 0.0           # Set, for example, -9.81 only after selecting external gravity
gravity_g_z  = 0.0
# gravity_G  = 6.6743e-8     # Reserved until a self-gravity potential solver exists

# Nuclear burning (optional)
use_burn         = 0             # 1 = enable, 0 = disable
network_name     = aprox19       # Options: aprox13, aprox19, aprox21, iso7; Setup must register it
use_nse          = 1             # Applies only when burning is enabled and NSE thresholds are met
ode_solver       = BE_NR         # Options provided by the selected burn network
linear_solver    = DenseLU       # DenseLU or SparseKLU when available

# Diffusion (optional)
use_diffusion    = 0             # 1 = enable, 0 = disable
diff_integrator  = RKL2          # RKL2 is the production STS method; RKL1 is first order
diff_cfl         = 0.8           # Fraction of the forward-Euler diffusion stability limit
diff_max_stages  = 256           # Bounds the RKL polynomial and permitted macro step
use_thermal_diff = 0             # 1 = Enable thermal diffusion
use_viscous_diff = 0             # 1 = Enable viscous diffusion
use_species_diff = 0             # 1 = Enable species diffusion
# Constant non-Helm transport overrides are grouped in Advanced Tuning below.

# ------------------------------------------------------------------------------
# 3. Advanced Tuning & Internal Controls (Hidden by Default)
# ------------------------------------------------------------------------------
# Only override these mathematical and internal solver parameters if necessary!

# --- Execution and numerics ---
# compute_backend = cpu        # cpu, cuda, or auto; cuda is strict when unavailable
# cuda_device     = 0
# EntropyFix = On              # On/Off; Off sets the entropy-fix coefficient to zero
# EntropyFixCoefficient = 0.1
# sml_rho         = 1e-12    # Floor density
# max_eint        = 1e21     # Maximum specific internal energy allowed

# --- Burn activation and thermodynamic thresholds ---
# nseTempThreshold = 4.5e9       # Temperature threshold for NSE (K)
# nseDensThreshold = 1.0e6       # Density threshold for NSE (g/cm^3)
# enforce_mass_conservation = 1  # Re-normalize mass fractions after burning step
# burn_verbose_level = 0         # Print debug information during burning
#
# nuclearTempMin   = 1e9         # Minimum temperature to ignite burning (K)
# nuclearDensMin   = 1e-10       # Minimum density to ignite burning
# use_nse          = 1           # Enable or disable NSE projection
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

# --- Advanced diffusion overrides ---
# nu_visc        = 0.0           # Constant kinematic viscosity for non-Helm EOS
# alpha_therm    = 0.0           # Constant thermal diffusivity for non-Helm EOS
# D_spec         = 0.0           # Constant species diffusivity for non-Helm EOS
# Helmholtz diffusion uses diffusionCoe automatically; do not set these three overrides.

# ------------------------------------------------------------------------------
# 4. Case-specific parameters (Read by config.Get<T> in Setup)
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
cp simulation/Sod/Sod.par runs/test_sod/arch.par
```

3. **Run the simulation:**
```bash
# Set OpenMP threads and run from the root directory or the run directory
export OMP_NUM_THREADS=4
./bin/ARCH Sod runs/test_sod/arch.par
```

---

## AMR diffusion and maintenance interface

Diffusion uses a single physical-flux implementation in src/numerics/diffusion/DiffFlux.h.
It owns constant overrides and the Helmholtz/diffusionCoe automatic transport lookup.
RKL integrators and AMR stages consume those fluxes; they must not duplicate thermal,
viscous, species, or geometric source formulae.

For a hierarchy with multiple active leaves, diff_integrator = RKL2 advances one
composite polynomial across the complete leaf set. Each RKL stage applies physical
boundaries, same/coarse/fine ghost exchange, coarse-fine flux registration, reflux,
and a post-reflux halo fill. dt_diff in the step table is the forward-Euler limit;
the driver uses the RKL polynomial stability radius to choose the macro step.
RKL1 is available for deliberate first-order studies only; the production default is
RKL2 and no automatic order downgrade is performed.

The maintained public integration entry points are:

- Numerics::Diffusion::advance_amr_rkl1(...) in RKL1TimeIntegrator.h.
- Numerics::Diffusion::advance_amr_rkl2(...) in RKL2TimeIntegrator.h.
- DiffFunction::RKLOrder, compute_stages(...), usable_max_stages(...),
  stable_step(...), and get_rkl_coeffs(...) in DiffFunction.h.

The existing order-specific DiffFunction functions remain available for compatibility.
The shared conservative stage engine is internal to DiffusionAMRStages.h; new code
should include the matching RKL1/RKL2 header, not implement another AMR diffusion loop.

With eos_type = helmholtz, leave alpha_therm, nu_visc, and D_spec at zero to request
the automatic EOS/diffusionCoe transport path. Supplying positive thermal or viscous
overrides with a stellar EOS is intentionally rejected to avoid silently mixing
transport models.
