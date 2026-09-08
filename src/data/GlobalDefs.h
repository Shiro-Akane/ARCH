/**
 * @file GlobalDefs.h
 * @brief Global configuration parameters parsed from input files.
 */

/**
 * Configuration is divided into typed core sections plus custom parameter maps
 * for problem-specific values that do not belong to the solver-wide contract.
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "../core/ArchPortability.h"
#include "../physics/constant/PhysicalConstants.h"

// Grid and domain configuration.
struct GridConfig
{
    // Root-block topology and active dimensionality.
    int nblockx1 = 1; ///< Number of root blocks in X
    int nblockx2 = 1; ///< Number of root blocks in Y
    int nblockx3 = 1; ///< Number of root blocks in Z
    int dim = 3;      ///< Dimensionality (1, 2, or 3)
    int amr_max_blocks = 2000; ///< Maximum number of AMR blocks

    // Physical domain bounds in code units.
    double x1_min = 0.0;
    double x1_max = 1.0;
    double x2_min = 0.0;
    double x2_max = 1.0;
    double x3_min = 0.0;
    double x3_max = 1.0;
    // Coordinate system used by metric terms: cartesian, spherical, or cylindrical.
    std::string geometry = "cartesian";

    std::string x1l_boundary_type = "outflow";
    std::string x1r_boundary_type = "outflow";
    std::string x2l_boundary_type = "outflow";
    std::string x2r_boundary_type = "outflow";
    std::string x3l_boundary_type = "outflow";
    std::string x3r_boundary_type = "outflow";
};

// Hydrodynamic discretization and stability controls.
struct NumericsConfig
{
    std::string solver_name;    ///< Numerical flux: SW, VL, HLL, HLLC, or Roe.

    // Runtime dispatch maps these names to compile-time reconstruction policies.
    std::string reconstruction = "pcm";  ///< "pcm" (1st), "plm" (2nd), "ppm" (3rd)
    std::string limiter = "minmod";      ///< "minmod", "mc", "superbee"
    std::string time_integrator = "RK2"; ///< "RK2","RK3"

    double cfl = 0.8; ///< Courant factor (CFL) for time-step stability control (0 < CFL < 1).

    double entropy_fix_coeff = 0.1; ///< Roe entropy-fix width relative to the local sound speed.

    double sml_rho = 1e-12; ///< Positive density floor in code units.
    double min_eint = 1e-10; ///< Positive specific internal-energy floor in code units.
    double max_eint = 1e21; ///< Specific internal-energy ceiling in code units.
};

// Execution backend selection.
struct ExecutionConfig
{
    // "cpu" always selects the host implementation.  "cuda" is strict and
    // must fail when the binary/device/selected physics combination cannot
    // provide a CUDA launcher.  "auto" may choose either, but must log it.
    std::string compute_backend = "cpu";
    int cuda_device = 0;
};

// Stiff ODE integration controls.
struct OdeConfig
{
    std::string ode_solver = "BE_NR";      ///< Default ODE solver: Backward Euler with Newton-Raphson
    std::string linear_solver = "Auto";    ///< DenseLU through 31 total ODE equations; larger CPU/CUDA systems use KLU/cuDSS.

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
    static constexpr int MAX_ODE_NEQ = 31; ///< Compact matrix limit, including thermal and auxiliary equations
    static constexpr int MAX_SPECIES = MAX_ODE_NEQ - 1; ///< Maximum with temperature and no auxiliary state

    static constexpr bool uses_compact_matrix(std::size_t equations) noexcept
    {
        return equations > 0 && equations <= MAX_ODE_NEQ;
    }

    static constexpr std::size_t equation_count(
        std::size_t species, std::size_t auxiliary = 0) noexcept
    {
        const auto maximum = std::numeric_limits<std::size_t>::max();
        return auxiliary >= maximum || species >= maximum - auxiliary
            ? maximum : species + 1 + auxiliary;
    }
};

/**
 * Plain numeric ODE controls passed through compile-time host/device policies.
 * Runtime-only names stay in OdeConfig and never enter a device-visible ABI.
 */
struct OdeConfigView
{
    double rtol;
    double atol;
    int max_newton_iter;
    int max_substeps;
    double dt_safe_factor;
    double dt_fac_max;
    double dt_fac_min;
    double initial_dt_frac;
    bool use_numerical_jacobian;
    bool freeze_jacobian;
};

/**
 * Plain numeric burn controls shared by the CPU authority and CUDA policy calls.
 */
struct BurnConfigView
{
    bool use_burn;
    double nuclearTempMin;
    double nuclearDensMin;
    double smallt;
    double smallx;
    double enucDtFactor;
    bool use_nse;
    double nseTempThreshold;
    double nseDensThreshold;
    bool enforce_mass_conservation;
    OdeConfigView odeconfig;
};

enum class BurnOdeStatus : std::uint8_t
{
    ActivationSkipped,
    NseSuccess,
    OdeSuccess,
    MaxSubsteps,
    Stalled,
    EosFailure // Backend-retained shared EOS error; never an adaptive retry.
};

struct BurnOdeReport
{
    BurnOdeStatus status = BurnOdeStatus::ActivationSkipped;
    int attempted_substeps = 0;
    int rejected_substeps = 0;
    int nse_attempts = 0;
    int nse_failures = 0;
    double dt_recommended = 0.0;
    // Signed specific energy from accepted ODE increments / NSE projection.
    // This is an integration result, not another ODE unknown.
    double energy_change = 0.0;

    ARCH_HOST_DEVICE constexpr bool success() const
    {
        return status == BurnOdeStatus::ActivationSkipped
            || status == BurnOdeStatus::NseSuccess
            || status == BurnOdeStatus::OdeSuccess;
    }
};

struct BurnConfig
{
    double ignition_temp = 1e9; ///< Ignition temperature threshold for burning (in Kelvin)
    double burn_tol = 1e-6;     ///< Tolerance for burn convergence

    bool use_burn = false;                ///< Master switch for the burn module
    std::string network_name = "aprox19"; ///< Built-in network: aprox13, aprox19, aprox21, or iso7.

    double nuclearTempMin = 1e9; ///< Minimum temperature for burning (in Kelvin)
    double nuclearDensMin = 1e-10;  ///< Minimum density for burning (in g/cm^3)

    double smallt = 1e5; ///< Minimum temperature for burning (in Kelvin)
    double smallx = 1e-20;  ///< Minimum mass fraction for species (to avoid negative or zero)

    double enucDtFactor = 1e30; ///< Maximum fractional change in internal energy per burn step (1e30 = practically off)
    bool use_nse = true; ///< Use the online Timmes Saha NSE solver at high T/rho
    double nseTempThreshold = 4.5e9; ///< Temperature threshold for NSE projection
    double nseDensThreshold = 1.0e6; ///< Density threshold for NSE projection

    bool enforce_mass_conservation = true; ///< Whether to enforce mass fraction conservation after each burn step

    int verbose_level = 0; ///< Verbosity level for burn diagnostics (0: silent, 1: basic, 2: detailed)

    OdeConfig odeconfig; ///< ODE solver configuration for the burn module
};

inline BurnConfigView make_burn_config_view(const BurnConfig& config)
{
    return {
        config.use_burn,
        config.nuclearTempMin,
        config.nuclearDensMin,
        config.smallt,
        config.smallx,
        config.enucDtFactor,
        config.use_nse,
        config.nseTempThreshold,
        config.nseDensThreshold,
        config.enforce_mass_conservation,
        {
            config.odeconfig.rtol,
            config.odeconfig.atol,
            config.odeconfig.max_newton_iter,
            config.odeconfig.max_substeps,
            config.odeconfig.dt_safe_factor,
            config.odeconfig.dt_fac_max,
            config.odeconfig.dt_fac_min,
            config.odeconfig.initial_dt_frac,
            config.odeconfig.use_numerical_jacobian,
            config.odeconfig.freeze_jacobian,
        },
    };
}

// Gravity configuration.
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
    double G_const = arch::constants::gravity::cgs::gravitational_constant;
};

// Diffusion configuration.
struct DiffusionConfig
{
    bool use_diffusion = false;          ///< Master switch for the diffusion module
    std::string integrator = "RKL2";     ///< Time integrator: "RKL1", "RKL2"
    double diff_cfl = 0.8;                    ///< CFL condition for explicit diffusion integrator
    int max_stages = 256;                ///< Maximum number of stages (s) allowed for RKL integrators

    // Independently enabled diffusion operators.
    bool use_thermal_diffusion = false;
    bool use_viscous_diffusion = false;
    bool use_species_diffusion = false;

    // Constant IdealGas coefficients or explicit transport overrides. A zero
    // value delegates to an EOS transport interface when one is available.
    double nu_visc = 0.0;     ///< Constant kinematic viscosity (nu)
    double alpha_therm = 0.0; ///< Constant thermal diffusivity (alpha = k / (rho * cp))
    double D_spec = 0.0;      ///< Constant species diffusivity
};

struct PhysicsConfig
{
    std::string eos_type = "ideal";  ///< Equation of state: ideal, tabular, or helmholtz.
    std::string eos_table_path = ""; ///< For tabular EOS, the path to the HDF5 file
    double gamma = 1.4;              ///< Default adiabatic index

    GravityConfig gravity;
    BurnConfig burn;
    DiffusionConfig diffusion;
};

// Adaptive mesh refinement controls.
struct AmrConfig
{
    int lrefinemin = 0;            ///< Minimum refinement level
    int lrefinemax = 0;            ///< Maximum refinement level (0 = AMR disabled)
    int regrid_interval = 2;       ///< Number of steps between regridding
    std::string refine_var = "DENS"; ///< Canonical comma-separated AMR indicator list
    bool refine_on_rho = true;       ///< DENS: density gradient
    bool refine_on_p = false;        ///< PRES: pressure gradient
    bool refine_on_temp = false;     ///< TEMP: EOS temperature gradient
    bool refine_on_velx = false;     ///< VELX: x-velocity gradient
    bool refine_on_vely = false;     ///< VELY: y-velocity gradient
    bool refine_on_velz = false;     ///< VELZ: z-velocity gradient
    bool refine_on_eng = false;      ///< ENER: total-energy density gradient
    bool refine_on_vorticity = false; ///< VORT: magnitude of curl(v)
    bool refine_on_div_v = false;     ///< DIVV: velocity divergence
    bool refine_on_entropy = false;   ///< ENTR: EOS-local Gamma1 entropy proxy
    bool refine_on_enuc = false;      ///< ENUC: nuclear specific-energy source rate
    bool refine_on_jeans = false;     ///< JENS: reserved for a self-gravity Jeans criterion
    bool refine_on_species = false;  ///< SPECIES or named network-tracer gradient
    bool refine_all_species = false; ///< SPECIES selects every registered species
    std::vector<std::string> refine_species_names; ///< Case-insensitive species tracer names
    double refine_threshold = 0.8;   ///< Dimensionless Lohner error threshold for refinement
    double derefine_threshold = 0.2; ///< Dimensionless Lohner error threshold for derefinement
};

// Plot-variable selection.
struct OutputVariables
{
    bool rho = true;     ///< DENS
    bool temp = false;   ///< TEMP
    bool u = true;       ///< VELX
    bool v = false;      ///< VELY
    bool w = false;      ///< VELZ
    bool p = true;       ///< PRES
    bool eng = true;     ///< ENER
    bool species = true; ///< SPECIES or all named tracers
    bool vort = false;    ///< VORT: metric-aware magnitude of curl(v)
    bool divv = false;    ///< DIVV: metric-aware velocity divergence
    bool entr = false;    ///< ENTR: EOS-local Gamma1 entropy proxy
    bool enuc = false;    ///< ENUC: signed nuclear specific-energy source rate
    bool jens = false;    ///< JENS: reserved until self gravity is available
};

struct IOConfig
{
    double tmax = 0.0;  ///< Simulation end time
    int max_steps = -1; ///< Maximum number of steps (-1 for no limit)

    // Plot-file cadence.
    double plt_dt = -1.0; ///< Output interval (-1.0 for no output)
    int plt_dstep = -1;   ///< Output every N steps (-1 for no output)

    // Checkpoint cadence.
    double chk_dt = -1.0; ///< Checkpoint interval (-1.0 for no checkpoints)
    int chk_dstep = -1;   ///< Checkpoint every N steps (-1 for no checkpoints)

    std::string out_dir = "output";
    std::string base_name = "arch";

    bool restart = false; ///< Is this a restart run?
    std::string restart_file = "";

    OutputVariables vars;
    std::vector<std::string> plot_species_names; ///< Individual case-insensitive species fields selected for PLT
};

struct RunState
{
    double time = 0.0; ///< Current physical time
    int step = 0;      ///< Current iteration step count
    int plt_idx = 0;   ///< Current plot file index
    int chk_idx = 0;   ///< Current checkpoint file index
    double dt_old = 0.0; ///< Unsynchronized macro-step proposal used by the growth limiter
    double dt_burn = 0.0; ///< Burn-reported limit carried into the next macro step
    bool has_timestep_state = false; ///< Restored controller limits are available; false for a fresh simulation.
    bool resume_after_regrid = false; ///< The saved loop checkpoint already completed regrid/I/O
    bool checkpoint_provenance_verified = false; ///< True after validating the restored scientific identity.
    std::string verified_eos_table_sha256; ///< Saved table identity rechecked after the EOS owner loads
};

// Complete runtime configuration.
struct SimConfig
{
    GridConfig grid;
    NumericsConfig numerics;
    ExecutionConfig execution;
    PhysicsConfig physics;
    AmrConfig amr;
    IOConfig io;

    /**
     * @brief Stores problem-specific parameters not represented by a core field.
     * Typical keys include:
     * - "prob_rho_L" (Shock tube specific)
     * - "burn_ignition_temp" (Burn module specific)
     * - "stiff_p_inf" (Stiffened Gas EOS parameter)
     */
    std::map<std::string, double> custom_params;

    std::map<std::string, std::string> custom_string_params;

    // Return a typed custom parameter or the caller-provided default.
    template <typename T>
    T Get(const std::string &key, T default_val) const
    {
        // Return the preserved string value when requested explicitly.
        if constexpr (std::is_same_v<T, std::string>)
        {
            auto it = custom_string_params.find(key);
            if (it != custom_string_params.end())
                return it->second;
            return default_val;
        }
        // Numeric requests use the typed custom-parameter map.
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
