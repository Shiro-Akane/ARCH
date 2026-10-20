#include "api/configuration/ParameterMetadata.h"
#include "core/config/RuntimeParams.h"
#include <fstream>
#include <sstream>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool good, const char *message) {
    if (!good) throw std::runtime_error(message);
}
static std::string fixture;

// Reads exercise the production loader with a named complete input. The model
// factory must never run during these lexical/observation tests.
static SimConfig case_values(const std::string& text,
    std::shared_ptr<arch::preview::ParameterReadTrace> reads = {},
    const std::string& value_type = "float") {
    ConfigParser parser;
    std::istringstream stream(text);
    parser.Load(stream);
    std::istringstream original(fixture);
    std::string input, line;
    while (std::getline(original, line)) {
        bool replace = false;
        for (const auto& [key, value] : parser.GetAllParams())
            replace |= line.starts_with(key + " ") || line.starts_with(key + "=");
        if (!replace) input += line + "\n";
    }
    input += text;
    return RuntimeParams::LoadText(input, "reads-" + value_type,
        arch::config::ConfigurationPurpose::InitialState, std::move(reads));
}

int main(int argc, char** argv) {
    require(argc == 2, "complete parameter fixture required");
    std::ifstream file(argv[1]);
    require(file.good(), "fixture not readable");
    fixture.assign(std::istreambuf_iterator<char>(file), {});
    for (const std::string type : {"float", "int", "bool"}) {
        ProblemRegistry::Get().Register("reads-" + type, []() -> std::unique_ptr<ProblemGenerator> {
            throw std::runtime_error("parameter observations must not construct a model");
        }, {"parameter-read-test.cpp", "test-source", true,
            [type](const arch::config::StandardInputResolution&) {
                arch::config::CaseConfiguration declaration;
                declaration.complete = true;
                declaration.consumers.needs_network = false;
                declaration.consumers.needs_temperature_floor = false;
                declaration.consumers.needs_composition_floor = false;
                for (const auto* key : {"x_pos", "rho_left", "rho_right", "p_left",
                                        "p_right", "u_left", "u_right"})
                    declaration.parameters.push_back({key, "float", ""});
                declaration.parameters.push_back({"value", type, "", "simulation", {false, {}}});
                return declaration;
            }});
    }
    auto trace = std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
    auto config = case_values("x_pos=.35\n", trace);
    require(config.Get<double>("x_pos", .5) == .35, "observer must preserve Get result");
    bool unknown = false;
    try { (void)config.Get<double>("uncovered", 7); }
    catch (const ConfigValueError& e) { unknown = e.code == "UNDECLARED_PARAMETER_ACCESS"; }
    require(unknown && trace->reads().size() == 1, "undeclared access or bounded coverage");
    (void)config.Get<double>("x_pos", .5);
    require(!trace->reads().at("x_pos").ambiguous, "identical repeated reads are unambiguous");
    (void)config.Get<double>("x_pos", .7);
    require(trace->reads().at("x_pos").ambiguous, "different observed fallback arguments must not be merged");
    auto output = arch::api::detail::Json::object();
    arch::api::PublishParameterMetadata(output, *trace, {{"Sod.x_pos", "x_pos", "x1", .35, 0, 1}}, true);
    const auto json = output.dump();
    require(json.find("\"effectiveValue\":null") != std::string::npos, "ambiguous effective value must be unknown");
    require(json.find("AMBIGUOUS_PARAMETER_READ") != std::string::npos, "ambiguity diagnostic");
    require(json.find("\"items\":[]") != std::string::npos, "ambiguous read cannot bind");

    auto changed = std::make_shared<arch::preview::ParameterReadTrace>(std::set<std::string>{"x_pos"});
    config = case_values("x_pos=.35\n", changed);
    // Exercise the observer's unattributed-value handling directly. Mutable
    // config overrides no longer exist; this is not an allowed Setup edit.
    changed->observe("x_pos", .5, .4, true);
    require(changed->reads().at("x_pos").source == "unknown", "unattributed value is not falsely attributed to input");
    arch::api::PublishParameterMetadata(output, *changed, {}, true);
    require(output.dump().find("\"constraints\"") == std::string::npos, "unknown constraints must be omitted");
    arch::api::PublishParameterMetadata(output, *changed, {{"Sod.x_pos", "x_pos", "x1", .4, 0, 1}}, true);
    require(output.dump().find("\"items\":[]") != std::string::npos, "unattributed value cannot bind");

    auto plain = case_values("x_pos=.2\n");
    require(!plain.parameter_reads && plain.Get<double>("x_pos", .5) == .2, "ordinary config has no observer");
    require(case_values("x_pos=1e2\n").Get<double>("x_pos", .5) == 100.0, "scientific notation");
    for (const std::string raw : {"2*pi", "exp(1)", "1.0suffix", "nan"}) {
        bool rejected = false;
        try { (void)case_values("x_pos=" + raw + "\n"); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "malformed custom parameter must fail before Get");
    }
    for (const bool observed : {false, true}) {
        const auto make_config = [&](const std::string& token, const std::string& type) {
            auto observer = observed ? std::make_shared<arch::preview::ParameterReadTrace>(
                std::set<std::string>{"value"}) : nullptr;
            return case_values("value=" + token + "\n", observer, type);
        };
        for (const std::string token : {"1.5", "1.0", "1e0", "2147483648", "-2147483649", "1suffix", "nan"}) {
            bool rejected = false;
            try { (void)make_config(token, "int").Get<int>("value", 99); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "integer strictness must not depend on observer");
        }
        for (const std::string token : {"0", "1", "2", "1.5", "yes", "nan"}) {
            bool rejected = false;
            try { (void)make_config(token, "bool").Get<bool>("value", false); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "boolean token strictness must not depend on observer");
        }
        for (const auto& [token, expected] : {
                std::pair<std::string, bool>{"true", true}, {"FALSE", false}}) {
            auto valid = make_config(token, "bool");
            require(valid.Get<bool>("value", !expected) == expected, "explicit bool lost");
            if (observed)
                require(valid.parameter_reads->reads().at("value").source == "explicit", "explicit bool provenance");
        }
        for (const auto& [token, expected] : {
                std::pair<std::string, int>{"+3", 3}, {"0", 0}, {"-2147483648", -2147483647 - 1}})
            require(make_config(token, "int").Get<int>("value", 999) == expected, "valid integer changed");
    }
    for (const auto number : {1.5, std::numeric_limits<double>::infinity(), 2147483648.0}) {
        bool rejected = false;
        try { ConfigParser::ValidateNumeric<int>("integer", number); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "numeric conversion silently truncated");
    }
    bool unprepared = false;
    try { (void)SimConfig{}.Get<double>("value", 7); }
    catch (const ConfigValueError& e) { unprepared = e.code == "INCOMPLETE_CONFIGURATION"; }
    require(unprepared, "unloaded config supplied an implicit model value");
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(0), "strict lower bound");
    require(arch::preview::AxisPosition{"", "", "x1", .5, 0, 1}.outside(1), "strict upper bound");
    std::cout << "Declared parameter observations, strict types and provenance passed\n";
}
