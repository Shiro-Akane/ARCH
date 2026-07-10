/**
 * @file GlobalDefs.h
 * @brief Global configuration parameters parsed from input files.
 */

#pragma once
#include <string>
#include <map>
#include <iostream>

/**
 * @file GlobalDefs.h
 * @brief Global configuration parameters.
 * Uses a hybrid approach: Explicit structs for core system params,
 * and a generic map for flexible module-specific params.
 */
// ----------------------------------------------------------------------
// 1. Grid Configuration (Future-proofed for Multi-dim)
// ----------------------------------------------------------------------
struct GridConfig
{
    // Basic Dimensions
    int nx;     ///< Cells in X
    int ny = 1; ///< Cells in Y (Default to 1 for 1D)
    int nz = 1; ///< Cells in Z (Default to 1 for 1D)

    // Physical Domain
    double x_min = 0.0;
    double x_max = 1.0;
    double y_min = 0.0;
    double y_max = 1.0;
    double z_min = 0.0;
    double z_max = 1.0;
    // Y/Z limits can be added later or kept here unused

    // Geometry: "cartesian", "spherical", "cylindrical"
    // This allows you to implement source terms later without changing the struct
    std::string geometry = "cartesian";

    std::string xl_boundary_type = "outflow";
    std::string xr_boundary_type = "outflow";
    std::string yl_boundary_type = "outflow";
    std::string yr_boundary_type = "outflow";
    std::string zl_boundary_type = "outflow";
    std::string zr_boundary_type = "outflow";
};

// ----------------------------------------------------------------------
// 2. Numerics Configuration (Solver, Limiter, Reconstruction)
// ----------------------------------------------------------------------
struct NumericsConfig
{
    // Solver Selection
    std::string solver_name;    ///< "SW", "VL", "HLLC", "Roe"
    std::string riemann_solver; ///< For future: separate Flux split vs Riemann?

    // Reconstruction & Limiting
    // These are strings now. The Factory will interpret "minmod" or "weno5"
    std::string reconstruction = "pcm";  ///< "pcm" (1st), "plm" (2nd), "ppm" (3rd)
    std::string limiter = "minmod";      ///< "minmod", "mc", "superbee"
    std::string time_integrator = "RK2"; ///< "RK2","RK3"

    double cfl = 0.8; ///< Courant factor (CFL) for time-step stability control (0 < CFL < 1).

    double entropy_fix_coeff = 0.1;
};

// ----------------------------------------------------------------------
// 3. Physics Configuration (EOS, Burn, Gravity)
// ----------------------------------------------------------------------

// --- 编译期静态内存常量 ---
struct OdeConfig
{
    std::string ode_solver = "BE_NR";      ///< Default ODE solver: Backward Euler with Newton-Raphson
    std::string linear_solver = "DenseLU"; ///< Default linear solver for the Jacobian system

    double rtol = 1e-4; ///< Relative tolerance for ODE integration
    double atol = 1e-8; ///< Absolute tolerance for ODE integration

    int max_newton_iter = 50; ///< Maximum Newton-Raphson iterations per ODE step
    int max_substeps = 100;   ///< Maximum adaptive sub-steps for stiff ODEs

    double dt_safe_factor = 0.9;    ///< Safety factor for adaptive time-stepping
    double dt_fac_max = 2.0;        ///< Maximum factor to increase dt
    double dt_fac_min = 0.1;        ///< Minimum factor to decrease dt
    double initial_dt_frac = 1e-14; ///< Initial fraction of the global time step for the first ODE sub-step

    bool use_numerical_jacobian = false; ///< Whether to compute Jacobian numerically (default: false, use analytical)
    bool freeze_jacobian = false;        ///< Whether to freeze the Jacobian for multiple Newton iterations (default: false)
};

struct BurnLimits
{
    static constexpr int MAX_SPECIES = 30;              ///< Maximum number of species supported by dense matrix network
    static constexpr int MAX_ODE_NEQ = MAX_SPECIES + 1; ///< Maximum ODE system size (species + temperature)
};

struct BurnConfig
{
    double ignition_temp = 1e9; ///< Ignition temperature threshold for burning (in Kelvin)
    double burn_tol = 1e-6;     ///< Tolerance for burn convergence

    // Nuclear Burning (Placeholder)
    bool use_burn = false;                ///< Master switch for the burn module
    std::string network_name = "aprox19"; ///< "alpha_chain", "c12_o16", "7isotope"

    double burn_temp_min = 1e6; ///< Minimum temperature for burning (in Kelvin)
    double burn_rho_min = 1e1;  ///< Minimum density for burning (in g/cm^3)

    double small_temp = 1e5; ///< Minimum temperature for burning (in Kelvin)
    double small_x = 1e-20;  ///< Minimum mass fraction for species (to avoid negative or zero)

    bool enforce_mass_conservation = true; ///< Whether to enforce mass fraction conservation after each burn step

    int verbose_level = 0; ///< Verbosity level for burn diagnostics (0: silent, 1: basic, 2: detailed)

    OdeConfig odeconfig; ///< ODE solver configuration for the burn module
};

// Gravity Configuration
struct GravityConfig
{
    std::string type = "none"; // "none", "external", "self"

    // External Gravity Components (Logical Dimensions)
    // - Cartesian:   g_x = X-gravity, g_y = Y-gravity
    // - Cylindrical: g_x = Radial (r), g_y = Axial (z)
    // - Spherical:   g_x = Radial (r), g_y = Polar (theta)
    double g_x = 0.0;
    double g_y = 0.0;
    double g_z = 0.0;
    double G_const = 6.6743e-8; // for self-gravity, in cgs units (cm^3 g^-1 s^-2)
};

struct PhysicsConfig
{
    // Equation of State
    // Future-proof: Factory switches based on this string.
    std::string eos_type = "ideal";  ///< "ideal", “tabular”, "stiffened_gas", etc.
    std::string eos_table_path = ""; ///< For tabular EOS, the path to the HDF5 file
    double gamma = 1.4;              ///< Default adiabatic index

    // Gravity (Placeholder)
    GravityConfig gravity;
    BurnConfig burn;
};

// ----------------------------------------------------------------------
// 4. I/O Configuration
// ----------------------------------------------------------------------
struct OutputVariables
{
    bool rho = true; // 默认开启
    bool u = true;   // x-velocity
    bool v = false;  // y-velocity (default false for 1D)
    bool w = false;  // z-velocity (default false for 1D)
    bool p = true;
    bool eng = true;
    bool species = true; // 是否输出所有组分
    // bool temp = false; // 未来可以加温度等
};

struct IOConfig
{
    double tmax = 0.0;  ///< Simulation end time
    int max_steps = -1; ///< Maximum number of steps (-1 for no limit)

    // --- Plot Files Controls ---
    double plt_dt = -1.0; ///< Output interval (-1.0 for no output)
    int plt_dstep = -1;   ///< Output every N steps (-1 for no output)

    // --- Output File Controls ---
    double chk_dt = -1.0; ///< Checkpoint interval (-1.0 for no checkpoints)
    int chk_dstep = -1;   ///< Checkpoint every N steps (-1 for no checkpoints)

    std::string out_dir = "output";
    std::string base_name = "arch";

    bool restart = false; ///< Is this a restart run?
    std::string restart_file = "";

    OutputVariables vars;
};

struct RunState
{
    double time = 0.0; ///< Current physical time
    int step = 0;      ///< Current iteration step count
    int plt_idx = 0;   ///< Current plot file index
    int chk_idx = 0;   ///< Current checkpoint file index
};

// ----------------------------------------------------------------------
// MAIN CONFIGURATION STRUCTURE
// ----------------------------------------------------------------------
struct SimConfig
{
    GridConfig grid;
    NumericsConfig numerics;
    PhysicsConfig physics;
    IOConfig io;

    // =========================================================
    // THE "CATCH-ALL" BUCKET
    // =========================================================
    /**
     * @brief Stores any parameter found in the .par file that doesn't
     * match a core struct member.
     * * Examples of what goes here:
     * - "prob_rho_L" (Shock tube specific)
     * - "burn_ignition_temp" (Burn module specific)
     * - "stiff_p_inf" (Stiffened Gas EOS parameter)
     */
    std::map<std::string, double> custom_params;

    std::map<std::string, std::string> custom_string_params;

    // Helper to get params safely
    template <typename T>
    T Get(const std::string &key, T default_val) const
    {
        // 如果请求的是 std::string
        if constexpr (std::is_same_v<T, std::string>)
        {
            auto it = custom_string_params.find(key);
            if (it != custom_string_params.end())
                return it->second;
            return default_val;
        }
        // 如果请求的是数字 (double, int, float)
        else
        {
            auto it = custom_params.find(key);
            if (it != custom_params.end())
                return static_cast<T>(it->second);
            return default_val;
        }
    }
    double GetCustomParam(const std::string &key, double default_val) const
    {
        return Get<double>(key, default_val);
    }
};
