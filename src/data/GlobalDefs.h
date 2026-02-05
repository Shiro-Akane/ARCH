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
    // Y/Z limits can be added later or kept here unused

    // Geometry: "cartesian", "spherical", "cylindrical"
    // This allows you to implement source terms later without changing the struct
    std::string geometry = "cartesian";

    std::string xl_boundary_type = "outflow";
    std::string xr_boundary_type = "outflow";
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
};

// ----------------------------------------------------------------------
// 3. Physics Configuration (EOS, Burn, Gravity)
// ----------------------------------------------------------------------
struct PhysicsConfig
{
    // Equation of State
    // Future-proof: Factory switches based on this string.
    std::string eos_type = "ideal"; ///< "ideal", "stiffened", "helmholtz"
    double gamma = 1.4;             ///< Default adiabatic index

    // Nuclear Burning (Placeholder)
    bool use_burn = false;         ///< Master switch for the burn module
    std::string network_name = ""; ///< "alpha_chain", "c12_o16", "7isotope"

    // Gravity (Placeholder)
    bool use_gravity = false;
};

// ----------------------------------------------------------------------
// 4. I/O Configuration
// ----------------------------------------------------------------------
struct OutputVariables
{
    bool rho = true; // 默认开启
    bool u = true;
    bool p = true;
    bool eng = true;
    bool species = true; // 是否输出所有组分
    // bool temp = false; // 未来可以加温度等
};

struct IOConfig
{
    double tmax;         ///< Simulation end time
    double plt_interval; ///< Output interval
    std::string out_dir = "output";
    std::string base_name = "plt";

    bool restart = false; ///< Is this a restart run?
    std::string restart_file = "";

    OutputVariables vars;
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
