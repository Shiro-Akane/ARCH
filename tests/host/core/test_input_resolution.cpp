/**
 * @file test_input_resolution.cpp
 * @brief Check missing inputs and conditional dependencies before resource creation.
 */
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include "core/config/InputResolution.h"

using namespace arch::config;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool error(const StandardInputResolution& result, const std::string& key, const std::string& code) {
    for (const auto& diagnostic : result.diagnostics)
        if (diagnostic.key == key && diagnostic.code == code) return true;
    return false;
}
StandardInputResolution resolve(const std::string& text, InputContext context = {}) {
    ConfigParser parser;
    std::istringstream stream(text);
    parser.Read(stream, "stdin");
    return ResolveStandardInput(parser, context);
}
std::string without(const std::string& text, std::string_view key) {
    std::istringstream lines(text);
    std::string result, line;
    while (std::getline(lines, line))
        if (!line.starts_with(std::string(key) + " ="))
            result += line + '\n';
    return result;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass shared fixture directory");
        std::ifstream file(std::string(argv[1]) + "/sod-valid.par");
        require(file.good(), "missing valid fixture");
        const std::string valid{std::istreambuf_iterator<char>(file), {}};
        InputContext sod;
        sod.needs_network = false;
        sod.needs_temperature_floor = false;
        sod.needs_composition_floor = false;
        const auto complete = resolve(valid, sod);
        require(complete.requirements_satisfied(), "valid standard requirements rejected");
        require(complete.parameters.size() == 94, "retired G must not be an active record");
        require(!complete.parameters.at("dt_min").parsed, "default invented explicit parsed value");
        require(complete.parameters.at("dt_min").source == InputValueSource::DocumentedDefault,
                "documented default lacks source");
        require(std::get<bool>(*complete.parameters.at("use_burn").parsed) == false,
                "explicit false lost");
        require(std::get<int>(*complete.parameters.at("nblockx2").parsed) == 0, "explicit zero lost");

        int required = 0, defaults = 0;
        for (const auto& d : standard_parameters) {
            defaults += AllowedDefault(d) != nullptr;
            if (d.requirement != RequirementKind::Required) continue;
            ++required;
            const auto missing = resolve(without(valid, d.key), sod);
            require(error(missing, std::string(d.key), "MISSING_PARAMETER"),
                    "required-key deletion was not diagnosed");
            require(!missing.parameters.at(std::string(d.key)).resolved,
                    "required-key deletion acquired fallback");
        }
        require(required == 19 && defaults == 25, "approved classification changed");
        const auto empty = resolve("");
        int missing_count = 0;
        for (const auto& d : empty.diagnostics) missing_count += d.code == "MISSING_PARAMETER";
        require(missing_count == 19, "empty input must not guess downstream choices");
        require(!empty.parameters.at("gamma").requirement.value, "unknown EOS treated as IdealGas");
        require(empty.parameters.at("restart_file").requirement.value == false, "restart default ignored");
        const auto no_switch = resolve(without(valid, "use_burn"), sod);
        require(!no_switch.parameters.at("ode_rtol").requirement.value,
                "missing burn switch treated as disabled");
        const auto initial = [&] {
            auto context = sod;
            context.purpose = ConfigurationPurpose::InitialState;
            return resolve(without(valid, "tmax"), context);
        }();
        require(initial.requirements_satisfied(), "init-only endpoint exemption lost");
        require(!resolve(valid).requirements_satisfied(), "undeclared case consumers claimed complete");

        for (const auto reconstruction : {"pcm", "ppm"}) {
            const auto value = resolve("reconstruct=" + std::string(reconstruction));
            require(value.parameters.at("limiter").requirement.value == false,
                    "non-MUSCL limiter requirement guessed");
        }
        require(error(resolve("reconstruct=PLM"), "limiter", "MISSING_PARAMETER"), "MUSCL alias ignored");
        const auto bad_axis = resolve("nblockx2=-1");
        require(error(bad_axis, "nblockx2", "INVALID_RANGE"), "negative axis count accepted");
        require(!bad_axis.parameters.at("x2_max").requirement.value, "invalid axis drove condition");
        const auto inactive_bad = resolve("use_burn=false\node_rtol=bad");
        require(error(inactive_bad, "ode_rtol", "INVALID_NUMBER"), "inactive bad token hidden");
        const auto unknown = resolve("eos_type=bogus\nsolver=bogus");
        require(error(unknown, "eos_type", "INVALID_OPTION"), "unknown EOS accepted");
        require(!unknown.parameters.at("gamma").requirement.value, "invalid EOS drove condition");
        require(!unknown.parameters.at("solver").parsed, "unknown method became effective value");
        const auto duplicate = resolve("use_burn=false\nuse_burn=true");
        require(duplicate.parameters.at("use_burn").state == InputState::Duplicate, "duplicate lost");
        require(!duplicate.parameters.at("use_burn").resolved, "duplicate selected an occurrence");

        const auto external = resolve("gravity_type=external");
        for (const auto key : {"gravity_g_x", "gravity_g_y", "gravity_g_z"})
            require(error(external, key, "MISSING_PARAMETER"), "gravity vector component guessed zero");
        const auto nse_off = resolve("use_burn=true\nuse_nse=auto\nnetwork_name=none");
        require(nse_off.parameters.at("nseTempThreshold").requirement.value == false, "NSE auto capability ignored");
        const auto nse_on = resolve("use_burn=true\nuse_nse=auto\nnetwork_name=aprox19");
        require(error(nse_on, "nseTempThreshold", "MISSING_PARAMETER"), "NSE requirement bypassed");
        const auto expect_missing = [&](const std::string& input,
                                        std::initializer_list<const char*> keys) {
            const auto inspected = resolve(input, sod);
            for (const auto key : keys)
                require(error(inspected, key, "MISSING_PARAMETER"), "conditional required key was omitted");
        };
        expect_missing("nblockx1=1\nnblockx2=1\nnblockx3=1",
            {"x1_min","x1_max","x1l_boundary_type","x1r_boundary_type",
             "x2_min","x2_max","x2l_boundary_type","x2r_boundary_type",
             "x3_min","x3_max","x3l_boundary_type","x3r_boundary_type"});
        expect_missing("solver=Roe", {"EntropyFix"});
        expect_missing("solver=HLL", {"hll_wave_speed"});
        expect_missing("eos_type=ideal", {"gamma"});
        expect_missing("eos_type=tabular", {"eos_table_path"});
        expect_missing("eos_type=helmholtz", {"eos_coulomb_mult"});
        expect_missing("use_burn=true",
            {"network_name","nuclearTempMin","nuclearDensMin","enucDtFactor",
             "use_nse","ode_solver","ode_rtol","ode_atol","dt_init","smallt","smallx"});
        expect_missing("use_diffusion=true",
            {"diff_integrator","diff_cfl","use_thermal_diff","use_viscous_diff","use_species_diff"});
        expect_missing("use_diffusion=true\neos_type=ideal\nuse_thermal_diff=true\nuse_viscous_diff=true\nuse_species_diff=true",
            {"alpha_therm","nu_visc","D_spec"});
        expect_missing("gravity_type=self", {"gravity_boundary","gravity_rtol","gravity_atol"});
        expect_missing("lrefinemin=0\nlrefinemax=1\nrefine_var=DENS",
            {"regrid_interval","refine_threshold","derefine_threshold"});
        expect_missing("lrefinemin=0\nlrefinemax=1", {"refine_var"});
        expect_missing("restart=true", {"restart_file"});
        const auto forbidden = resolve("use_diffusion=true\neos_type=helmholtz\nalpha_therm=0\nnu_visc=0\nD_spec=0");
        for (const auto key : {"alpha_therm", "nu_visc", "D_spec"})
            require(error(forbidden, key, "INAPPLICABLE_PARAMETER"), "explicit forbidden zero accepted");
        const auto tabular = resolve("use_diffusion=true\neos_type=tabular\nuse_thermal_diff=true");
        require(!tabular.parameters.at("alpha_therm").requirement.value, "unknown material transport guessed");
        require(error(resolve("timeintegrator=RK2\ngravity_G=1"), "gravity_G", "RETIRED_PARAMETER"), "G not retired");
        require(error(resolve("timeintegrator=RK2"), "timeintegrator", "RETIRED_PARAMETER"), "old alias restored");
        const auto bad_ranges = resolve(
            "use_burn=false\ncfl=2\ngamma=1\ntmax=-1\ndt_min=0\node_rtol=2", sod);
        for (const auto key : {"cfl", "gamma", "tmax", "dt_min", "ode_rtol"}) {
            require(error(bad_ranges, key, "INVALID_RANGE"), "partial scalar range error lost");
            const auto& record = bad_ranges.parameters.at(key);
            require(record.state == InputState::Invalid && record.parsed
                    && !record.resolved && !record.source,
                    "range-invalid explicit value was hidden or rescued by default");
            require(record.locations.size() == 1, "range error lost original location");
        }
        const auto boundary_values = resolve(
            "cfl=1\ntmax=0\ngravity_atol=0\node_dt_safe_fac=1\n"
            "ode_dt_fac_min=1\node_initial_dt_frac=1\node_dt_fac_max=1\n"
            "plt_dt=-1\nchk_dt=-1\nmax_steps=0\ncuda_device=0", sod);
        for (const auto key : {"cfl", "tmax", "gravity_atol", "ode_dt_safe_fac",
                              "ode_dt_fac_min", "ode_initial_dt_frac", "ode_dt_fac_max",
                              "plt_dt", "chk_dt", "max_steps", "cuda_device"})
            require(!error(boundary_values, key, "INVALID_RANGE"), "legal scalar boundary rejected");
        const auto bad_relations = resolve(
            "min_eint=2\nmax_eint=1\ndt_min=0.1\ndt_init=0.01\ndt_max=0.05\n"
            "nblockx1=1\nx1_min=2\nx1_max=1\nnblockx2=0\nnblockx3=1\n"
            "lrefinemin=2\nlrefinemax=1\nrefine_threshold=0.2\nderefine_threshold=0.3\n"
            "solver=Roe\nhll_wave_speed=davis\neos_type=ideal\neos_coulomb_mult=0.5", sod);
        for (const auto key : {"max_eint", "dt_init", "dt_max", "x1_max", "nblockx3",
                              "lrefinemax", "refine_threshold", "hll_wave_speed", "eos_coulomb_mult"}) {
            require(error(bad_relations, key, "INVALID_RANGE"), "cross-control error not aggregated");
            const auto& record = bad_relations.parameters.at(key);
            require(record.parsed && !record.resolved && !record.source,
                    "relation failure hid raw value or retained effective value");
        }
        for (const auto& diagnostic : bad_relations.diagnostics)
            if (diagnostic.code == "INVALID_RANGE")
                require(!diagnostic.related_keys.empty(), "relation lacks dependency evidence");
        const auto unknown_relation = resolve("max_eint=1\nx1_max=-1", sod);
        require(!error(unknown_relation, "max_eint", "INVALID_RANGE")
                && !error(unknown_relation, "x1_max", "INVALID_RANGE"),
                "missing relation dependency was replaced by a default");
        require(!resolve("nblockx1=1\nx1_min=-1e308\nx1_max=1e308", sod)
                    .parameters.at("x1_max").resolved, "overflowed active extent accepted");
        const auto valid_relations = resolve(
            "min_eint=1\nmax_eint=1\ndt_min=0.1\ndt_init=0.1\ndt_max=-1\n"
            "nblockx1=1\nx1_min=0\nx1_max=1\nnblockx2=0\nnblockx3=0\n"
            "lrefinemin=0\nlrefinemax=15\nrefine_threshold=1\nderefine_threshold=0\n"
            "solver=HLLC\nhll_wave_speed=Davis\neos_type=Helmholtz\neos_coulomb_mult=0", sod);
        for (const auto& diagnostic : valid_relations.diagnostics)
            require(diagnostic.code != "INVALID_RANGE", "legal relation boundary rejected");
        const auto bad_gravity = resolve(
            "gravity_type=self\ngeometry=cylindrical\ngravity_boundary=periodic\n"
            "nblockx1=1\nnblockx2=1\nnblockx3=0\nx1_min=-1\nx1_max=1\n"
            "x2_min=0\nx2_max=1\nx1l_boundary_type=outflow\n"
            "x1r_boundary_type=outflow\nx2l_boundary_type=outflow\nx2r_boundary_type=outflow", sod);
        for (const auto key : {"gravity_boundary", "x1_min", "x1l_boundary_type", "x2_max"})
            require(error(bad_gravity, key, "INVALID_RANGE"), "gravity topology error not aggregated");
        const auto valid_gravity = resolve(
            "gravity_type=self\ngeometry=cylindrical\ngravity_boundary=isolated\n"
            "nblockx1=1\nnblockx2=1\nnblockx3=0\nx1_min=0\nx1_max=1\n"
            "x2_min=0\nx2_max=2*pi\nx1l_boundary_type=reflecting\n"
            "x1r_boundary_type=outflow\nx2l_boundary_type=periodic\nx2r_boundary_type=periodic", sod);
        for (const auto& diagnostic : valid_gravity.diagnostics)
            require(diagnostic.code != "INVALID_RANGE", "existing cylindrical chart contract changed");
        const auto unknown_gravity = resolve("gravity_type=self\nx1_min=-1", sod);
        require(!error(unknown_gravity, "x1_min", "INVALID_RANGE"),
                "unknown geometry guessed a radial domain");
        std::cout << "PASS: standard presence, allowed defaults and conditional requirements\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
