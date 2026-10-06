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
#include "core/config/RefinementSelection.h"

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
        input.raw_text = parser.InputText();
        input.raw_text_available = true;
        auto config = Resolve(input.standard);
        CaptureResolvedCaseValues(config, input);
        config.loaded_input_ = std::make_shared<const arch::config::ConfigurationInput>(std::move(input));
        config.loaded_values_ = std::make_shared<const SimConfig>(config);
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
        input.raw_text = parser.InputText();
        input.raw_text_available = true;
        auto config = Resolve(input.standard);
        CaptureResolvedCaseValues(config, input);
        config.loaded_input_ = std::make_shared<const arch::config::ConfigurationInput>(std::move(input));
        config.loaded_values_ = std::make_shared<const SimConfig>(config);
        if (reads) {
            CaptureReads(config, reads);
            config.parameter_reads = std::move(reads);
        }
        return config;
    }

    static void CaptureReads(const SimConfig& config,
                             const std::shared_ptr<arch::preview::ParameterReadTrace>& reads) {
        config.RequireLoadedValues();
        if (!reads) return;
        std::map<std::string, double> numeric;
        for (const auto& [key, record] : config.resolved_case_values_) {
            if (!record.explicit_input || !record.value) continue;
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, double> || std::is_same_v<T, std::int64_t>)
                    numeric[record.input_key] = static_cast<double>(value);
            }, *record.value);
        }
        const auto& raw = config.LoadedInput()->raw_tokens;
        reads->capture_input(raw, numeric, raw);
    }

private:
    static void CaptureResolvedCaseValues(SimConfig& config,
                                          const arch::config::ConfigurationInput& input) {
        config.loaded_case_id_ = input.case_id;
        config.material_model_owner_ = input.case_id + ":" + input.case_source_file;
        config.material_model_identity_ = input.case_source_sha256;
        const auto material_inputs = [&](const auto& records) {
            for (const auto& [key, record] : records) {
                if (!record.resolved) continue;
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, double>)
                        config.material_inputs_.emplace(key, arch::config::MaterialValue{
                            static_cast<double>(value), arch::config::MaterialOrigin::ResolvedInput,
                            "resolved:" + input.case_id, {}, key});
                }, *record.resolved);
            }
        };
        material_inputs(input.standard.parameters);
        material_inputs(input.model.parameters);
        const auto capture = [&](const auto& records, bool composition = false) {
            for (const auto& [key, record] : records) {
                SimConfig::ResolvedCaseValue value;
                value.input_key = key;
                if (record.resolved)
                    value.value = std::visit([](const auto& v) {
                        return arch::preview::parameter_value(v);
                    }, *record.resolved);
                if (const auto raw = input.raw_tokens.find(key); raw != input.raw_tokens.end())
                    value.raw = raw->second;
                if (composition) {
                    for (const auto& [raw_key, token] : input.raw_tokens)
                        if (arch::config::CompositionKey(raw_key) == key) {
                            value.raw = token;
                            value.input_key = raw_key;
                            break;
                        }
                }
                value.explicit_input = record.state == arch::config::InputState::Present;
                if (value.input_key != key)
                    config.resolved_case_values_.emplace(value.input_key, value);
                config.resolved_case_values_.emplace(key, std::move(value));
            }
        };
        capture(input.model.parameters);
        if (input.model.composition) capture(input.model.composition->parameters, true);
        capture(input.auxiliary);
    }

    static SimConfig Resolve(const arch::config::StandardInputResolution& inputs)
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
        assign(cfg.amr.jeans_cells, "jeans_cells");
        assign(cfg.amr.refine_var, "refine_var");
        arch::config::ResolveRefinementSelection(cfg.amr, cfg.grid.dim, cfg.physics.burn.use_burn,true,
            cfg.physics.gravity.type=="self",
            arch::config::SupportsJeansBackend(cfg.execution.compute_backend,cfg.grid.geometry));
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
            // ALL uses the same requested route as explicit JENS, without
            // treating a static configuration check as device readiness.
            cfg.io.vars.jens=cfg.physics.gravity.type=="self"
                && arch::config::SupportsJeansBackend(cfg.execution.compute_backend,cfg.grid.geometry);
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
        if (cfg.io.vars.jens)
            if (cfg.physics.gravity.type!="self"
                || !arch::config::SupportsJeansBackend(cfg.execution.compute_backend,cfg.grid.geometry))
                throw std::invalid_argument(std::string("JENS output ") + arch::config::JeansBackendRequirement + ".");

        arch::config::ValidateControls(cfg);
        return cfg;
    }
};
