/**
 * @file RuntimeParams.h
 * @brief Global static interface for accessing runtime parameters via type deduction.
 * Wraps the ConfigParser in a Singleton to provide seamless access across the codebase.
 */

/**
 * Workflow:
 * 1. Read or derive the configuration value from its canonical source.
 * 2. Validate it before exposing it to problem setup and solver dispatch.
 * 3. Keep policy decisions out of low-level numerical kernels.
 */

#pragma once

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../data/GlobalDefs.h"
#include "../io/ConfigParser.h"

class RuntimeParams
{
private:
    /**
     * @brief Parse a restricted numeric expression containing an optional pi.
     * Supported forms are a plain number, pi, -pi, coefficient*pi,
     * pi*coefficient, and pi/coefficient. This is not a general parser.
     */
    static double ParseMathExpr(std::string str, double default_val = 0.0)
    {
        if (str.empty())
            return default_val;

        // Remove whitespace before matching the supported literal forms.
        str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());

        // Separate ordinary numeric input from the supported pi forms.
        size_t pi_pos = str.find("pi");
        if (pi_pos == std::string::npos)
        {
            // Plain numeric input uses the standard conversion path.
            try
            {
                return std::stod(str);
            }
            catch (...)
            {
                return default_val;
            }
        }

        // M_PI supplies the platform's double-precision value of pi.
        double pi_val = M_PI;

        if (str == "pi")
            return pi_val;
        if (str == "-pi")
            return -pi_val;

        // Handle coefficient*pi and pi*coefficient.
        size_t star_pos = str.find('*');
        if (star_pos != std::string::npos)
        {
            std::string coeff_str = (pi_pos > star_pos) ? str.substr(0, star_pos) : str.substr(star_pos + 1);
            try
            {
                return std::stod(coeff_str) * pi_val;
            }
            catch (...)
            {
                return pi_val;
            }
        }

        // Handle pi/coefficient.
        size_t slash_pos = str.find('/');
        if (slash_pos != std::string::npos && pi_pos < slash_pos)
        {
            std::string coeff_str = str.substr(slash_pos + 1);
            try
            {
                return pi_val / std::stod(coeff_str);
            }
            catch (...)
            {
                return pi_val;
            }
        }

        return default_val; // Fall back when the expression cannot be parsed.
    }

public:
    /**
     * @brief Parses the parameter file and populates the SimConfig struct.
     * @param filename Path to the .par file.
     * @return A fully initialized SimConfig object.
     */
    static SimConfig Load(const std::string &filename)
    {
        ConfigParser parser;
        if (!parser.Load(filename))
        {
            throw std::runtime_error("RuntimeParams::Load failed: Could not open " + filename);
        }

        SimConfig cfg;

        cfg.grid.geometry = parser.GetString("geometry", "cartesian");
        // Grid topology uses canonical nblockx* keys.
        cfg.grid.nblockx1 = parser.GetInt("nblockx1", 1);
        cfg.grid.nblockx2 = parser.GetInt("nblockx2", 1);
        cfg.grid.nblockx3 = parser.GetInt("nblockx3", 1);
        cfg.grid.amr_max_blocks = parser.GetInt("max_blocks", 2000);

        if (cfg.grid.nblockx2 <= 0 && cfg.grid.nblockx3 > 0)
        {
            throw std::invalid_argument("Grid Error: Cross-dimensional topology anomaly. nblockx2 <= 0 but nblockx3 > 0 is not allowed.");
        }

        cfg.grid.dim = 3;
        if (cfg.grid.nblockx3 <= 0)
            cfg.grid.dim = 2;
        if (cfg.grid.nblockx2 <= 0)
            cfg.grid.dim = 1;

        cfg.grid.x1_min = ParseMathExpr(parser.GetString("x1_min", "0.0"));
        cfg.grid.x1_max = ParseMathExpr(parser.GetString("x1_max", "1.0"));
        cfg.grid.x2_min = ParseMathExpr(parser.GetString("x2_min", "0.0"));
        cfg.grid.x2_max = ParseMathExpr(parser.GetString("x2_max", "1.0"));
        cfg.grid.x3_min = ParseMathExpr(parser.GetString("x3_min", "0.0"));
        cfg.grid.x3_max = ParseMathExpr(parser.GetString("x3_max", "1.0"));

        cfg.grid.x1l_boundary_type = parser.GetString("x1l_boundary_type", "outflow");
        cfg.grid.x1r_boundary_type = parser.GetString("x1r_boundary_type", "outflow");
        cfg.grid.x2l_boundary_type = parser.GetString("x2l_boundary_type", "outflow");
        cfg.grid.x2r_boundary_type = parser.GetString("x2r_boundary_type", "outflow");
        cfg.grid.x3l_boundary_type = parser.GetString("x3l_boundary_type", "outflow");
        cfg.grid.x3r_boundary_type = parser.GetString("x3r_boundary_type", "outflow");

        // Numerical-method configuration.
        cfg.numerics.solver_name = parser.GetString("solver", "SW");
        cfg.numerics.cfl = parser.GetDouble("cfl", 0.8);
        cfg.numerics.limiter = parser.GetString("limiter", "minmod");
        cfg.numerics.reconstruction = parser.GetString("reconstruct", "pcm");
        // Prefer the canonical snake_case key while preserving compatibility
        // with existing parameter files that use the legacy spelling.
        cfg.numerics.time_integrator = parser.GetString(
            "time_integrator", parser.GetString("timeintegrator", "RK2"));
        if (parser.GetBool("EntropyFix", true))
        {
            // 0.1 is the default fraction of local spectral radius used as the
            // entropy-fix smoothing width.
            cfg.numerics.entropy_fix_coeff = parser.GetDouble("EntropyFixCoefficient", 0.1);
        }
        else
        {
            cfg.numerics.entropy_fix_coeff = 0.0; // Zero disables eigenvalue smoothing.
        }

        cfg.numerics.sml_rho = parser.GetDouble("sml_rho", 1e-12);
        cfg.numerics.max_eint = parser.GetDouble("max_eint", 1e21);

        // Execution backend.  This is independent of the time integrator:
        // a CUDA-enabled fat binary can still execute the CPU path at runtime.
        cfg.execution.compute_backend = parser.GetString("compute_backend", "cpu");
        std::transform(cfg.execution.compute_backend.begin(),
                       cfg.execution.compute_backend.end(),
                       cfg.execution.compute_backend.begin(), ::tolower);
        cfg.execution.cuda_device = parser.GetInt("cuda_device", 0);

        // Equation-of-state and physical-module configuration.
        cfg.physics.eos_type = parser.GetString("eos_type", "ideal");
        cfg.physics.eos_table_path = parser.GetString("eos_table_path", "");
        cfg.physics.gamma = parser.GetDouble("gamma", 1.4);

        // Nuclear reaction and NSE configuration.
        cfg.physics.burn.use_burn = parser.GetBool("use_burn", false);
        cfg.physics.burn.network_name = parser.GetString("network_name", "aprox19");
        cfg.physics.burn.nuclearTempMin = parser.GetDouble("nuclearTempMin", 1e9);
        cfg.physics.burn.nuclearDensMin = parser.GetDouble("nuclearDensMin", 1e-10);
        cfg.physics.burn.smallt = parser.GetDouble("smallt", 1e5);
        cfg.physics.burn.smallx = parser.GetDouble("smallx", 1e-20);

        cfg.physics.burn.enucDtFactor = parser.GetDouble("enucDtFactor", 1e30);
        cfg.physics.burn.use_nse = parser.GetBool("use_nse", true);
        cfg.physics.burn.nseTempThreshold = parser.GetDouble("nseTempThreshold", 4.5e9);
        cfg.physics.burn.nseDensThreshold = parser.GetDouble("nseDensThreshold", 1.0e6);

        cfg.physics.burn.enforce_mass_conservation = parser.GetBool("enforce_mass_conservation", true);
        cfg.physics.burn.verbose_level = parser.GetInt("burn_verbose_level", 0);

        // Stiff ODE solver configuration.
        cfg.physics.burn.odeconfig.ode_solver = parser.GetString("ode_solver", "BE_NR");
        cfg.physics.burn.odeconfig.linear_solver = parser.GetString("linear_solver", "Auto");

        cfg.physics.burn.odeconfig.rtol = parser.GetDouble("ode_rtol", 1e-4);
        cfg.physics.burn.odeconfig.atol = parser.GetDouble("ode_atol", 1e-8);
        cfg.physics.burn.odeconfig.max_newton_iter = parser.GetInt("ode_max_newton_iter", 50);
        cfg.physics.burn.odeconfig.max_substeps = parser.GetInt("ode_max_substeps", 10000);

        cfg.physics.burn.odeconfig.dt_safe_factor = parser.GetDouble("ode_dt_safe_fac", 0.9);
        cfg.physics.burn.odeconfig.dt_fac_max = parser.GetDouble("ode_dt_fac_max", 2.0);
        cfg.physics.burn.odeconfig.dt_fac_min = parser.GetDouble("ode_dt_fac_min", 0.1);
        cfg.physics.burn.odeconfig.initial_dt_frac = parser.GetDouble("ode_initial_dt_frac", 1e-3);

        cfg.physics.burn.odeconfig.use_numerical_jacobian = parser.GetBool("ode_use_numerical_jac", false);
        cfg.physics.burn.odeconfig.freeze_jacobian = parser.GetBool("ode_freeze_jacobian", false);

        // Diffusion configuration.
        cfg.physics.diffusion.use_diffusion = parser.GetBool("use_diffusion", false);
        cfg.physics.diffusion.integrator = parser.GetString("diff_integrator", "RKL2");
        cfg.physics.diffusion.diff_cfl = parser.GetDouble("diff_cfl", 0.8);
        cfg.physics.diffusion.max_stages = parser.GetInt("diff_max_stages", 256);

        cfg.physics.diffusion.use_thermal_diffusion = parser.GetBool("use_thermal_diff", false);
        cfg.physics.diffusion.use_viscous_diffusion = parser.GetBool("use_viscous_diff", false);
        cfg.physics.diffusion.use_species_diffusion = parser.GetBool("use_species_diff", false);
        cfg.physics.diffusion.nu_visc = parser.GetDouble("nu_visc", 0.0);
        cfg.physics.diffusion.alpha_therm = parser.GetDouble("alpha_therm", 0.0);
        cfg.physics.diffusion.D_spec = parser.GetDouble("D_spec", 0.0);

        // Helmholtz dispatch accepts the canonical helmholtz spelling. Normalize
        // only for this safety gate so mixed-case user input cannot bypass the
        // physical diffusionCoe path by supplying constant transport overrides.
        std::string eos_type_for_transport = cfg.physics.eos_type;
        std::transform(eos_type_for_transport.begin(), eos_type_for_transport.end(),
                       eos_type_for_transport.begin(), ::tolower);
        if (eos_type_for_transport == "helmholtz" && cfg.physics.diffusion.use_diffusion)
        {
            if (parser.HasKey("alpha_therm") || parser.HasKey("nu_visc") || parser.HasKey("D_spec"))
            {
                std::cerr << "[Fatal Error] When using HelmEos with diffusion, diffusion coefficients are computed physically. Do not set alpha_therm, nu_visc, or D_spec in the .par file." << std::endl;
                std::exit(EXIT_FAILURE);
            }
        }

        // Gravity configuration.
        std::string grav_type = parser.GetString("gravity_type", "none");
        std::transform(grav_type.begin(), grav_type.end(), grav_type.begin(), ::tolower);
        cfg.physics.gravity.type = grav_type;

        if (grav_type == "external")
        {
            cfg.physics.gravity.g_x = ParseMathExpr(parser.GetString("gravity_g_x", "0.0"));
            cfg.physics.gravity.g_y = ParseMathExpr(parser.GetString("gravity_g_y", "0.0"));
            cfg.physics.gravity.g_z = ParseMathExpr(parser.GetString("gravity_g_z", "0.0"));
        }
        else if (grav_type == "self")
        {
            cfg.physics.gravity.G_const = ParseMathExpr(parser.GetString("gravity_G", "6.6743e-8"));
        }

        // --- AMR (Adaptive Mesh Refinement) ---
        cfg.amr.lrefinemin = parser.GetInt("lrefinemin", 0);
        cfg.amr.lrefinemax = parser.GetInt("lrefinemax", 0);
        cfg.amr.regrid_interval = parser.GetInt("regrid_interval", 2);
        if (cfg.amr.regrid_interval < 1)
            throw std::invalid_argument("regrid_interval must be positive.");
        cfg.amr.refine_var = parser.GetString("refine_var", "DENS");
        std::replace(cfg.amr.refine_var.begin(), cfg.amr.refine_var.end(), '+', ',');
        cfg.amr.refine_on_rho = false;
        cfg.amr.refine_on_p = false;
        cfg.amr.refine_on_temp = false;
        cfg.amr.refine_on_velx = false;
        cfg.amr.refine_on_vely = false;
        cfg.amr.refine_on_velz = false;
        cfg.amr.refine_on_eng = false;
        cfg.amr.refine_on_vorticity = false;
        cfg.amr.refine_on_div_v = false;
        cfg.amr.refine_on_entropy = false;
        cfg.amr.refine_on_enuc = false;
        cfg.amr.refine_on_jeans = false;
        cfg.amr.refine_on_species = false;
        cfg.amr.refine_all_species = false;
        cfg.amr.refine_species_names.clear();
        std::stringstream refine_stream(cfg.amr.refine_var);
        std::string refine_token;
        while (std::getline(refine_stream, refine_token, ',')) {
            const size_t first = refine_token.find_first_not_of(" \t");
            const size_t last = refine_token.find_last_not_of(" \t");
            if (first == std::string::npos) continue;
            refine_token = refine_token.substr(first, last - first + 1);
            std::string canonical = refine_token;
            std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (canonical == "DENS") cfg.amr.refine_on_rho = true;
            else if (canonical == "PRES") cfg.amr.refine_on_p = true;
            else if (canonical == "TEMP") cfg.amr.refine_on_temp = true;
            else if (canonical == "VELX") cfg.amr.refine_on_velx = true;
            else if (canonical == "VELY") cfg.amr.refine_on_vely = true;
            else if (canonical == "VELZ") cfg.amr.refine_on_velz = true;
            else if (canonical == "ENER") cfg.amr.refine_on_eng = true;
            else if (canonical == "VORT") cfg.amr.refine_on_vorticity = true;
            else if (canonical == "DIVV") cfg.amr.refine_on_div_v = true;
            else if (canonical == "ENTR") cfg.amr.refine_on_entropy = true;
            else if (canonical == "ENUC") cfg.amr.refine_on_enuc = true;
            else if (canonical == "JENS") cfg.amr.refine_on_jeans = true;
            else if (canonical == "SPECIES") { cfg.amr.refine_on_species = true; cfg.amr.refine_all_species = true; }
            else if (canonical == "VORTICITY" || canonical == "DIV_V" || canonical == "ENTROPY" || canonical == "JEANS" ||
                     canonical == "RHO" || canonical == "P" || canonical == "U" || canonical == "V" || canonical == "W" || canonical == "ENG" || canonical == "ALL")
                throw std::invalid_argument("Use a canonical AMR indicator name instead of: " + refine_token);
            else {
                std::transform(refine_token.begin(), refine_token.end(), refine_token.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                cfg.amr.refine_on_species = true;
                cfg.amr.refine_species_names.push_back(refine_token);
            }
        }
        const auto warn_amr_disabled = [](const std::string& name, const std::string& reason) {
            std::cerr << "[RuntimeParams] Warning: AMR indicator " << name << " is disabled: " << reason << std::endl;
        };
        if (cfg.amr.refine_on_enuc && !cfg.physics.burn.use_burn) {
            warn_amr_disabled("ENUC", "the nuclear reaction network is not enabled"); cfg.amr.refine_on_enuc = false;
        }
        if (cfg.amr.refine_on_vely && cfg.grid.dim < 2) { warn_amr_disabled("VELY", "the simulation is one-dimensional"); cfg.amr.refine_on_vely = false; }
        if (cfg.amr.refine_on_velz && cfg.grid.dim < 3) { warn_amr_disabled("VELZ", "the simulation has fewer than three dimensions"); cfg.amr.refine_on_velz = false; }
        if (cfg.amr.refine_on_jeans) { warn_amr_disabled("JENS", "the self-gravity potential solver is not implemented"); cfg.amr.refine_on_jeans = false; }
        const auto has_amr_indicator = [&] {
            return cfg.amr.refine_on_rho || cfg.amr.refine_on_p || cfg.amr.refine_on_temp || cfg.amr.refine_on_velx ||
                cfg.amr.refine_on_vely || cfg.amr.refine_on_velz || cfg.amr.refine_on_eng || cfg.amr.refine_on_vorticity ||
                cfg.amr.refine_on_div_v || cfg.amr.refine_on_entropy || cfg.amr.refine_on_enuc || cfg.amr.refine_on_jeans || cfg.amr.refine_on_species;
        };
        if (!has_amr_indicator()) throw std::invalid_argument("refine_var has no usable AMR indicator for this configuration.");
        cfg.amr.refine_threshold = parser.GetDouble("refine_threshold", 0.8);
        cfg.amr.derefine_threshold = parser.GetDouble("derefine_threshold", 0.2);
        if (cfg.amr.refine_threshold < 0.0 || cfg.amr.refine_threshold > 1.0 || cfg.amr.derefine_threshold < 0.0 ||
            cfg.amr.derefine_threshold >= cfg.amr.refine_threshold)
            throw std::invalid_argument("AMR Lohner thresholds require ordered values in [0, 1].");
        // Time limits and output configuration.
        cfg.io.tmax = parser.GetDouble("tmax", 0.1);
        cfg.io.max_steps = parser.GetInt("max_steps", -1);

        cfg.io.out_dir = parser.GetString("out_dir", "data");
        cfg.io.base_name = parser.GetString("base_name", "arch");

        cfg.io.plt_dt = parser.GetDouble("plt_dt", -1.0);
        cfg.io.plt_dstep = parser.GetInt("plt_dstep", -1);

        cfg.io.chk_dt = parser.GetDouble("chk_dt", -1.0);
        cfg.io.chk_dstep = parser.GetInt("chk_dstep", -1);

        cfg.io.restart = parser.GetBool("restart", false);

        std::string r_file = parser.GetString("restart_file", "");
        r_file.erase(0, r_file.find_first_not_of(" \t\r\n"));
        r_file.erase(r_file.find_last_not_of(" \t\r\n") + 1);
        cfg.io.restart_file = r_file;

        if (cfg.io.restart)
        {
            std::cout << "[RuntimeParams] Restart Enabled. Target file: '"
                      << cfg.io.restart_file << "'" << std::endl;
        }

        std::string plt_vars = parser.GetString("plt_variables", "ALL");
        std::replace(plt_vars.begin(), plt_vars.end(), '+', ',');
        const auto clear_plot_variables = [&] {
            cfg.io.vars.rho = cfg.io.vars.temp = cfg.io.vars.u = cfg.io.vars.v = cfg.io.vars.w = false;
            cfg.io.vars.p = cfg.io.vars.eng = cfg.io.vars.species = false;
            cfg.io.vars.vort = cfg.io.vars.divv = cfg.io.vars.entr = cfg.io.vars.enuc = cfg.io.vars.jens = false;
            cfg.io.plot_species_names.clear();
        };
        const auto enable_conserved = [&] { cfg.io.vars.rho = cfg.io.vars.u = cfg.io.vars.v = cfg.io.vars.w = cfg.io.vars.eng = true; };
        const auto enable_all = [&] {
            cfg.io.vars.rho = cfg.io.vars.temp = cfg.io.vars.u = cfg.io.vars.v = cfg.io.vars.w = true;
            cfg.io.vars.p = cfg.io.vars.eng = cfg.io.vars.species = cfg.io.vars.vort = cfg.io.vars.divv = cfg.io.vars.entr = cfg.io.vars.enuc = true;
        };
        clear_plot_variables();
        std::stringstream output_stream(plt_vars);
        std::string raw_token;
        while (std::getline(output_stream, raw_token, ',')) {
            const size_t first = raw_token.find_first_not_of(" \t");
            const size_t last = raw_token.find_last_not_of(" \t");
            if (first == std::string::npos) continue;
            raw_token = raw_token.substr(first, last - first + 1);
            std::string canonical = raw_token;
            std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (canonical == "ALL") enable_all();
            else if (canonical == "CONSERVED") enable_conserved();
            else if (canonical == "DENS") cfg.io.vars.rho = true;
            else if (canonical == "TEMP") cfg.io.vars.temp = true;
            else if (canonical == "VELX") cfg.io.vars.u = true;
            else if (canonical == "VELY") cfg.io.vars.v = true;
            else if (canonical == "VELZ") cfg.io.vars.w = true;
            else if (canonical == "PRES") cfg.io.vars.p = true;
            else if (canonical == "ENER") cfg.io.vars.eng = true;
            else if (canonical == "SPECIES") cfg.io.vars.species = true;
            else if (canonical == "VORT") cfg.io.vars.vort = true;
            else if (canonical == "DIVV") cfg.io.vars.divv = true;
            else if (canonical == "ENTR") cfg.io.vars.entr = true;
            else if (canonical == "ENUC") cfg.io.vars.enuc = true;
            else if (canonical == "JENS") cfg.io.vars.jens = true;
            else if (canonical == "VORTICITY" || canonical == "DIV_V" || canonical == "ENTROPY" || canonical == "JEANS" ||
                     canonical == "RHO" || canonical == "P" || canonical == "U" || canonical == "V" || canonical == "W" || canonical == "ENG")
                throw std::invalid_argument("Use a canonical PLT variable name instead of: " + raw_token);
            else {
                std::transform(raw_token.begin(), raw_token.end(), raw_token.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                cfg.io.plot_species_names.push_back(raw_token);
            }
        }
        const auto warn_plot_disabled = [](const std::string& name, const std::string& reason) {
            std::cerr << "[RuntimeParams] Warning: PLT variable " << name << " is disabled: " << reason << std::endl;
        };
        if (cfg.io.vars.enuc && !cfg.physics.burn.use_burn) { warn_plot_disabled("ENUC", "the nuclear reaction network is not enabled"); cfg.io.vars.enuc = false; }
        if (cfg.io.vars.v && cfg.grid.dim < 2) { warn_plot_disabled("VELY", "the simulation is one-dimensional"); cfg.io.vars.v = false; }
        if (cfg.io.vars.w && cfg.grid.dim < 3) { warn_plot_disabled("VELZ", "the simulation has fewer than three dimensions"); cfg.io.vars.w = false; }
        if (cfg.io.vars.jens) { warn_plot_disabled("JENS", "the self-gravity potential solver is not implemented"); cfg.io.vars.jens = false; }
        // Preserve untyped parameters for problem-specific setup.
        for (const auto &[key, val_str] : parser.GetAllParams())
        {
            try
            {
                // Store numeric custom parameters directly.
                double val = std::stod(val_str);
                cfg.custom_params[key] = val;
            }
            catch (...)
            {
                // Retain values such as solver names that are not valid doubles.
                cfg.custom_string_params[key] = val_str;
            }
        }

        return cfg;
    }
};
