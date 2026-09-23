/**
 * @file RuntimeParams.h
 * @brief Load a parameter file into the shared simulation configuration.
 *
 * The loader converts typed sections, checks control values and preserves
 * problem-specific parameters. Enum-like tokens are case-normalized here;
 * policy registration and backend resolution validate their meanings later.
 * Workflow:
 * 1. Read a parameter file through typed parser keys.
 * 2. Resolve defaults and validate cross-field configuration.
 * 3. Return one SimConfig to case setup and runtime dispatch.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "data/GlobalDefs.h"
#include "io/ConfigParser.h"
#include "core/config/StandardParameters.h"
#include "core/config/ConfigValidation.h"

class RuntimeParams
{
private:
    /**
     * @brief Store enum-like runtime tokens in their ASCII lowercase form.
     *
     * This is deliberately only case canonicalization: it does not validate
     * tokens or translate aliases. Unknown values therefore remain unknown
     * and are rejected by the resolved execution-plan parsers downstream.
     */
    static std::string CanonicalizeEnumToken(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) {
                return c >= 'A' && c <= 'Z'
                    ? static_cast<char>(c - 'A' + 'a')
                    : static_cast<char>(c);
            });
        return value;
    }

    /**
     * @brief Parse a restricted numeric expression containing an optional pi.
     * Supported forms are a plain number, pi, -pi, coefficient*pi,
     * pi*coefficient, and pi/coefficient. This is not a general parser.
     */
    static double ParseMathExpr(const std::string& str, const std::string& key)
    {
        return ConfigParser::ParseExpression(key, str);
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

        return Resolve(parser);
    }

    static SimConfig LoadText(const std::string &text,
                             std::shared_ptr<arch::preview::ParameterReadTrace> reads = {})
    {
        std::istringstream input(text);
        ConfigParser parser;
        parser.Load(input);
        auto config = Resolve(parser);
        if (reads) {
            reads->capture_input(parser.GetAllParams(), config.custom_params, config.custom_string_params);
            config.parameter_reads = std::move(reads);
        }
        return config;
    }

private:
    static SimConfig Resolve(const ConfigParser &parser)
    {

        arch::config::ValidateStandardTokens(parser);
        SimConfig cfg;

        cfg.grid.geometry = CanonicalizeEnumToken(
            parser.GetString("geometry", arch::config::DefaultString("geometry")));
        // Grid topology uses canonical nblockx* keys.
        cfg.grid.nblockx1 = parser.GetInt("nblockx1", arch::config::DefaultInt("nblockx1"));
        cfg.grid.nblockx2 = parser.GetInt("nblockx2", arch::config::DefaultInt("nblockx2"));
        cfg.grid.nblockx3 = parser.GetInt("nblockx3", arch::config::DefaultInt("nblockx3"));
        cfg.grid.amr_max_blocks = parser.GetInt("max_blocks", arch::config::DefaultInt("max_blocks"));

        if (cfg.grid.nblockx2 <= 0 && cfg.grid.nblockx3 > 0)
        {
            throw ConfigValueError("nblockx3", "INVALID_TOPOLOGY", "The third axis requires a positive nblockx2.");
        }

        cfg.grid.dim = 3;
        if (cfg.grid.nblockx3 <= 0)
            cfg.grid.dim = 2;
        if (cfg.grid.nblockx2 <= 0)
            cfg.grid.dim = 1;

        cfg.grid.x1_min = ParseMathExpr(parser.GetString("x1_min", arch::config::DefaultString("x1_min")), "x1_min");
        cfg.grid.x1_max = ParseMathExpr(parser.GetString("x1_max", arch::config::DefaultString("x1_max")), "x1_max");
        cfg.grid.x2_min = ParseMathExpr(parser.GetString("x2_min", arch::config::DefaultString("x2_min")), "x2_min");
        cfg.grid.x2_max = ParseMathExpr(parser.GetString("x2_max", arch::config::DefaultString("x2_max")), "x2_max");
        cfg.grid.x3_min = ParseMathExpr(parser.GetString("x3_min", arch::config::DefaultString("x3_min")), "x3_min");
        cfg.grid.x3_max = ParseMathExpr(parser.GetString("x3_max", arch::config::DefaultString("x3_max")), "x3_max");

        cfg.grid.x1l_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x1l_boundary_type", arch::config::DefaultString("x1l_boundary_type")));
        cfg.grid.x1r_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x1r_boundary_type", arch::config::DefaultString("x1r_boundary_type")));
        cfg.grid.x2l_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x2l_boundary_type", arch::config::DefaultString("x2l_boundary_type")));
        cfg.grid.x2r_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x2r_boundary_type", arch::config::DefaultString("x2r_boundary_type")));
        cfg.grid.x3l_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x3l_boundary_type", arch::config::DefaultString("x3l_boundary_type")));
        cfg.grid.x3r_boundary_type = CanonicalizeEnumToken(
            parser.GetString("x3r_boundary_type", arch::config::DefaultString("x3r_boundary_type")));

        // Numerical-method configuration.
        cfg.numerics.solver_name = parser.GetString("solver", arch::config::DefaultString("solver"));
        cfg.numerics.dt_init = parser.GetDouble("dt_init", arch::config::DefaultDouble("dt_init"));
        cfg.numerics.dt_min = parser.GetDouble("dt_min", arch::config::DefaultDouble("dt_min"));
        cfg.numerics.tstep_change_factor = parser.GetDouble("tstep_change_factor", arch::config::DefaultDouble("tstep_change_factor"));
        cfg.numerics.cfl = parser.GetDouble("cfl", arch::config::DefaultDouble("cfl"));
        cfg.numerics.limiter = parser.GetString("limiter", arch::config::DefaultString("limiter"));
        cfg.numerics.reconstruction = parser.GetString("reconstruct", arch::config::DefaultString("reconstruct"));
        cfg.numerics.time_integrator = parser.GetString(
            "time_integrator", arch::config::DefaultString("time_integrator"));
        if (parser.GetBool("EntropyFix", arch::config::DefaultBool("EntropyFix")))
        {
            // 0.1 is the default fraction of local spectral radius used as the
            // entropy-fix smoothing width.
            cfg.numerics.entropy_fix_coeff = parser.GetDouble("EntropyFixCoefficient", arch::config::DefaultDouble("EntropyFixCoefficient"));
        }
        else
        {
            cfg.numerics.entropy_fix_coeff = 0.0; // Zero disables eigenvalue smoothing.
        }

        cfg.numerics.sml_rho = parser.GetDouble("sml_rho", arch::config::DefaultDouble("sml_rho"));
        cfg.numerics.min_eint = parser.GetDouble("min_eint", arch::config::DefaultDouble("min_eint"));
        cfg.numerics.max_eint = parser.GetDouble("max_eint", arch::config::DefaultDouble("max_eint"));
        // Execution backend.  This is independent of the time integrator:
        // a CUDA-enabled fat binary can still execute the CPU path at runtime.
        cfg.execution.compute_backend = CanonicalizeEnumToken(
            parser.GetString("compute_backend", arch::config::DefaultString("compute_backend")));
        cfg.execution.cuda_device = parser.GetInt("cuda_device", arch::config::DefaultInt("cuda_device"));

        // Equation-of-state and physical-module configuration.
        cfg.physics.eos_type = parser.GetString("eos_type", arch::config::DefaultString("eos_type"));
        cfg.physics.eos_table_path = parser.GetString("eos_table_path", arch::config::DefaultString("eos_table_path"));
        cfg.physics.eos_helm_table_path = parser.GetString("eos_helm_table_path", arch::config::DefaultString("eos_helm_table_path"));
        cfg.physics.gamma = parser.GetDouble("gamma", arch::config::DefaultDouble("gamma"));

        // Nuclear reaction and NSE configuration.
        cfg.physics.burn.use_burn = parser.GetBool("use_burn", arch::config::DefaultBool("use_burn"));
        cfg.physics.burn.network_name = parser.GetString("network_name", arch::config::DefaultString("network_name"));
        cfg.physics.burn.nuclearTempMin = parser.GetDouble("nuclearTempMin", arch::config::DefaultDouble("nuclearTempMin"));
        cfg.physics.burn.nuclearDensMin = parser.GetDouble("nuclearDensMin", arch::config::DefaultDouble("nuclearDensMin"));
        cfg.physics.burn.smallt = parser.GetDouble("smallt", arch::config::DefaultDouble("smallt"));
        cfg.physics.burn.smallx = parser.GetDouble("smallx", arch::config::DefaultDouble("smallx"));

        cfg.physics.burn.enucDtFactor = parser.GetDouble("enucDtFactor", arch::config::DefaultDouble("enucDtFactor"));
        const std::string nse_request = CanonicalizeEnumToken(
            parser.GetString("use_nse", arch::config::DefaultString("use_nse")));
        cfg.physics.burn.nse_auto = nse_request == "auto";
        cfg.physics.burn.use_nse = cfg.physics.burn.nse_auto
            || parser.GetBool("use_nse", arch::config::DefaultBool("use_nse"));
        cfg.physics.burn.nseTempThreshold = parser.GetDouble("nseTempThreshold", arch::config::DefaultDouble("nseTempThreshold"));
        cfg.physics.burn.nseDensThreshold = parser.GetDouble("nseDensThreshold", arch::config::DefaultDouble("nseDensThreshold"));
        // Stiff ODE solver configuration.
        cfg.physics.burn.odeconfig.ode_solver = parser.GetString("ode_solver", arch::config::DefaultString("ode_solver"));
        cfg.physics.burn.odeconfig.linear_solver = parser.GetString("linear_solver", arch::config::DefaultString("linear_solver"));

        cfg.physics.burn.odeconfig.rtol = parser.GetDouble("ode_rtol", arch::config::DefaultDouble("ode_rtol"));
        cfg.physics.burn.odeconfig.atol = parser.GetDouble("ode_atol", arch::config::DefaultDouble("ode_atol"));
        cfg.physics.burn.odeconfig.max_newton_iter = parser.GetInt("ode_max_newton_iter", arch::config::DefaultInt("ode_max_newton_iter"));
        cfg.physics.burn.odeconfig.max_substeps = parser.GetInt("ode_max_substeps", arch::config::DefaultInt("ode_max_substeps"));

        cfg.physics.burn.odeconfig.dt_safe_factor = parser.GetDouble("ode_dt_safe_fac", arch::config::DefaultDouble("ode_dt_safe_fac"));
        cfg.physics.burn.odeconfig.dt_fac_max = parser.GetDouble("ode_dt_fac_max", arch::config::DefaultDouble("ode_dt_fac_max"));
        cfg.physics.burn.odeconfig.dt_fac_min = parser.GetDouble("ode_dt_fac_min", arch::config::DefaultDouble("ode_dt_fac_min"));
        cfg.physics.burn.odeconfig.initial_dt_frac = parser.GetDouble("ode_initial_dt_frac", arch::config::DefaultDouble("ode_initial_dt_frac"));


        // Diffusion configuration.
        cfg.physics.diffusion.use_diffusion = parser.GetBool("use_diffusion", arch::config::DefaultBool("use_diffusion"));
        cfg.physics.diffusion.integrator = parser.GetString("diff_integrator", arch::config::DefaultString("diff_integrator"));
        cfg.physics.diffusion.diff_cfl = parser.GetDouble("diff_cfl", arch::config::DefaultDouble("diff_cfl"));
        cfg.physics.diffusion.max_stages = parser.GetInt("diff_max_stages", arch::config::DefaultInt("diff_max_stages"));

        cfg.physics.diffusion.use_thermal_diffusion = parser.GetBool("use_thermal_diff", arch::config::DefaultBool("use_thermal_diff"));
        cfg.physics.diffusion.use_viscous_diffusion = parser.GetBool("use_viscous_diff", arch::config::DefaultBool("use_viscous_diff"));
        cfg.physics.diffusion.use_species_diffusion = parser.GetBool("use_species_diff", arch::config::DefaultBool("use_species_diff"));
        cfg.physics.diffusion.nu_visc = parser.GetDouble("nu_visc", arch::config::DefaultDouble("nu_visc"));
        cfg.physics.diffusion.alpha_therm = parser.GetDouble("alpha_therm", arch::config::DefaultDouble("alpha_therm"));
        cfg.physics.diffusion.D_spec = parser.GetDouble("D_spec", arch::config::DefaultDouble("D_spec"));

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
                const auto key = parser.HasKey("alpha_therm") ? "alpha_therm"
                    : parser.HasKey("nu_visc") ? "nu_visc" : "D_spec";
                throw ConfigValueError(key, "INAPPLICABLE_PARAMETER",
                    "Helmholtz diffusion computes transport coefficients; omit explicit alpha_therm, nu_visc and D_spec.");
            }
        }

        // Gravity configuration.
        std::string grav_type = CanonicalizeEnumToken(
            parser.GetString("gravity_type", arch::config::DefaultString("gravity_type")));
        cfg.physics.gravity.type = grav_type;
        cfg.physics.gravity.boundary = CanonicalizeEnumToken(parser.GetString("gravity_boundary", arch::config::DefaultString("gravity_boundary")));
        cfg.physics.gravity.relative_tolerance = parser.GetDouble("gravity_rtol", arch::config::DefaultDouble("gravity_rtol"));
        cfg.physics.gravity.absolute_tolerance = parser.GetDouble("gravity_atol", arch::config::DefaultDouble("gravity_atol"));
        cfg.physics.gravity.max_cycles = parser.GetInt("gravity_max_cycles", arch::config::DefaultInt("gravity_max_cycles"));
        cfg.physics.gravity.G_const = parser.HasKey("gravity_G") ? ParseMathExpr(parser.GetString("gravity_G", ""), "gravity_G")
            : arch::config::DefaultDouble("gravity_G");
        cfg.physics.gravity.g_x = ParseMathExpr(parser.GetString("gravity_g_x", arch::config::DefaultString("gravity_g_x")), "gravity_g_x");
        cfg.physics.gravity.g_y = ParseMathExpr(parser.GetString("gravity_g_y", arch::config::DefaultString("gravity_g_y")), "gravity_g_y");
        cfg.physics.gravity.g_z = ParseMathExpr(parser.GetString("gravity_g_z", arch::config::DefaultString("gravity_g_z")), "gravity_g_z");

        // --- AMR (Adaptive Mesh Refinement) ---
        cfg.amr.lrefinemin = parser.GetInt("lrefinemin", arch::config::DefaultInt("lrefinemin"));
        cfg.amr.lrefinemax = parser.GetInt("lrefinemax", arch::config::DefaultInt("lrefinemax"));
        cfg.amr.regrid_interval = parser.GetInt("regrid_interval", arch::config::DefaultInt("regrid_interval"));
        if (cfg.amr.regrid_interval < 1)
            throw std::invalid_argument("regrid_interval must be positive.");
        cfg.amr.refine_var = parser.GetString("refine_var", arch::config::DefaultString("refine_var"));
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
        if (cfg.amr.refine_on_jeans) { warn_amr_disabled("JENS", "the Jeans refinement/plot diagnostic is not implemented"); cfg.amr.refine_on_jeans = false; }
        const auto has_amr_indicator = [&] {
            return cfg.amr.refine_on_rho || cfg.amr.refine_on_p || cfg.amr.refine_on_temp || cfg.amr.refine_on_velx ||
                cfg.amr.refine_on_vely || cfg.amr.refine_on_velz || cfg.amr.refine_on_eng || cfg.amr.refine_on_vorticity ||
                cfg.amr.refine_on_div_v || cfg.amr.refine_on_entropy || cfg.amr.refine_on_enuc || cfg.amr.refine_on_jeans || cfg.amr.refine_on_species;
        };
        if (!has_amr_indicator()) throw std::invalid_argument("refine_var has no usable AMR indicator for this configuration.");
        cfg.amr.refine_threshold = parser.GetDouble("refine_threshold", arch::config::DefaultDouble("refine_threshold"));
        cfg.amr.derefine_threshold = parser.GetDouble("derefine_threshold", arch::config::DefaultDouble("derefine_threshold"));
        // Time limits and output configuration.
        cfg.io.tmax = parser.GetDouble("tmax", arch::config::DefaultDouble("tmax"));
        cfg.io.max_steps = parser.GetInt("max_steps", arch::config::DefaultInt("max_steps"));

        cfg.io.out_dir = parser.GetString("out_dir", arch::config::DefaultString("out_dir"));
        cfg.io.base_name = parser.GetString("base_name", arch::config::DefaultString("base_name"));

        cfg.io.plt_dt = parser.GetDouble("plt_dt", arch::config::DefaultDouble("plt_dt"));
        cfg.io.plt_dstep = parser.GetInt("plt_dstep", arch::config::DefaultInt("plt_dstep"));

        cfg.io.chk_dt = parser.GetDouble("chk_dt", arch::config::DefaultDouble("chk_dt"));
        cfg.io.chk_dstep = parser.GetInt("chk_dstep", arch::config::DefaultInt("chk_dstep"));

        cfg.io.restart = parser.GetBool("restart", arch::config::DefaultBool("restart"));

        std::string r_file = parser.GetString("restart_file", arch::config::DefaultString("restart_file"));
        r_file.erase(0, r_file.find_first_not_of(" \t\r\n"));
        r_file.erase(r_file.find_last_not_of(" \t\r\n") + 1);
        cfg.io.restart_file = r_file;

        if (cfg.io.restart)
        {
            if (cfg.io.restart_file.empty()) {
                throw std::invalid_argument(
                    "restart_file must be non-empty when restart = true.");
            }
            std::cout << "[RuntimeParams] Restart Enabled. Target file: '"
                      << cfg.io.restart_file << "'" << std::endl;
        }

        std::string plt_vars = parser.GetString("plt_variables", arch::config::DefaultString("plt_variables"));
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
        if (cfg.io.vars.jens) { warn_plot_disabled("JENS", "the Jeans refinement/plot diagnostic is not implemented"); cfg.io.vars.jens = false; }
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

        arch::config::ValidateControls(cfg);
        return cfg;
    }
};
