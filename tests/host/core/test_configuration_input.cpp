/**
 * @file test_configuration_input.cpp
 * @brief Exercise declared production loading before any model or output exists.
 */
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "core/config/RuntimeParams.h"
#include "fixtures/config/burn_controller_input.h"
#include "physics/network/aprox13/NetAprox13.h"
#include "physics/network/aprox19/NetAprox19.h"

namespace {
template<class T> concept HasMutableCaseMaps = requires(T value) { value.custom_params; value.custom_string_params; };
static_assert(!HasMutableCaseMaps<SimConfig>);
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string without(const std::string& input, const std::string& key) {
    std::istringstream lines(input);
    std::string output, line;
    while (std::getline(lines, line))
        if (!line.starts_with(key + " ") && !line.starts_with(key + "="))
            output += line + "\n";
    return output;
}
arch::config::ConfigurationInput inspect(const std::string& text,
    arch::config::ConfigurationPurpose purpose = arch::config::ConfigurationPurpose::Evolution) {
    ConfigParser parser;
    std::istringstream stream(text);
    parser.Read(stream);
    return arch::config::AnalyzeConfigurationInput(parser, "declared-test", purpose);
}
bool has(const arch::config::ConfigurationInput& input, const std::string& key,
         const std::string& code) {
    return std::any_of(input.diagnostics.begin(), input.diagnostics.end(),
        [&](const auto& error) { return error.key == key && error.code == code; });
}
}
int main(int argc, char** argv) {
    try {
        using namespace arch::config;
        require(argc == 2, "fixture directory required");
        std::ifstream file(std::filesystem::path(argv[1]) / "sod-valid.par");
        require(file.good(), "fixture not readable");
        const std::string fixture((std::istreambuf_iterator<char>(file)), {});
        auto& registry = ProblemRegistry::Get();
        registry.Register("declared-test", []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("input analysis constructed a model");
        }, {"test", "test", true, [](const StandardInputResolution&) {
            CaseConfiguration declaration;
            declaration.complete = true;
            declaration.consumers.needs_network = false;
            declaration.consumers.needs_temperature_floor = false;
            declaration.consumers.needs_composition_floor = false;
            declaration.parameters = {
                {"x_pos", "float", "cm"}, {"rho_left", "float", "g/cm^3"},
                {"rho_right", "float", "g/cm^3"}, {"p_left", "float", "erg/cm^3"},
                {"p_right", "float", "erg/cm^3"}, {"u_left", "float", "cm/s"},
                {"u_right", "float", "cm/s"}};
            return declaration;
        }});
        for (const auto& definition : standard_parameters) {
            require(definition.declared_default.has_value()
                        == (definition.requirement == RequirementKind::Optional),
                    "unapproved standard runtime default exists");
        }
        auto input = inspect(fixture);
        input.RequireDeclaredInputs();
        require(input.requirements_known(), "complete input retained unknown requirements");
        const auto& log = input.auxiliary.at("log_dir");
        require(log.state == InputState::Missing && !log.parsed
                && log.source == InputValueSource::Derived && log.source_evidence
                && std::get<std::string>(*log.resolved) == "output/sod_standard",
                "derived log directory was reported as explicit/default input");
        input = inspect(fixture + "log_dir=separate logs\n");
        require(input.auxiliary.at("log_dir").source == InputValueSource::Input,
                "explicit log path overwritten by derived path");
        input = inspect(without(without(fixture, "rho_left"), "cfl"));
        require(has(input, "rho_left", "MISSING_PARAMETER") && has(input, "cfl", "MISSING_PARAMETER"),
                "standard/case missing errors were not aggregated");
        input = inspect(fixture + "broken line\nx_pos=0.2\nunknown_input=1\n");
        require(has(input, "", "MALFORMED_LINE") && has(input, "x_pos", "DUPLICATE_PARAMETER")
                && has(input, "unknown_input", "UNKNOWN_PARAMETER"),
                "syntax/duplicate/unknown diagnostics were not aggregated");
        require(std::count_if(input.diagnostics.begin(), input.diagnostics.end(),
            [](const auto& error) { return error.key == "x_pos" && error.code == "DUPLICATE_PARAMETER"; }) == 1,
            "duplicate input was diagnosed twice");
        try { input.RequireDeclaredInputs(); require(false, "invalid input accepted"); }
        catch (const ConfigInputError& error) {
            require(std::string(error.what()).find("[x_pos]") != std::string::npos,
                    "CLI aggregate does not identify failed keys");
        }
        const auto no_endpoint = without(fixture, "tmax");
        require(has(inspect(no_endpoint), "tmax", "MISSING_PARAMETER"), "evolution omitted endpoint");
        inspect(no_endpoint, ConfigurationPurpose::InitialState).RequireDeclaredInputs();


        // NSE parsing belongs to the complete input boundary, not checkpoint
        // serialization. Missing burn controls must not re-create old defaults.
        const auto burn_input = without(without(fixture, "use_burn"), "network_name")
            + "use_burn=true\nnetwork_name=aprox19\ndt_init=1e-16\n"
              "nuclearTempMin=1e9\nnuclearDensMin=1e-10\nsmallt=1e5\nsmallx=1e-20\n"
              "enucDtFactor=1e30\node_solver=BE_NR\node_rtol=1e-4\node_atol=1e-8\n";
        for (const std::string request : {"true", "false", "auto", "AuTo", "TRUE"}) {
            const auto config = RuntimeParams::LoadText(burn_input + "use_nse=" + request
                + "\nnseTempThreshold=5.25e9\nnseDensThreshold=2.75e6\n",
                "declared-test", ConfigurationPurpose::Evolution);
            const auto& burn = config.physics.burn;
            require(burn.nse_auto == (request == "auto" || request == "AuTo"),
                    "NSE auto parsing must be case-insensitive");
            require(burn.use_nse == (request != "false"), "NSE explicit boolean changed");
            require(burn.nseTempThreshold == 5.25e9 && burn.nseDensThreshold == 2.75e6,
                    "NSE mode changed explicit activation thresholds");
        }
        require(has(inspect(burn_input), "use_nse", "MISSING_PARAMETER"),
                "missing NSE selection was defaulted");
        const auto missing_thresholds = inspect(burn_input + "use_nse=true\n");
        require(has(missing_thresholds, "nseTempThreshold", "MISSING_PARAMETER")
                && has(missing_thresholds, "nseDensThreshold", "MISSING_PARAMETER"),
                "missing NSE thresholds were defaulted");
        for (const std::string request : {"sometimes", "1"}) {
            require(has(inspect(burn_input + "use_nse=" + request + "\n"),
                        "use_nse", "INVALID_OPTION"), "invalid NSE option accepted");
        }
        for (const auto& [key, token] : {
                std::pair{"nseTempThreshold", "nan"}, {"nseTempThreshold", "0"},
                {"nseDensThreshold", "-1"}, {"nseDensThreshold", "inf"}}) {
            auto invalid = burn_input + "use_nse=auto\nnseTempThreshold=5.25e9\nnseDensThreshold=2.75e6\n";
            invalid = without(invalid, key) + key + "=" + token + "\n";
            bool rejected = false;
            try { (void)RuntimeParams::LoadText(invalid, "declared-test", ConfigurationPurpose::Evolution); }
            catch (const ConfigInputError& error) {
                rejected = std::any_of(error.diagnostics.begin(), error.diagnostics.end(),
                    [&](const auto& item) { return item.key == key && item.code != "MISSING_PARAMETER"; });
            }
            require(rejected, "invalid explicit NSE threshold accepted or misreported as missing");
        }

        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto output = std::filesystem::temp_directory_path() / ("arch-input-no-output-" + unique);
        const auto text = without(fixture, "out_dir") + "out_dir=" + output.string() + "\n";
        require(!std::filesystem::exists(output), "test output already exists");
        const auto config = RuntimeParams::LoadText(text, "declared-test", ConfigurationPurpose::Evolution);
        require(config.io.out_dir == output.string() && config.io.tmax == 0.2,
                "declared loading changed valid controls");
        require(!std::filesystem::exists(output), "loading created scientific output");
        const auto origin = config.LoadedInput();
        static_assert(std::is_const_v<std::remove_reference_t<decltype(*origin)>>);
        require(origin && origin->case_id == "declared-test"
                && origin->purpose == ConfigurationPurpose::Evolution,
                "loaded input lost case or purpose identity");
        require(!SimConfig{}.LoadedInput(), "default storage claimed inspected inputs");
        const auto& loaded_cfl = origin->standard.parameters.at("cfl");
        require(loaded_cfl.source == InputValueSource::Input
                && std::get<double>(*loaded_cfl.resolved) == 0.4
                && !loaded_cfl.locations.empty()
                && origin->raw_tokens.at("cfl") == "0.4",
                "explicit input lost lexical value, location or typed origin");
        const auto& default_steps = origin->standard.parameters.at("ode_max_substeps");
        require(default_steps.state == InputState::Missing
                && default_steps.source == InputValueSource::DocumentedDefault
                && !default_steps.parsed && default_steps.resolved,
                "approved default was relabelled explicit");
        const auto& absent_restart = origin->standard.parameters.at("restart_file");
        require(absent_restart.state == InputState::Missing && !absent_restart.resolved
                && !absent_restart.source, "inactive absent input acquired a source");
        require(origin->model.parameters.at("x_pos").source == InputValueSource::Input
                && origin->auxiliary.at("log_dir").source == InputValueSource::Derived,
                "case or auxiliary origin was lost");
        auto edited = config;
        edited.numerics.cfl = 0.3;
        require(edited.LoadedInput() == origin
                && std::get<double>(*origin->standard.parameters.at("cfl").resolved) == 0.4
                && std::get<double>(*origin->model.parameters.at("x_pos").resolved) == 0.5,
                "mutable preparation rewrote the original inspection evidence");
        const auto retained = RuntimeParams::LoadText(text, "declared-test",
            ConfigurationPurpose::InitialState).LoadedInput();
        require(retained && retained->purpose == ConfigurationPurpose::InitialState
                && retained->raw_tokens.at("x_pos") == "0.5",
                "origin lifetime depended on parser or configuration storage");

        const auto memory = RuntimeParams::LoadText(
            without(text, "nblockx1") + "nblockx1=2\n", "declared-test",
            ConfigurationPurpose::InitialState);
        require(memory.grid.dim == 1 && memory.grid.nblockx1 == 2,
                "in-memory grid configuration changed");
        const auto expressions = RuntimeParams::LoadText(
            without(text, "x1_max") + "x1_max=exp(1)\n", "declared-test",
            ConfigurationPurpose::InitialState);
        require(expressions.LoadedInput()->raw_tokens.at("x1_max") == "exp(1)",
                "expression origin was replaced by its evaluated number");
        require(std::abs(expressions.grid.x1_max - std::exp(1.0)) < 1e-14,
                "runtime grid expression evaluates exp");
        const auto acceleration = RuntimeParams::LoadText(
            without(text, "gravity_type") +
                "gravity_type=external\ngravity_g_x=exp(-17)\ngravity_g_y=0\ngravity_g_z=0\n",
            "declared-test", ConfigurationPurpose::InitialState);
        require(std::abs(acceleration.physics.gravity.g_x - std::exp(-17.0)) < 1e-20,
                "runtime external acceleration expression evaluates exp");


        require(config.Get<double>("x_pos", -1.0) == 0.5,
                "case parameter lost its authoritative lexical value");
        require(config.Get<std::string>("log_dir", "missing") == output.string()
                && !config.LoadedInput()->raw_tokens.contains("log_dir"),
                "derived auxiliary value did not use its resolved source");
        auto injected = config;
        for (const auto key : {"cfl", "network_name", "timeintegrator"}) {
            bool access_rejected = false;
            try { (void)injected.Get<std::string>(key, "fallback"); }
            catch (const ConfigValueError&) { access_rejected = true; }
            require(access_rejected, "standard/retired key bypassed typed ownership through Get");
        }

        for (const bool observed : {false, true}) {
            auto checked = config;
            if (observed) checked.parameter_reads =
                std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{}, true);
            bool undeclared = false;
            try { (void)checked.Get<double>("not_declared", 123.0); }
            catch (const ConfigValueError& error) {
                undeclared = error.code == "UNDECLARED_PARAMETER_ACCESS";
            }
            require(undeclared, "undeclared read accepted a caller fallback");
            bool wrong_type = false;
            try { (void)checked.Get<std::string>("x_pos", "fallback"); }
            catch (const ConfigValueError& error) {
                wrong_type = error.code == "PARAMETER_TYPE_MISMATCH";
            }
            require(wrong_type, "case reader changed the declared type");
        }

        bool rejected = false;
        try { (void)RuntimeParams::LoadText(without(text, "rho_left"), "declared-test",
                                           ConfigurationPurpose::Evolution); }
        catch (const ConfigInputError&) { rejected = true; }
        require(rejected && !std::filesystem::exists(output), "missing input acquired resources");
        rejected = false;
        try { (void)RuntimeParams::LoadText(text, "unregistered", ConfigurationPurpose::InitialState); }
        catch (const ConfigInputError& error) {
            rejected = std::any_of(error.diagnostics.begin(), error.diagnostics.end(),
                [](const auto& item) { return item.code == "UNKNOWN_CASE"; });
        }
        require(rejected, "unknown case accepted");
        // Model-provided controls use the same typed checks and source records;
        // these synthetic declarations do not alter any production model.
        const auto base_declaration = registry.DescribeConfiguration("declared-test", inspect(fixture).standard);
        const auto register_values = [&](const std::string& name, std::vector<ModelInputValue> values) {
            auto declaration = base_declaration;
            declaration.standard_values = std::move(values);
            registry.Register(name, []() -> std::unique_ptr<ProblemGenerator> {
                throw std::runtime_error("provision analysis constructed a model");
            }, {"provision-test.cpp", "test-source-identity", true,
                [declaration](const StandardInputResolution&) { return declaration; }});
        };
        auto conditional_declaration = base_declaration;
        conditional_declaration.parameters.push_back({"inactive_case_value", "float", "",
            "simulation", {false, {}}});
        registry.Register("inactive-case", []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("conditional analysis constructed a model");
        }, {"test.cpp", "source", true,
            [conditional_declaration](const StandardInputResolution&) { return conditional_declaration; }});
        const auto conditional = RuntimeParams::LoadText(text, "inactive-case",
            ConfigurationPurpose::Evolution);
        bool missing_read = false;
        try { (void)conditional.Get<double>("inactive_case_value", 123.0); }
        catch (const ConfigValueError& error) { missing_read = error.code == "MISSING_PARAMETER"; }
        require(missing_read, "inactive-but-consumed case value accepted caller fallback");
        register_values("provided", {
            {"cfl", 0.4, InputValueSource::CaseDefined, {"test:fixed-control", {}}},
            {"dt_max", 0.1, InputValueSource::Derived, {"test:half-endpoint", {"tmax"}}}});
        const auto provided = RuntimeParams::LoadText(without(text, "cfl"), "provided",
            ConfigurationPurpose::Evolution);
        provided.RequireLoadedValues();
        require(provided.numerics.cfl == 0.4 && provided.numerics.dt_max == 0.1,
                "model controls did not reach typed storage");
        const auto supplied = provided.LoadedInput();
        const auto& supplied_cfl = supplied->standard.parameters.at("cfl");
        require(supplied_cfl.state == InputState::Missing && !supplied_cfl.parsed
                && supplied_cfl.source == InputValueSource::CaseDefined
                && supplied_cfl.locations.empty() && !supplied->raw_tokens.contains("cfl")
                && supplied->case_source_sha256 == "test-source-identity",
                "model value was forged into explicit input or lost source identity");
        require(supplied->standard.parameters.at("dt_max").source == InputValueSource::Derived,
                "derivation became a documented default");
        const auto explicit_value = RuntimeParams::LoadText(without(text, "cfl") + "cfl=0.3\n",
            "provided", ConfigurationPurpose::Evolution);
        require(explicit_value.numerics.cfl == 0.3
                && explicit_value.LoadedInput()->standard.parameters.at("cfl").source == InputValueSource::Input,
                "model provision silently overrode explicit input");
        const auto rejects = [&](const std::string& name, const std::string& input_text,
                                 const std::string& code) {
            try { (void)RuntimeParams::LoadText(input_text, name, ConfigurationPurpose::Evolution); }
            catch (const ConfigInputError& error) {
                return std::any_of(error.diagnostics.begin(), error.diagnostics.end(),
                    [&](const auto& item) { return item.code == code; });
            }
            return false;
        };
        require(rejects("provided", without(text, "cfl") + "cfl=broken\n", "INVALID_NUMBER"),
                "invalid explicit value fell back to the model");
        register_values("bad-type", {{"cfl", 1, InputValueSource::CaseDefined, {"test", {}}}});
        require(rejects("bad-type", without(text, "cfl"), "INVALID_MODEL_PROVISION"),
                "wrong typed provision accepted");
        register_values("bad-value", {{"cfl", 2.0, InputValueSource::CaseDefined, {"test", {}}}});
        require(rejects("bad-value", without(text, "cfl"), "INVALID_RANGE"),
                "model value bypassed scalar range checks");
        register_values("bad-source", {{"cfl", 0.4, InputValueSource::DocumentedDefault, {"test", {}}}});
        require(rejects("bad-source", text, "INVALID_MODEL_PROVISION"),
                "model declaration expanded the allowed default catalog");
        register_values("bad-key", {{"gravity_G", 1.0, InputValueSource::CaseDefined, {"test", {}}}});
        require(rejects("bad-key", text, "INVALID_MODEL_PROVISION"), "retired key provision accepted");
        register_values("cycle", {
            {"dt_max", 0.1, InputValueSource::Derived, {"test", {"cfl"}}},
            {"cfl", 0.4, InputValueSource::Derived, {"test", {"dt_max"}}}});
        require(rejects("cycle", without(text, "cfl"), "UNRESOLVED_MODEL_DEPENDENCY"),
                "cyclic derivation accepted");
        register_values("unknown-dependency", {
            {"dt_max", 0.1, InputValueSource::Derived, {"test", {"unknown"}}}});
        require(rejects("unknown-dependency", text, "UNRESOLVED_MODEL_DEPENDENCY"),
                "unknown derivation dependency accepted");
        require(rejects("provided", without(text, "tmax"), "UNRESOLVED_MODEL_DEPENDENCY"),
                "missing derivation input retained a usable value");

        registry.Register("provided-consumers", []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("consumer analysis constructed a model");
        }, {"test.cpp", "source", true, [base_declaration](const StandardInputResolution& values) {
            auto declaration = base_declaration;
            declaration.standard_values = {{"eos_type", std::string("ideal"),
                InputValueSource::CaseDefined, {"test:ideal-model", {}}}};
            declaration.consumers.needs_network = input_detail::get<std::string>(values, "eos_type")
                ? std::optional<bool>(false) : std::nullopt;
            return declaration;
        }});
        const auto completed_consumers = RuntimeParams::LoadText(without(text, "eos_type"),
            "provided-consumers", ConfigurationPurpose::Evolution);
        require(completed_consumers.LoadedInput()->requirements_known()
                && completed_consumers.physics.eos_type == "ideal",
                "model-provided switch did not resolve consumer conditions");
        registry.Register("unstable-provision", []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("unstable analysis constructed a model");
        }, {"test.cpp", "source", true, [base_declaration](const StandardInputResolution& values) {
            auto declaration = base_declaration;
            declaration.standard_values = {{"cfl",
                input_detail::get<double>(values, "cfl") ? 0.3 : 0.4,
                InputValueSource::CaseDefined, {"test", {}}}};
            return declaration;
        }});
        require(rejects("unstable-provision", without(text, "cfl"), "UNSTABLE_MODEL_PROVISION"),
                "unstable model values silently converged or changed identity");
        auto missing_source = base_declaration;
        missing_source.standard_values = {{"cfl", 0.4, InputValueSource::CaseDefined, {"test", {}}}};
        registry.Register("no-provision-source", []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("source analysis constructed a model");
        }, {"", "", true, [missing_source](const StandardInputResolution&) { return missing_source; }});
        require(rejects("no-provision-source", text, "MISSING_MODEL_SOURCE"),
                "model-provided input lost mandatory registered source identity");

        const auto bd = burn_controller_fixture::load(ARCH_SOURCE_DIR);
        require(bd.numerics.tstep_change_factor == 2.0
                && bd.physics.burn.odeconfig.ode_solver == "BD"
                && bd.physics.burn.odeconfig.rtol == 1e-6
                && bd.physics.burn.odeconfig.atol == 1e-10
                && bd.Get<double>("rho0", -1.0) == 1e7
                && bd.Get<double>("temperature0", -1.0) == 3e9,
                "actual BD controller input values changed");
        const auto bd_origin = bd.LoadedInput();
        require(bd_origin && bd_origin->case_id == "BurnOneZone"
                && bd_origin->model.composition.has_value(),
                "file loader discarded composition input evidence");
        const auto& composition = bd_origin->model.composition->parameters;
        require(composition.at("xc12").source == InputValueSource::Input
                && std::get<double>(*composition.at("xc12").resolved) == 0.5
                && !composition.at("xc12").locations.front().source.empty(),
                "file composition lost explicit input or filename");
        SpeciesManager network_species;
        NetAprox13::RegisterSpecies(network_species);
        auto network_input = bd;
        const auto raw_fractions = arch::network::ReadInitialComposition(network_input, network_species);
        std::vector<double> expected(network_species.count(), 0.0);
        expected[network_species.GetSpeciesID("c12")] = 0.5;
        expected[network_species.GetSpeciesID("o16")] = 0.5;
        require(raw_fractions == expected, "declared sparse input changed raw fractions");
        require(arch::state::normalize_composition(expected.data(), network_species.count(), 1,
                network_input.physics.burn.smallx), "reference normalization failed");
        std::vector<double> normalized;
        NetAprox13::SetupInitialFractions(network_input, network_species, normalized);
        require(normalized == expected, "network normalization changed after input migration");

        std::ifstream bd_file(std::string(ARCH_SOURCE_DIR) + "/validation/burn/inputs/bd-config-v3.par");
        const std::string bd_text((std::istreambuf_iterator<char>(bd_file)), {});
        auto trace = std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{}, true);
        auto capitalized = RuntimeParams::LoadText(without(bd_text, "xc12") + "XC12=0.5\n",
            "BurnOneZone", ConfigurationPurpose::InitialState, trace);
        require(arch::network::ReadInitialComposition(capitalized, network_species) == raw_fractions,
                "case-insensitive species input changed values");
        require(trace->reads().at("XC12").source == "explicit"
                && trace->reads().at("XC12").raw_value == std::optional<std::string>("0.5")
                && trace->units.at("XC12").unit == "1",
                "canonical species lookup lost original input observation");
        require(capitalized.Get<double>("XC12", -1) == 0.5,
                "explicit species spelling did not resolve");
        SpeciesManager wrong_network_species;
        NetAprox19::RegisterSpecies(wrong_network_species);
        bool wrong_network_rejected = false;
        try { (void)arch::network::ReadInitialComposition(network_input, wrong_network_species); }
        catch (const ConfigValueError& error) {
            wrong_network_rejected = error.code == "UNDECLARED_PARAMETER_ACCESS";
        }
        require(wrong_network_rejected, "unregistered species were silently initialized to zero");
        SimConfig unprepared;
        bool composition_rejected = false;
        try { (void)arch::network::ReadInitialComposition(unprepared, network_species); }
        catch (const ConfigValueError& error) { composition_rejected = error.code == "INCOMPLETE_CONFIGURATION"; }
        require(composition_rejected, "network accepted default-constructed configuration");
        network_input.physics.burn.smallx = 1e-15;
        composition_rejected = false;
        try { (void)arch::network::ReadInitialComposition(network_input, network_species); }
        catch (const ConfigValueError& error) { composition_rejected = error.code == "UNDECLARED_CONFIGURATION_CHANGE"; }
        require(composition_rejected, "network accepted modified preparation controls");

        std::cout << "PASS: aggregate declared loading without model or scientific resources\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
