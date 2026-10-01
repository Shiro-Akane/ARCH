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

namespace {
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

        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto output = std::filesystem::temp_directory_path() / ("arch-input-no-output-" + unique);
        const auto text = without(fixture, "out_dir") + "out_dir=" + output.string() + "\n";
        require(!std::filesystem::exists(output), "test output already exists");
        const auto config = RuntimeParams::LoadText(text, "declared-test", ConfigurationPurpose::Evolution);
        require(config.io.out_dir == output.string() && config.io.tmax == 0.2,
                "declared loading changed valid controls");
        require(!std::filesystem::exists(output), "loading created scientific output");
        for (const auto& definition : standard_parameters) {
            require(!config.custom_params.contains(std::string(definition.key))
                    && !config.custom_string_params.contains(std::string(definition.key)),
                    "standard input retained a mutable custom-map copy");
        }
        require(config.Get<double>("x_pos", -1.0) == 0.5,
                "case parameter lost its authoritative lexical value");
        require(config.Get<std::string>("log_dir", "missing") == "missing",
                "absent auxiliary input was synthesized into custom storage");
        auto injected = config;
        injected.custom_params["cfl"] = 0.99;
        injected.custom_string_params["network_name"] = "untrusted";
        for (const auto key : {"cfl", "network_name", "timeintegrator"}) {
            bool access_rejected = false;
            try { (void)injected.Get<std::string>(key, "fallback"); }
            catch (const ConfigValueError&) { access_rejected = true; }
            require(access_rejected, "standard/retired key bypassed typed ownership through Get");
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
        std::cout << "PASS: aggregate declared loading without model or scientific resources\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
