/**
 * @file RuntimeParams.h
 * @brief Load a parameter file into the shared simulation configuration.
 *
 * The loader converts typed sections, checks control values and preserves
 * problem-specific parameters. Enum-like tokens are case-normalized here;
 * policy registration and backend resolution validate their meanings later.
 * Workflow:
 * 1. Parse once and check standard/case requirements through the shared resolver.
 * 2. Map resolved values (including only approved defaults) into typed storage.
 * 3. Return mutable preparation storage for case setup; final freezing is separate.
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
#include "core/config/ConfigurationInput.h"
#include "core/config/CaseParameterValues.h"

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


public:
    // Case-aware production entry: aggregate declarations before constructing
    // the mutable preparation configuration. No model or resource is created.
    static SimConfig Load(const std::string& filename, const std::string& case_id)
    {
        ConfigParser parser;
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("RuntimeParams::Load failed: Could not open " + filename);
        parser.Read(file, filename);
        auto input = arch::config::AnalyzeConfigurationInput(parser, case_id);
        input.RequireDeclaredInputs();
        input.raw_tokens = parser.GetAllParams();
        auto config = Resolve(parser, input.standard);
        config.loaded_input_ = std::make_shared<const arch::config::ConfigurationInput>(std::move(input));
        return config;
    }

    static SimConfig LoadText(const std::string& text, const std::string& case_id,
                             arch::config::ConfigurationPurpose purpose,
                             std::shared_ptr<arch::preview::ParameterReadTrace> reads = {})
    {
        std::istringstream stream(text);
        ConfigParser parser;
        parser.Read(stream);
        auto input = arch::config::AnalyzeConfigurationInput(parser, case_id, purpose);
        input.RequireDeclaredInputs();
        input.raw_tokens = parser.GetAllParams();
        auto config = Resolve(parser, input.standard);
        config.loaded_input_ = std::make_shared<const arch::config::ConfigurationInput>(std::move(input));
        if (reads) {
            reads->capture_input(parser.GetAllParams(), config.custom_params, config.custom_string_params);
            config.parameter_reads = std::move(reads);
        }
        return config;
    }

private:
    static SimConfig Resolve(const ConfigParser &parser,
                             const arch::config::StandardInputResolution& inputs)
    {

        SimConfig cfg;
        // Completeness was checked before this private mapping. Missing inactive
        // records leave storage alone; initialization is not an input or source.
        const auto assign = [&]<class T>(T& destination, const char* key) {
            const auto& record = inputs.parameters.at(key);
            if (record.resolved) {
                const auto* value = std::get_if<T>(&*record.resolved);
                if (!value) throw std::logic_error(std::string("Resolved input type mismatch: ") + key);
                destination = *value;
                return true;
            }
            if (record.requirement.value != false)
                throw ConfigValueError(key, "MISSING_PARAMETER", "Runtime mapping requires a resolved input.");
            return false;
        };
        const auto assign_enum = [&](std::string& destination, const char* key) {
            if (assign(destination, key)) destination = CanonicalizeEnumToken(destination);
        };

        assign_enum(cfg.grid.geometry, "geometry");
        // Grid topology uses canonical nblockx* keys.
        assign(cfg.grid.nblockx1, "nblockx1");
        assign(cfg.grid.nblockx2, "nblockx2");
        assign(cfg.grid.nblockx3, "nblockx3");
        assign(cfg.grid.amr_max_blocks, "max_blocks");

        if (cfg.grid.nblockx2 <= 0 && cfg.grid.nblockx3 > 0)
        {
            throw ConfigValueError("nblockx3", "INVALID_TOPOLOGY", "The third axis requires a positive nblockx2.");
        }

        cfg.grid.dim = 3;
        if (cfg.grid.nblockx3 <= 0)
            cfg.grid.dim = 2;
        if (cfg.grid.nblockx2 <= 0)
            cfg.grid.dim = 1;

        assign(cfg.grid.x1_min, "x1_min");
        assign(cfg.grid.x1_max, "x1_max");
        assign(cfg.grid.x2_min, "x2_min");
        assign(cfg.grid.x2_max, "x2_max");
        assign(cfg.grid.x3_min, "x3_min");
        assign(cfg.grid.x3_max, "x3_max");

        assign_enum(cfg.grid.x1l_boundary_type, "x1l_boundary_type");
        assign_enum(cfg.grid.x1r_boundary_type, "x1r_boundary_type");
        assign_enum(cfg.grid.x2l_boundary_type, "x2l_boundary_type");
        assign_enum(cfg.grid.x2r_boundary_type, "x2r_boundary_type");
        assign_enum(cfg.grid.x3l_boundary_type, "x3l_boundary_type");
        assign_enum(cfg.grid.x3r_boundary_type, "x3r_boundary_type");

        // Numerical-method configuration.
        assign(cfg.numerics.solver_name, "solver");
        assign(cfg.numerics.dt_init, "dt_init");
        assign(cfg.numerics.dt_max, "dt_max");
        std::string hll_speed;
        if (assign(hll_speed, "hll_wave_speed"))
            cfg.numerics.hll_roe_wave_speed = CanonicalizeEnumToken(hll_speed) == "roe";
        assign(cfg.numerics.dt_min, "dt_min");
        assign(cfg.numerics.tstep_change_factor, "tstep_change_factor");
        assign(cfg.numerics.cfl, "cfl");
        assign(cfg.numerics.limiter, "limiter");
        assign(cfg.numerics.reconstruction, "reconstruct");
        assign(cfg.numerics.time_integrator, "time_integrator");
        assign(cfg.numerics.entropy_fix_coeff, "EntropyFixCoefficient");
        bool entropy_fix = false;
        if (assign(entropy_fix, "EntropyFix") && !entropy_fix)
            cfg.numerics.entropy_fix_coeff = 0.0; // Explicit false disables smoothing.

        assign(cfg.numerics.sml_rho, "sml_rho");
        assign(cfg.numerics.min_eint, "min_eint");
        assign(cfg.numerics.max_eint, "max_eint");
        // Execution backend.  This is independent of the time integrator:
        // a CUDA-enabled fat binary can still execute the CPU path at runtime.
        assign_enum(cfg.execution.compute_backend, "compute_backend");
        assign(cfg.execution.cuda_device, "cuda_device");

        // Equation-of-state and physical-module configuration.
        assign(cfg.physics.eos_type, "eos_type");
        assign(cfg.physics.eos_table_path, "eos_table_path");
        assign(cfg.physics.eos_helm_table_path, "eos_helm_table_path");
        assign(cfg.physics.eos_coulomb_mult, "eos_coulomb_mult");
        assign(cfg.physics.gamma, "gamma");

        // Nuclear reaction and NSE configuration.
        assign(cfg.physics.burn.use_burn, "use_burn");
        assign(cfg.physics.burn.network_name, "network_name");
        assign(cfg.physics.burn.nuclearTempMin, "nuclearTempMin");
        assign(cfg.physics.burn.nuclearDensMin, "nuclearDensMin");
        assign(cfg.physics.burn.smallt, "smallt");
        assign(cfg.physics.burn.smallx, "smallx");

        assign(cfg.physics.burn.enucDtFactor, "enucDtFactor");
        std::string nse_request;
        if (assign(nse_request, "use_nse")) {
            nse_request = CanonicalizeEnumToken(nse_request);
            cfg.physics.burn.nse_auto = nse_request == "auto";
            cfg.physics.burn.use_nse = cfg.physics.burn.nse_auto || nse_request == "true";
        }
        assign(cfg.physics.burn.nseTempThreshold, "nseTempThreshold");
        assign(cfg.physics.burn.nseDensThreshold, "nseDensThreshold");
        // Stiff ODE solver configuration.
        assign(cfg.physics.burn.odeconfig.ode_solver, "ode_solver");
        assign(cfg.physics.burn.odeconfig.linear_solver, "linear_solver");

        assign(cfg.physics.burn.odeconfig.rtol, "ode_rtol");
        assign(cfg.physics.burn.odeconfig.atol, "ode_atol");
        assign(cfg.physics.burn.odeconfig.max_newton_iter, "ode_max_newton_iter");
        assign(cfg.physics.burn.odeconfig.max_substeps, "ode_max_substeps");

        assign(cfg.physics.burn.odeconfig.dt_safe_factor, "ode_dt_safe_fac");
        assign(cfg.physics.burn.odeconfig.dt_fac_max, "ode_dt_fac_max");
        assign(cfg.physics.burn.odeconfig.dt_fac_min, "ode_dt_fac_min");
        assign(cfg.physics.burn.odeconfig.initial_dt_frac, "ode_initial_dt_frac");


        // Diffusion configuration.
        assign(cfg.physics.diffusion.use_diffusion, "use_diffusion");
        assign(cfg.physics.diffusion.integrator, "diff_integrator");
        assign(cfg.physics.diffusion.diff_cfl, "diff_cfl");
        assign(cfg.physics.diffusion.max_stages, "diff_max_stages");

        assign(cfg.physics.diffusion.use_thermal_diffusion, "use_thermal_diff");
        assign(cfg.physics.diffusion.use_viscous_diffusion, "use_viscous_diff");
        assign(cfg.physics.diffusion.use_species_diffusion, "use_species_diff");
        assign(cfg.physics.diffusion.nu_visc, "nu_visc");
        assign(cfg.physics.diffusion.alpha_therm, "alpha_therm");
        assign(cfg.physics.diffusion.D_spec, "D_spec");

        // Gravity configuration.
        assign_enum(cfg.physics.gravity.type, "gravity_type");
        assign_enum(cfg.physics.gravity.boundary, "gravity_boundary");
        assign(cfg.physics.gravity.relative_tolerance, "gravity_rtol");
        assign(cfg.physics.gravity.absolute_tolerance, "gravity_atol");
        assign(cfg.physics.gravity.max_cycles, "gravity_max_cycles");
        assign(cfg.physics.gravity.g_x, "gravity_g_x");
        assign(cfg.physics.gravity.g_y, "gravity_g_y");
        assign(cfg.physics.gravity.g_z, "gravity_g_z");

        // --- AMR (Adaptive Mesh Refinement) ---
        assign(cfg.amr.lrefinemin, "lrefinemin");
        assign(cfg.amr.lrefinemax, "lrefinemax");
        assign(cfg.amr.regrid_interval, "regrid_interval");
        if (cfg.amr.regrid_interval < 1)
            throw std::invalid_argument("regrid_interval must be positive.");
        assign(cfg.amr.refine_var, "refine_var");
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
        assign(cfg.amr.refine_threshold, "refine_threshold");
        assign(cfg.amr.derefine_threshold, "derefine_threshold");
        // Time limits and output configuration.
        assign(cfg.io.tmax, "tmax");
        assign(cfg.io.max_steps, "max_steps");

        assign(cfg.io.out_dir, "out_dir");
        assign(cfg.io.base_name, "base_name");

        assign(cfg.io.plt_dt, "plt_dt");
        assign(cfg.io.plt_dstep, "plt_dstep");

        assign(cfg.io.chk_dt, "chk_dt");
        assign(cfg.io.chk_dstep, "chk_dstep");

        assign(cfg.io.restart, "restart");

        std::string r_file;
        assign(r_file, "restart_file");
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

        std::string plt_vars;
        assign(plt_vars, "plt_variables");
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
        arch::config::CaptureCaseParameterValues(parser, cfg);

        arch::config::ValidateControls(cfg);
        return cfg;
    }
};
